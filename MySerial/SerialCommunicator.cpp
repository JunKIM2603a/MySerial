#include <iostream>
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <cstring> // for memcpy
#include <algorithm>
#include <atomic>
#include <sstream> // for std::ostringstream
#include <mutex>   // for thread-safe logging

// 로그 파일 스트림
std::ofstream logFile;
std::mutex logMutex;  // 로그 보호용 mutex

// Thread-safe 로그 기록 함수
void logMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    // 콘솔과 파일에 동시 출력
    logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " - " << message << std::endl;
    std::cout << message << std::endl;
}

// ==========================================================
// UPDATE: 프레임 구조 정의
// ==========================================================
const char SOF = 0x02;          // Start of Frame (데이터 프레임용)
const char SOF_ACK = 0x04;      // Start of ACK Frame (EOT, 제어 프레임용)
const char EOF_BYTE = 0x03;     // End of Frame
const int FRAME_HEADER_SIZE = sizeof(int); // 프레임 번호 (4 bytes)
const int FRAME_OVERHEAD = 1 + FRAME_HEADER_SIZE + 1; // SOF + FrameNum + EOF = 6 bytes
// 참고: datasize=1024일 때 프레임 크기 = 1030 bytes
// Windows 시리얼 포트 기본 버퍼: 약 64KB-128KB (약 60-120 프레임)

// ==========================================================
// 동기화 ACK 프로토콜
// ==========================================================
// ACK 프레임 형식: [SOF_ACK][R][E][A][D][Y][EOF] = 7 bytes
const char READY_ACK[] = {0x04, 'R', 'E', 'A', 'D', 'Y', 0x03};
const int READY_ACK_LEN = 7;

// Overlapped I/O를 지원하는 시리얼 포트 핸들 관리 클래스
class SerialPort {
public:
    SerialPort() : hComm(INVALID_HANDLE_VALUE), readEvent(NULL), writeEvent(NULL) {
        ZeroMemory(&readOverlapped, sizeof(OVERLAPPED));
        ZeroMemory(&writeOverlapped, sizeof(OVERLAPPED));
    }
    
    ~SerialPort() {
        if (readEvent) CloseHandle(readEvent);
        if (writeEvent) CloseHandle(writeEvent);
        if (hComm != INVALID_HANDLE_VALUE) {
            CloseHandle(hComm);
        }
    }

    bool open(const std::string& comport, int baudrate) {
        std::string portName = "\\\\.\\" + comport;
        
        // FILE_FLAG_OVERLAPPED 플래그로 비동기 I/O 활성화
        hComm = CreateFileA(portName.c_str(),
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED,  // Overlapped I/O 활성화
                            NULL);

        if (hComm == INVALID_HANDLE_VALUE) {
            logMessage("Error: Unable to open " + comport);
            return false;
        }

        // 이벤트 객체 생성 (비동기 I/O 완료 신호용)
        readEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        writeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        if (!readEvent || !writeEvent) {
            logMessage("Error: Failed to create event objects");
            CloseHandle(hComm);
            hComm = INVALID_HANDLE_VALUE;
            return false;
        }

        readOverlapped.hEvent = readEvent;
        writeOverlapped.hEvent = writeEvent;

        DCB dcbSerialParams = { 0 };
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

        if (!GetCommState(hComm, &dcbSerialParams)) {
            logMessage("Error getting device state");
            CloseHandle(hComm);
            hComm = INVALID_HANDLE_VALUE;
            return false;
        }

        dcbSerialParams.BaudRate = baudrate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;

        if (!SetCommState(hComm, &dcbSerialParams)) {
            logMessage("Error setting device state");
            CloseHandle(hComm);
            hComm = INVALID_HANDLE_VALUE;
            return false;
        }

        // Overlapped I/O에서는 타임아웃을 0으로 설정 (이벤트 기반)
        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(hComm, &timeouts)) {
            logMessage("Error setting timeouts");
            CloseHandle(hComm);
            hComm = INVALID_HANDLE_VALUE;
            return false;
        }

        // 버퍼 크기 설정 (128KB로 증가)
        if (!SetupComm(hComm, 131072, 131072)) {
            logMessage("Warning: Failed to set buffer size");
        }

        return true;
    }

    int write(const char* buffer, int length) {
        if (hComm == INVALID_HANDLE_VALUE) return -1;

        DWORD bytesWritten = 0;
        ResetEvent(writeOverlapped.hEvent);
        
        BOOL result = WriteFile(hComm, buffer, length, &bytesWritten, &writeOverlapped);
        
        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 비동기 작업 진행 중 - 완료 대기 (최대 30초)
                DWORD waitResult = WaitForSingleObject(writeOverlapped.hEvent, 30000);
                
                if (waitResult == WAIT_OBJECT_0) {
                    if (GetOverlappedResult(hComm, &writeOverlapped, &bytesWritten, FALSE)) {
                        return bytesWritten;
                    }
                } else if (waitResult == WAIT_TIMEOUT) {
                    logMessage("Error: Write timeout");
                    CancelIo(hComm);
                    return -1;
                }
            }
            logMessage("Error writing to serial port: " + std::to_string(error));
            return -1;
        }
        
        return bytesWritten;
    }
    
    int read(char* buffer, int length) {
        if (hComm == INVALID_HANDLE_VALUE) return -1;

        DWORD totalBytesRead = 0;
        int timeoutCount = 0;
        const int MAX_TIMEOUT_RETRIES = 3;  // 최대 3번 재시도
        
        while (totalBytesRead < length) {
            DWORD bytesReadInThisCall = 0;
            ResetEvent(readOverlapped.hEvent);
            
            BOOL result = ReadFile(hComm, 
                                  buffer + totalBytesRead, 
                                  length - totalBytesRead, 
                                  &bytesReadInThisCall, 
                                  &readOverlapped);
            
            if (!result) {
                DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING) {
                    // 비동기 작업 진행 중 - 완료 대기 (최대 30초)
                    DWORD waitResult = WaitForSingleObject(readOverlapped.hEvent, 30000);
                    
                    if (waitResult == WAIT_OBJECT_0) {
                        if (GetOverlappedResult(hComm, &readOverlapped, &bytesReadInThisCall, FALSE)) {
                            if (bytesReadInThisCall > 0) {
                                totalBytesRead += bytesReadInThisCall;
                                timeoutCount = 0;  // 데이터를 받았으므로 카운터 리셋
                            } else {
                                // 데이터가 없으면 재시도 전에 짧은 대기
                                if (totalBytesRead == 0 || timeoutCount >= MAX_TIMEOUT_RETRIES) {
                                    break;  // 처음부터 데이터 없거나 재시도 한계
                                }
                                timeoutCount++;
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                        } else {
                            logMessage("Error during GetOverlappedResult for read");
                            return -1;
                        }
                    } else if (waitResult == WAIT_TIMEOUT) {
                        // 타임아웃 발생 시, 이미 일부 데이터를 받았다면 반환
                        if (totalBytesRead > 0) {
                            logMessage("Warning: Read timeout but returning partial data (" + 
                                      std::to_string(totalBytesRead) + " bytes)");
                            break;
                        }
                        // 처음부터 타임아웃이면 에러
                        logMessage("Error: Read timeout");
                        CancelIo(hComm);
                        break;
                    }
                } else {
                    logMessage("Error during ReadFile call: " + std::to_string(error));
                    return -1;
                }
            } else {
                // 즉시 완료된 경우
                if (bytesReadInThisCall > 0) {
                    totalBytesRead += bytesReadInThisCall;
                    timeoutCount = 0;
                } else {
                    break;
                }
            }
        }
        
        return totalBytesRead;
    }

private:
    HANDLE hComm;
    OVERLAPPED readOverlapped;   // 읽기 작업용 OVERLAPPED 구조체
    OVERLAPPED writeOverlapped;  // 쓰기 작업용 OVERLAPPED 구조체
    HANDLE readEvent;            // 읽기 완료 이벤트
    HANDLE writeEvent;           // 쓰기 완료 이벤트
};

// 설정 정보 구조체
struct Settings {
    int datasize; // Payload size
    int num;
};

// 결과 정보 구조체
struct Results {
    long long totalReceivedBytes;
    int receivedNum;
    int errorCount;
};

// ==========================================================
// 동기화 ACK 함수
// ==========================================================

// READY ACK 전송 함수
bool sendReadyAck(SerialPort& serial) {
    int sent = serial.write(READY_ACK, READY_ACK_LEN);
    if (sent == READY_ACK_LEN) {
        logMessage("READY ACK sent.");
        return true;
    }
    logMessage("Error: Failed to send READY ACK.");
    return false;
}

// READY ACK 수신 함수 (타임아웃 30초, 느린 주기로 체크)
bool waitForReadyAck(SerialPort& serial) {
    logMessage("Waiting for READY ACK...");
    
    char ackBuffer[10];
    int attempts = 0;
    const int MAX_ATTEMPTS = 300;  // 30초 (각 시도마다 100ms)
    
    while (attempts < MAX_ATTEMPTS) {
        // ACK 프레임 크기로 읽기 시도 (7 bytes)
        int received = serial.read(ackBuffer, READY_ACK_LEN);
        
        if (received == READY_ACK_LEN) {
            // ACK 프레임 검증: [SOF_ACK][READY][EOF]
            if (ackBuffer[0] == SOF_ACK && 
                ackBuffer[1] == 'R' && 
                ackBuffer[2] == 'E' && 
                ackBuffer[3] == 'A' && 
                ackBuffer[4] == 'D' && 
                ackBuffer[5] == 'Y' && 
                ackBuffer[6] == EOF_BYTE) {
                logMessage("READY ACK received.");
                return true;
            } else {
                // 잘못된 데이터 수신 (지연된 프레임 데이터일 가능성)
                logMessage("Warning: Received unexpected data while waiting for ACK (SOF=" + 
                          std::to_string((unsigned char)ackBuffer[0]) + "), ignoring...");
            }
        } else if (received > 0 && received < READY_ACK_LEN) {
            // 부분 수신 - 잘못된 데이터일 가능성
            logMessage("Warning: Partial data received (" + std::to_string(received) + 
                      " bytes) while waiting for ACK, ignoring...");
        }
        
        attempts++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    logMessage("Error: Timeout waiting for READY ACK (30 seconds).");
    return false;
}

void clientMode(const std::string& comport, int baudrate, int datasize, int num);
void serverMode(const std::string& comport, int baudrate);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: program.exe <mode> [options]" << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  client <comport> <baudrate> <datasize> <num>" << std::endl;
        std::cerr << "  server <comport> <baudrate>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    
    // Get COM port from arguments
    std::string comport = "";
    if (mode == "client" && argc >= 3) {
        comport = argv[2];
    } else if (mode == "server" && argc >= 3) {
        comport = argv[2];
    }
    
    // Generate timestamp
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream logFileNameStream;
    logFileNameStream << "serial_log_" << mode << "_" << comport;
    logFileNameStream << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S");
    logFileNameStream << ".txt";
    
    std::string logFileName = logFileNameStream.str();
    logFile.open(logFileName, std::ios_base::app);

    if (mode == "client") {
        if (argc != 6) {
            logMessage("Error: Invalid arguments for client mode.");
            return 1;
        }
        clientMode(argv[2], std::stoi(argv[3]), std::stoi(argv[4]), std::stoi(argv[5]));
    } else if (mode == "server") {
        if (argc != 4) {
            logMessage("Error: Invalid arguments for server mode.");
            return 1;
        }
        serverMode(argv[2], std::stoi(argv[3]));
    } else {
        logMessage("Error: Unknown mode '" + mode + "'");
        return 1;
    }

    logFile.close();

    return 0;
}

void clientMode(const std::string& comport, int baudrate, int datasize, int num) {
    logMessage("--- Client Mode ---");
    SerialPort serial;
    if (!serial.open(comport, baudrate)) {
        return;
    }
    logMessage("Port " + comport + " opened successfully at " + std::to_string(baudrate) + " bps.");

    // 3. 클라이언트는 서버로 옵션을 전달
    Settings settings = {datasize, num};
    logMessage("Attempting to send settings to server...");
    if (serial.write(reinterpret_cast<char*>(&settings), sizeof(Settings)) != sizeof(Settings)) {
        logMessage("Error: Failed to send settings to server.");
        return;
    }
    logMessage("Settings sent to server: datasize=" + std::to_string(datasize) + ", num=" + std::to_string(num));

    // 5. 서버로부터 ACK 메시지 수신
    logMessage("Waiting for ACK from server...");
    char ack[4];
    int bytesRead = serial.read(ack, 3);
    if (bytesRead != 3) {
        logMessage("Error: Did not receive full ACK from server. Received " + std::to_string(bytesRead) + " bytes.");
        return;
    }
    ack[3] = '\0';
    if (std::string(ack) != "ACK") {
        logMessage("Error: Invalid response from server. Expected ACK, got: " + std::string(ack));
        return;
    }
    logMessage("ACK received from server.");
    
    // 6 & 8. 데이터 송수신 및 무결성 검사 (송수신 동시 진행)
    const int frameSize = datasize + FRAME_OVERHEAD;
    std::vector<char> sendFrame(frameSize);
    std::vector<char> receiveFrame(frameSize);
    Results clientResults = {0, 0, 0};
    
    // 스레드 완료 플래그
    std::atomic<bool> senderDone(false);
    std::atomic<bool> receiverDone(false);

    auto sender = [&]() {
        // 프레임 구성
        sendFrame[0] = SOF;
        for (int j = 0; j < datasize; ++j) {
            sendFrame[1 + FRAME_HEADER_SIZE + j] = static_cast<char>(j % 256);
        }
        sendFrame[frameSize - 1] = EOF_BYTE;

        for (int i = 0; i < num; ++i) {
            memcpy(&sendFrame[1], &i, FRAME_HEADER_SIZE);
            if (serial.write(sendFrame.data(), frameSize) != frameSize) {
                logMessage("Error sending frame " + std::to_string(i));
            } else {
                 logMessage("Sending frame " + std::to_string(i) + "...");
            }
        }
        senderDone = true;  // 송신 완료 표시
        logMessage("Sender thread completed.");
    };

    auto receiver = [&]() {
        std::vector<char> expectedPayload(datasize);
        for (int j = 0; j < datasize; ++j) {
            expectedPayload[j] = static_cast<char>(255 - (j % 256));
        }

        for (int i = 0; i < num; ++i) {
            int received = serial.read(receiveFrame.data(), frameSize);
            bool isFrameOk = true;
            std::string errorReason = "";

            if (received != frameSize) {
                isFrameOk = false;
                errorReason = "Size mismatch (got " + std::to_string(received) + ")";
            } else {
	    // 데이터 내용 검증
                int frameNum;
                memcpy(&frameNum, &receiveFrame[1], FRAME_HEADER_SIZE);

                if (receiveFrame[0] != SOF || receiveFrame[frameSize - 1] != EOF_BYTE) {
                    isFrameOk = false;
                    errorReason = "SOF/EOF mismatch";
                } else if (frameNum != i) {
                    isFrameOk = false;
                    errorReason = "Frame num mismatch (expected " + std::to_string(i) + ", got " + std::to_string(frameNum) + ")";
                } else if (memcmp(&receiveFrame[1 + FRAME_HEADER_SIZE], expectedPayload.data(), datasize) != 0) {
                    isFrameOk = false;
                    errorReason = "Payload content mismatch";
                }
            }

            if (isFrameOk) {
                clientResults.totalReceivedBytes += received;
                clientResults.receivedNum++;
                logMessage("Received frame " + std::to_string(i) + ": OK");
            } else {
                clientResults.errorCount++;
                logMessage("Received frame " + std::to_string(i) + ": NG - " + errorReason + ". Total errors: " + std::to_string(clientResults.errorCount));
            }
        }
        receiverDone = true;  // 수신 완료 표시
        logMessage("Receiver thread completed. Received " + std::to_string(clientResults.receivedNum) + "/" + std::to_string(num) + " frames.");
    };

    // 송신과 수신을 동시에 수행하여 버퍼 오버플로우 방지
    std::thread senderThread(sender);
    std::thread receiverThread(receiver);
    
    senderThread.join();
    receiverThread.join();

    logMessage("Data exchange complete.");
    
    // === 스레드 완료 확인 및 버퍼 정리 ===
    // 1. 양쪽 스레드가 완전히 종료될 때까지 대기
    int waitCount = 0;
    while ((!senderDone || !receiverDone) && waitCount < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitCount++;
    }
    
    if (!senderDone || !receiverDone) {
        logMessage("Warning: Thread completion timeout. Sender: " + 
                  std::string(senderDone ? "Done" : "Not Done") + 
                  ", Receiver: " + std::string(receiverDone ? "Done" : "Not Done"));
    }
    
    // 2. 모든 프레임이 수신되었는지 확인
    if (clientResults.receivedNum < num) {
        logMessage("Warning: Not all frames received (" + 
                  std::to_string(clientResults.receivedNum) + "/" + 
                  std::to_string(num) + "). Waiting for remaining data...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    // 3. 버퍼에 남은 데이터가 안정화될 때까지 대기
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // === 동기화 ACK 교환 (양쪽 준비 완료 확인) ===
    // 4. READY ACK 전송
    if (!sendReadyAck(serial)) {
        logMessage("Error: Failed to synchronize with server.");
        return;
    }
    
    // 2. 서버의 READY ACK 대기
    if (!waitForReadyAck(serial)) {
        logMessage("Error: Server not ready for result exchange.");
        return;
    }
    
    // 3. 양쪽 모두 준비됨 - 짧은 대기 후 결과 교환
    logMessage("Synchronization complete. Starting result exchange.");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 규칙 1: 클라이언트는 'Master'로서 결과를 먼저 전송한다.
    if (serial.write(reinterpret_cast<char*>(&clientResults), sizeof(Results)) != sizeof(Results)) {
        logMessage("Error: Failed to send results to server.");
    } else {
        logMessage("Client results sent to server.");
    }

    // 규칙 2: 서버로부터 오는 결과 응답을 수신한다.
    Results serverResults;
    if (serial.read(reinterpret_cast<char*>(&serverResults), sizeof(Results)) == sizeof(Results)) {
        logMessage("Results received from server.");
        // 12. 최종 결과 로깅
        logMessage("--- Final Client Report ---");
        logMessage("Sent: datasize=" + std::to_string(datasize) + ", num=" + std::to_string(num));
        logMessage("Received from Server: total bytes=" + std::to_string(serverResults.totalReceivedBytes) +
                   ", num=" + std::to_string(serverResults.receivedNum) + ", errors=" + std::to_string(serverResults.errorCount));
        logMessage("Client's own reception: total bytes=" + std::to_string(clientResults.totalReceivedBytes) +
                   ", num=" + std::to_string(clientResults.receivedNum) + ", errors=" + std::to_string(clientResults.errorCount));
    } else {
        logMessage("Error: Failed to receive results from server.");
    }
}

void serverMode(const std::string& comport, int baudrate) {
    logMessage("--- Server Mode ---");
    SerialPort serial;
    if (!serial.open(comport, baudrate)) {
        return;
    }
    logMessage("Server waiting for a client on " + comport + "...");

    // 4. 클라이언트로부터 옵션 수신
    Settings settings;
    if (serial.read(reinterpret_cast<char*>(&settings), sizeof(Settings)) != sizeof(Settings)) {
        logMessage("Error: Failed to receive settings from client. Connection timed out or closed.");
        return;
    }
    
    logMessage("Client connected. Settings received: datasize=" + std::to_string(settings.datasize) + ", num=" + std::to_string(settings.num));

    // 5. 클라이언트로 ACK 메시지 전송
    if (serial.write("ACK", 3) != 3) {
        logMessage("Error: Failed to send ACK to client.");
        return;
    }
    logMessage("ACK sent to client.");

    // 7 & 9. 데이터 송수신 및 무결성 검사 (송수신 동시 진행)
    const int datasize = settings.datasize;
    const int num = settings.num;
    const int frameSize = datasize + FRAME_OVERHEAD;
    std::vector<char> sendFrame(frameSize);
    std::vector<char> receiveFrame(frameSize);
    Results serverResults = {0, 0, 0};
    
    // 스레드 완료 플래그
    std::atomic<bool> senderDone(false);
    std::atomic<bool> receiverDone(false);

    auto sender = [&]() {
        // 프레임 구성
        sendFrame[0] = SOF;
        for (int j = 0; j < datasize; ++j) {
            sendFrame[1 + FRAME_HEADER_SIZE + j] = static_cast<char>(255 - (j % 256));
        }
        sendFrame[frameSize - 1] = EOF_BYTE;

        for (int i = 0; i < num; ++i) {
            memcpy(&sendFrame[1], &i, FRAME_HEADER_SIZE);
            if (serial.write(sendFrame.data(), frameSize) != frameSize) {
                logMessage("Error sending frame " + std::to_string(i));
            } else {
                 logMessage("Sending frame " + std::to_string(i) + "...");
            }
        }
        senderDone = true;  // 송신 완료 표시
        logMessage("Sender thread completed.");
    };

    auto receiver = [&]() {
        std::vector<char> expectedPayload(datasize);
        for (int j = 0; j < datasize; ++j) {
            expectedPayload[j] = static_cast<char>(j % 256);
        }

        for (int i = 0; i < num; ++i) {
            int received = serial.read(receiveFrame.data(), frameSize);
            bool isFrameOk = true;
            std::string errorReason = "";

            if (received != frameSize) {
                isFrameOk = false;
                errorReason = "Size mismatch (got " + std::to_string(received) + ")";
            } else {
	    // 데이터 내용 검증
                int frameNum;
                memcpy(&frameNum, &receiveFrame[1], FRAME_HEADER_SIZE);

                if (receiveFrame[0] != SOF || receiveFrame[frameSize - 1] != EOF_BYTE) {
                    isFrameOk = false;
                    errorReason = "SOF/EOF mismatch";
                } else if (frameNum != i) {
                    isFrameOk = false;
                    errorReason = "Frame num mismatch (expected " + std::to_string(i) + ", got " + std::to_string(frameNum) + ")";
                } else if (memcmp(&receiveFrame[1 + FRAME_HEADER_SIZE], expectedPayload.data(), datasize) != 0) {
                    isFrameOk = false;
                    errorReason = "Payload content mismatch";
                }
            }

            if (isFrameOk) {
                serverResults.totalReceivedBytes += received;
                serverResults.receivedNum++;
                logMessage("Received frame " + std::to_string(i) + ": OK");
            } else {
                serverResults.errorCount++;
                logMessage("Received frame " + std::to_string(i) + ": NG - " + errorReason + ". Total errors: " + std::to_string(serverResults.errorCount));
            }
        }
        receiverDone = true;  // 수신 완료 표시
        logMessage("Receiver thread completed. Received " + std::to_string(serverResults.receivedNum) + "/" + std::to_string(num) + " frames.");
    };

    // 송신과 수신을 동시에 수행하여 버퍼 오버플로우 방지
    std::thread receiverThread(receiver);
    std::thread senderThread(sender);
    
    receiverThread.join();
    senderThread.join();

    logMessage("Data exchange complete.");
    
    // === 스레드 완료 확인 및 버퍼 정리 ===
    // 1. 양쪽 스레드가 완전히 종료될 때까지 대기
    int waitCount = 0;
    while ((!senderDone || !receiverDone) && waitCount < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitCount++;
    }
    
    if (!senderDone || !receiverDone) {
        logMessage("Warning: Thread completion timeout. Sender: " + 
                  std::string(senderDone ? "Done" : "Not Done") + 
                  ", Receiver: " + std::string(receiverDone ? "Done" : "Not Done"));
    }
    
    // 2. 모든 프레임이 수신되었는지 확인
    if (serverResults.receivedNum < num) {
        logMessage("Warning: Not all frames received (" + 
                  std::to_string(serverResults.receivedNum) + "/" + 
                  std::to_string(num) + "). Waiting for remaining data...");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    // 3. 버퍼에 남은 데이터가 안정화될 때까지 대기
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // === 동기화 ACK 교환 (양쪽 준비 완료 확인) ===
    // 4. READY ACK 전송
    if (!sendReadyAck(serial)) {
        logMessage("Error: Failed to synchronize with client.");
        return;
    }
    
    // 2. 클라이언트의 READY ACK 대기
    if (!waitForReadyAck(serial)) {
        logMessage("Error: Client not ready for result exchange.");
        return;
    }
    
    // 3. 양쪽 모두 준비됨 - 짧은 대기 후 결과 교환
    logMessage("Synchronization complete. Starting result exchange.");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 규칙 1: 서버는 'Slave'로서 클라이언트의 결과를 먼저 수신한다.
    Results clientResults;
    if (serial.read(reinterpret_cast<char*>(&clientResults), sizeof(Results)) == sizeof(Results)) {
        logMessage("Results received from client.");

        // 규칙 2: 클라이언트 결과를 받은 후, 자신의 결과를 응답으로 전송한다.
        if (serial.write(reinterpret_cast<char*>(&serverResults), sizeof(Results)) != sizeof(Results)) {
            logMessage("Error: Failed to send results to client.");
        } else {
            logMessage("Server results sent to client.");
        }

        // 13. 최종 결과 로깅
        logMessage("--- Final Server Report ---");
        logMessage("Sent: datasize=" + std::to_string(settings.datasize) + ", num=" + std::to_string(settings.num));
        logMessage("Received from Client: total bytes=" + std::to_string(clientResults.totalReceivedBytes) +
                   ", num=" + std::to_string(clientResults.receivedNum) + ", errors=" + std::to_string(clientResults.errorCount));
        logMessage("Server's own reception: total bytes=" + std::to_string(serverResults.totalReceivedBytes) +
                   ", num=" + std::to_string(serverResults.receivedNum) + ", errors=" + std::to_string(serverResults.errorCount));
    } else {
        logMessage("Error: Failed to receive results from client.");
    }
}