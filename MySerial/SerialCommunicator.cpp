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
#include <set>     // for tracking received frames

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
// 동기화 및 재전송 ACK/NAK 프로토콜
// ==========================================================
// READY ACK 프레임 형식: [SOF_ACK][R][E][A][D][Y][EOF] = 7 bytes
const char READY_ACK[] = {0x04, 'R', 'E', 'A', 'D', 'Y', 0x03};
const int READY_ACK_LEN = 7;

// 프레임별 ACK/NAK 형식: [SOF_ACK][TYPE(3bytes)][FrameNum(4bytes)][EOF] = 10 bytes
const int FRAME_ACK_LEN = 10;
const int MAX_RETRANSMIT_ATTEMPTS = 3;

// 동적 타임아웃 계산 상수
const double TIMEOUT_SAFETY_FACTOR = 3.0;  // 안전 여유율
const int BASE_TIMEOUT_MS = 1000;           // 기본 타임아웃 (ms)

// Overlapped I/O를 지원하는 시리얼 포트 핸들 관리 클래스
class SerialPort {
public:
    SerialPort() : hComm(INVALID_HANDLE_VALUE), readEvent(NULL), writeEvent(NULL), baudRate(0) {
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
    
    // 버퍼 플러싱 함수
    bool flush() {
        std::lock_guard<std::mutex> lockRead(readMutex);
        std::lock_guard<std::mutex> lockWrite(writeMutex);
        
        if (hComm == INVALID_HANDLE_VALUE) return false;
        
        DWORD flags = PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT;
        if (!PurgeComm(hComm, flags)) {
            logMessage("Warning: Failed to flush serial buffers");
            return false;
        }
        logMessage("Serial buffers flushed.");
        return true;
    }
    
    // 동적 타임아웃 계산 함수
    DWORD calculateTimeout(int dataSize) const {
        if (baudRate == 0) return 30000;  // fallback
        
        // 전송 시간 = (dataSize * 10 bits) / baudrate * 1000 (ms 변환) * safetyFactor + base
        double transmitTime = (static_cast<double>(dataSize) * 10.0 / baudRate) * 1000.0 * TIMEOUT_SAFETY_FACTOR;
        DWORD timeout = static_cast<DWORD>(transmitTime) + BASE_TIMEOUT_MS;
        
        // 최소 2초, 최대 60초
        if (timeout < 2000) timeout = 2000;
        if (timeout > 60000) timeout = 60000;
        
        return timeout;
    }
    
    int getBaudRate() const { return baudRate; }

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

        // baudrate 저장 (타임아웃 계산용)
        baudRate = baudrate;
        
        // 포트 오픈 후 버퍼 초기화
        DWORD flags = PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT;
        if (!PurgeComm(hComm, flags)) {
            logMessage("Warning: Failed to purge buffers on open");
        } else {
            logMessage("Port buffers purged on open.");
        }

        return true;
    }

    int write(const char* buffer, int length) {
        std::lock_guard<std::mutex> lock(writeMutex);
        
        if (hComm == INVALID_HANDLE_VALUE) return -1;

        DWORD bytesWritten = 0;
        
        // OVERLAPPED 구조체 초기화
        ZeroMemory(&writeOverlapped, sizeof(OVERLAPPED));
        writeOverlapped.hEvent = writeEvent;
        ResetEvent(writeOverlapped.hEvent);
        
        BOOL result = WriteFile(hComm, buffer, length, &bytesWritten, &writeOverlapped);
        
        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 동적 타임아웃 계산
                DWORD timeout = calculateTimeout(length);
                DWORD waitResult = WaitForSingleObject(writeOverlapped.hEvent, timeout);
                
                if (waitResult == WAIT_OBJECT_0) {
                    if (GetOverlappedResult(hComm, &writeOverlapped, &bytesWritten, FALSE)) {
                        return bytesWritten;
                    }
                } else if (waitResult == WAIT_TIMEOUT) {
                    logMessage("Error: Write timeout (" + std::to_string(timeout) + "ms)");
                    CancelIo(hComm);
                    return -1;
                }
            }
            logMessage("Error writing to serial port: " + std::to_string(error));
            return -1;
        }
        
        return bytesWritten;
    }
    
    int read(char* buffer, int length, DWORD timeoutMs = 0) {
        std::lock_guard<std::mutex> lock(readMutex);
        
        if (hComm == INVALID_HANDLE_VALUE) return -1;

        DWORD totalBytesRead = 0;
        int timeoutCount = 0;
        const int MAX_TIMEOUT_RETRIES = 3;
        
        // 타임아웃이 지정되지 않으면 동적 계산
        if (timeoutMs == 0) {
            timeoutMs = calculateTimeout(length);
        }
        
        while (totalBytesRead < length) {
            DWORD bytesReadInThisCall = 0;
            
            // OVERLAPPED 구조체 초기화
            ZeroMemory(&readOverlapped, sizeof(OVERLAPPED));
            readOverlapped.hEvent = readEvent;
            ResetEvent(readOverlapped.hEvent);
            
            BOOL result = ReadFile(hComm, 
                                  buffer + totalBytesRead, 
                                  length - totalBytesRead, 
                                  &bytesReadInThisCall, 
                                  &readOverlapped);
            
            if (!result) {
                DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING) {
                    DWORD waitResult = WaitForSingleObject(readOverlapped.hEvent, timeoutMs);
                    
                    if (waitResult == WAIT_OBJECT_0) {
                        if (GetOverlappedResult(hComm, &readOverlapped, &bytesReadInThisCall, FALSE)) {
                            if (bytesReadInThisCall > 0) {
                                totalBytesRead += bytesReadInThisCall;
                                timeoutCount = 0;
                            } else {
                                if (totalBytesRead == 0 || timeoutCount >= MAX_TIMEOUT_RETRIES) {
                                    break;
                                }
                                timeoutCount++;
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                        } else {
                            logMessage("Error during GetOverlappedResult for read");
                            return -1;
                        }
                    } else if (waitResult == WAIT_TIMEOUT) {
                        if (totalBytesRead > 0) {
                            logMessage("Warning: Read timeout but returning partial data (" + 
                                      std::to_string(totalBytesRead) + " bytes)");
                            break;
                        }
                        logMessage("Error: Read timeout (" + std::to_string(timeoutMs) + "ms)");
                        CancelIo(hComm);
                        break;
                    }
                } else {
                    logMessage("Error during ReadFile call: " + std::to_string(error));
                    return -1;
                }
            } else {
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
    
    // 프레임 재동기화 함수: SOF를 찾아 프레임 시작 위치 복구
    bool findFrameStart(int maxSearchBytes = 10240) {
        std::lock_guard<std::mutex> lock(readMutex);
        
        if (hComm == INVALID_HANDLE_VALUE) return false;
        
        logMessage("Attempting frame resynchronization...");
        char byte;
        int searchCount = 0;
        
        while (searchCount < maxSearchBytes) {
            DWORD bytesRead = 0;
            ZeroMemory(&readOverlapped, sizeof(OVERLAPPED));
            readOverlapped.hEvent = readEvent;
            ResetEvent(readOverlapped.hEvent);
            
            BOOL result = ReadFile(hComm, &byte, 1, &bytesRead, &readOverlapped);
            
            if (!result && GetLastError() == ERROR_IO_PENDING) {
                DWORD waitResult = WaitForSingleObject(readOverlapped.hEvent, 1000);
                if (waitResult == WAIT_OBJECT_0) {
                    GetOverlappedResult(hComm, &readOverlapped, &bytesRead, FALSE);
                } else {
                    logMessage("Frame resync timeout.");
                    return false;
                }
            }
            
            if (bytesRead == 1) {
                if (byte == SOF) {
                    logMessage("Frame start (SOF) found after " + std::to_string(searchCount) + " bytes.");
                    return true;
                }
                searchCount++;
            } else {
                break;
            }
        }
        
        logMessage("Frame resync failed: SOF not found within " + std::to_string(maxSearchBytes) + " bytes.");
        return false;
    }

private:
    HANDLE hComm;
    OVERLAPPED readOverlapped;   // 읽기 작업용 OVERLAPPED 구조체
    OVERLAPPED writeOverlapped;  // 쓰기 작업용 OVERLAPPED 구조체
    HANDLE readEvent;            // 읽기 완료 이벤트
    HANDLE writeEvent;           // 쓰기 완료 이벤트
    std::mutex readMutex;        // 읽기 작업 보호용 mutex
    std::mutex writeMutex;       // 쓰기 작업 보호용 mutex
    int baudRate;                // 타임아웃 계산용 baudrate 저장
};

// 설정 정보 구조체 (버전 2)
struct Settings {
    int protocolVersion;  // 프로토콜 버전 (현재: 2)
    int datasize;         // Payload size
    int num;              // 프레임 개수
    int reserved;         // 향후 확장용
};

const int PROTOCOL_VERSION = 2;

// 결과 정보 구조체 (확장 버전)
struct Results {
    long long totalReceivedBytes;  // 총 수신 바이트
    int receivedNum;               // 성공적으로 수신한 프레임 개수
    int errorCount;                // 에러 발생 횟수
    int retransmitCount;           // 재전송 횟수
    double elapsedSeconds;         // 경과 시간 (초)
    double throughputMBps;         // 처리량 (MB/s)
    double charactersPerSecond;    // CPS: 초당 문자(바이트) 수
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
        // ACK 프레임 크기로 읽기 시도 (7 bytes, 짧은 타임아웃)
        int received = serial.read(ackBuffer, READY_ACK_LEN, 100);
        
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

// ==========================================================
// 프레임별 ACK/NAK 프로토콜 함수
// ==========================================================

// ACK 프레임 전송: [SOF_ACK]['A']['C']['K'][FrameNum(4bytes)][EOF]
bool sendFrameAck(SerialPort& serial, int frameNum) {
    char ackFrame[FRAME_ACK_LEN];
    ackFrame[0] = SOF_ACK;
    ackFrame[1] = 'A';
    ackFrame[2] = 'C';
    ackFrame[3] = 'K';
    memcpy(&ackFrame[4], &frameNum, sizeof(int));
    ackFrame[9] = EOF_BYTE;
    
    int sent = serial.write(ackFrame, FRAME_ACK_LEN);
    return (sent == FRAME_ACK_LEN);
}

// NAK 프레임 전송: [SOF_ACK]['N']['A']['K'][FrameNum(4bytes)][EOF]
bool sendFrameNak(SerialPort& serial, int frameNum) {
    char nakFrame[FRAME_ACK_LEN];
    nakFrame[0] = SOF_ACK;
    nakFrame[1] = 'N';
    nakFrame[2] = 'A';
    nakFrame[3] = 'K';
    memcpy(&nakFrame[4], &frameNum, sizeof(int));
    nakFrame[9] = EOF_BYTE;
    
    int sent = serial.write(nakFrame, FRAME_ACK_LEN);
    return (sent == FRAME_ACK_LEN);
}

// ACK/NAK 프레임 수신 및 파싱
// 반환값: 1=ACK, 0=NAK, -1=타임아웃/에러
int waitForFrameAck(SerialPort& serial, int expectedFrameNum, int timeoutMs = 5000) {
    char ackBuffer[FRAME_ACK_LEN];
    int received = serial.read(ackBuffer, FRAME_ACK_LEN, timeoutMs);
    
    if (received != FRAME_ACK_LEN) {
        return -1;  // 타임아웃 또는 불완전한 수신
    }
    
    // 프레임 형식 검증
    if (ackBuffer[0] != SOF_ACK || ackBuffer[9] != EOF_BYTE) {
        logMessage("Warning: Invalid ACK/NAK frame format");
        return -1;
    }
    
    // 프레임 번호 확인
    int receivedFrameNum;
    memcpy(&receivedFrameNum, &ackBuffer[4], sizeof(int));
    
    if (receivedFrameNum != expectedFrameNum) {
        logMessage("Warning: Frame number mismatch in ACK/NAK (expected " + 
                  std::to_string(expectedFrameNum) + ", got " + std::to_string(receivedFrameNum) + ")");
        return -1;
    }
    
    // ACK인지 NAK인지 확인
    if (ackBuffer[1] == 'A' && ackBuffer[2] == 'C' && ackBuffer[3] == 'K') {
        return 1;  // ACK
    } else if (ackBuffer[1] == 'N' && ackBuffer[2] == 'A' && ackBuffer[3] == 'K') {
        return 0;  // NAK
    }
    
    logMessage("Warning: Unknown ACK/NAK type");
    return -1;
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
    logMessage("--- Client Mode (Protocol V" + std::to_string(PROTOCOL_VERSION) + ") ---");
    SerialPort serial;
    if (!serial.open(comport, baudrate)) {
        return;
    }
    logMessage("Port " + comport + " opened successfully at " + std::to_string(baudrate) + " bps.");

    // 3. 클라이언트는 서버로 옵션을 전달 (프로토콜 버전 포함)
    Settings settings = {PROTOCOL_VERSION, datasize, num, 0};
    logMessage("Attempting to send settings to server...");
    if (serial.write(reinterpret_cast<char*>(&settings), sizeof(Settings)) != sizeof(Settings)) {
        logMessage("Error: Failed to send settings to server.");
        return;
    }
    logMessage("Settings sent to server: protocolVersion=" + std::to_string(PROTOCOL_VERSION) + 
               ", datasize=" + std::to_string(datasize) + ", num=" + std::to_string(num));

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
    
    // 6 & 8. 데이터 송수신 및 무결성 검사 (Half-Duplex: 순차 송수신)
    const int frameSize = datasize + FRAME_OVERHEAD;
    std::vector<char> sendFrame(frameSize);
    std::vector<char> receiveFrame(frameSize);
    Results clientResults = {0, 0, 0, 0, 0.0, 0.0, 0.0};
    
    // 성능 측정 시작
    auto startTime = std::chrono::high_resolution_clock::now();

    // ===== Phase 1: 클라이언트가 데이터 송신 =====
    logMessage("Phase 1: Client sending data...");
    {
        // 프레임 구성 (클라이언트는 0-255 패턴)
        sendFrame[0] = SOF;
        for (int j = 0; j < datasize; ++j) {
            sendFrame[1 + FRAME_HEADER_SIZE + j] = static_cast<char>(j % 256);
        }
        sendFrame[frameSize - 1] = EOF_BYTE;

        for (int i = 0; i < num; ++i) {
            memcpy(&sendFrame[1], &i, FRAME_HEADER_SIZE);
            
            bool frameSent = false;
            int attempts = 0;
            
            while (!frameSent && attempts < MAX_RETRANSMIT_ATTEMPTS) {
                // 프레임 전송
                if (serial.write(sendFrame.data(), frameSize) != frameSize) {
                    logMessage("Error sending frame " + std::to_string(i) + " (attempt " + std::to_string(attempts + 1) + ")");
                    attempts++;
                    clientResults.retransmitCount++;
                    continue;
                }
                
                // ACK/NAK 대기
                int ackResult = waitForFrameAck(serial, i, 5000);
                
                if (ackResult == 1) {
                    // ACK 수신 성공
                    frameSent = true;
                    if (i % 100 == 0 || i < 10) {
                        logMessage("Frame " + std::to_string(i) + " sent and ACKed.");
                    }
                } else if (ackResult == 0) {
                    // NAK 수신 - 재전송 필요
                    logMessage("Frame " + std::to_string(i) + " NAKed, retransmitting (attempt " + std::to_string(attempts + 1) + ")");
                    attempts++;
                    clientResults.retransmitCount++;
                } else {
                    // 타임아웃 - 재전송 시도
                    logMessage("Frame " + std::to_string(i) + " ACK timeout, retransmitting (attempt " + std::to_string(attempts + 1) + ")");
                    attempts++;
                    clientResults.retransmitCount++;
                }
            }
            
            if (!frameSent) {
                logMessage("Error: Failed to send frame " + std::to_string(i) + " after " + std::to_string(MAX_RETRANSMIT_ATTEMPTS) + " attempts.");
                clientResults.errorCount++;
            }
        }
        logMessage("Phase 1 complete: All frames sent.");
    }

    // ===== Phase 2: 클라이언트가 데이터 수신 =====
    logMessage("Phase 2: Client receiving data...");
    {
        std::vector<char> expectedPayload(datasize);
        // 서버에서 오는 패턴: 255 - (j % 256)
        for (int j = 0; j < datasize; ++j) {
            expectedPayload[j] = static_cast<char>(255 - (j % 256));
        }
        
        std::set<int> receivedFrames;  // 중복 프레임 추적

        for (int i = 0; i < num; ++i) {
            int received = serial.read(receiveFrame.data(), frameSize);
            bool isFrameOk = true;
            std::string errorReason = "";
            int frameNum = -1;

            if (received != frameSize) {
                isFrameOk = false;
                errorReason = "Size mismatch (got " + std::to_string(received) + ")";
                
                // 프레임 재동기화 시도
                if (serial.findFrameStart()) {
                    logMessage("Frame resynchronized, continuing...");
                }
            } else {
                // 데이터 내용 검증
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
                // 중복 프레임 체크
                if (receivedFrames.find(frameNum) != receivedFrames.end()) {
                    logMessage("Warning: Duplicate frame " + std::to_string(frameNum) + " received, sending ACK again");
                    sendFrameAck(serial, frameNum);
                    i--;  // 카운터 유지
                    continue;
                }
                
                receivedFrames.insert(frameNum);
                clientResults.totalReceivedBytes += received;
                clientResults.receivedNum++;
                
                // ACK 전송
                if (!sendFrameAck(serial, frameNum)) {
                    logMessage("Warning: Failed to send ACK for frame " + std::to_string(frameNum));
                }
                
                if (i % 100 == 0 || i < 10) {
                    logMessage("Received frame " + std::to_string(i) + ": OK, ACK sent.");
                }
            } else {
                clientResults.errorCount++;
                
                // NAK 전송
                if (frameNum >= 0) {
                    if (!sendFrameNak(serial, frameNum)) {
                        logMessage("Warning: Failed to send NAK for frame " + std::to_string(frameNum));
                    }
                    logMessage("Received frame " + std::to_string(i) + ": NG - " + errorReason + ", NAK sent. Total errors: " + std::to_string(clientResults.errorCount));
                } else {
                    logMessage("Received frame " + std::to_string(i) + ": NG - " + errorReason + " (cannot send NAK)");
                }
                
                // 에러 후 재동기화 시도
                i--;  // 같은 프레임 번호로 재시도
            }
        }
        logMessage("Phase 2 complete: All frames received.");
    }


    // 성능 측정 종료
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    clientResults.elapsedSeconds = elapsed.count();
    
    // 성능 지표 계산
    if (clientResults.elapsedSeconds > 0) {
        clientResults.throughputMBps = (clientResults.totalReceivedBytes / (1024.0 * 1024.0)) / clientResults.elapsedSeconds;
        clientResults.charactersPerSecond = clientResults.totalReceivedBytes / clientResults.elapsedSeconds;
    }

    logMessage("Data exchange complete.");
    logMessage("Performance: " + std::to_string(clientResults.throughputMBps) + " MB/s, " + 
               std::to_string(clientResults.charactersPerSecond) + " chars/s (CPS)");
    
    // 버퍼 안정화 대기 (1초로 증가)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // === 동기화 ACK 교환 (양쪽 준비 완료 확인) ===
    // 1. READY ACK 전송
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
        
        // 12. 최종 결과 로깅 (확장된 정보 포함)
        logMessage("=== Final Client Report ===");
        logMessage("Test Configuration:");
        logMessage("  - Data size: " + std::to_string(datasize) + " bytes");
        logMessage("  - Frame count: " + std::to_string(num));
        logMessage("  - Protocol version: " + std::to_string(PROTOCOL_VERSION));
        
        logMessage("\nClient Transmission Results:");
        logMessage("  - Retransmissions: " + std::to_string(clientResults.retransmitCount));
        
        logMessage("\nClient Reception Results:");
        logMessage("  - Received frames: " + std::to_string(clientResults.receivedNum) + "/" + std::to_string(num));
        logMessage("  - Total bytes: " + std::to_string(clientResults.totalReceivedBytes));
        logMessage("  - Errors: " + std::to_string(clientResults.errorCount));
        logMessage("  - Elapsed time: " + std::to_string(clientResults.elapsedSeconds) + " seconds");
        logMessage("  - Throughput: " + std::to_string(clientResults.throughputMBps) + " MB/s");
        logMessage("  - CPS (chars/sec): " + std::to_string(clientResults.charactersPerSecond));
        
        logMessage("\nServer Reception Results:");
        logMessage("  - Received frames: " + std::to_string(serverResults.receivedNum) + "/" + std::to_string(num));
        logMessage("  - Total bytes: " + std::to_string(serverResults.totalReceivedBytes));
        logMessage("  - Errors: " + std::to_string(serverResults.errorCount));
        logMessage("  - Retransmissions: " + std::to_string(serverResults.retransmitCount));
        logMessage("  - Elapsed time: " + std::to_string(serverResults.elapsedSeconds) + " seconds");
        logMessage("  - Throughput: " + std::to_string(serverResults.throughputMBps) + " MB/s");
        logMessage("  - CPS (chars/sec): " + std::to_string(serverResults.charactersPerSecond));
        
        logMessage("=========================");
    } else {
        logMessage("Error: Failed to receive results from server.");
    }
}

void serverMode(const std::string& comport, int baudrate) {
    logMessage("--- Server Mode (Protocol V" + std::to_string(PROTOCOL_VERSION) + ") ---");
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
    
    // 프로토콜 버전 확인
    if (settings.protocolVersion != PROTOCOL_VERSION) {
        logMessage("Error: Protocol version mismatch! Client: " + std::to_string(settings.protocolVersion) + 
                   ", Server: " + std::to_string(PROTOCOL_VERSION));
        logMessage("Please update both client and server to the same version.");
        return;
    }
    
    logMessage("Client connected. Settings received: protocolVersion=" + std::to_string(settings.protocolVersion) + 
               ", datasize=" + std::to_string(settings.datasize) + ", num=" + std::to_string(settings.num));

    // 5. 클라이언트로 ACK 메시지 전송
    if (serial.write("ACK", 3) != 3) {
        logMessage("Error: Failed to send ACK to client.");
        return;
    }
    logMessage("ACK sent to client.");

    // 7 & 9. 데이터 송수신 및 무결성 검사 (Half-Duplex: 순차 송수신)
    const int datasize = settings.datasize;
    const int num = settings.num;
    const int frameSize = datasize + FRAME_OVERHEAD;
    std::vector<char> sendFrame(frameSize);
    std::vector<char> receiveFrame(frameSize);
    Results serverResults = {0, 0, 0, 0, 0.0, 0.0, 0.0};
    
    // 성능 측정 시작
    auto startTime = std::chrono::high_resolution_clock::now();

    // ===== Phase 1: 서버가 데이터 수신 =====
    logMessage("Phase 1: Server receiving data...");
    {
        std::vector<char> expectedPayload(datasize);
        // 클라이언트에서 오는 패턴: j % 256
        for (int j = 0; j < datasize; ++j) {
            expectedPayload[j] = static_cast<char>(j % 256);
        }
        
        std::set<int> receivedFrames;  // 중복 프레임 추적

        for (int i = 0; i < num; ++i) {
            int received = serial.read(receiveFrame.data(), frameSize);
            bool isFrameOk = true;
            std::string errorReason = "";
            int frameNum = -1;

            if (received != frameSize) {
                isFrameOk = false;
                errorReason = "Size mismatch (got " + std::to_string(received) + ")";
                
                // 프레임 재동기화 시도
                if (serial.findFrameStart()) {
                    logMessage("Frame resynchronized, continuing...");
                }
            } else {
                // 데이터 내용 검증
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
                // 중복 프레임 체크
                if (receivedFrames.find(frameNum) != receivedFrames.end()) {
                    logMessage("Warning: Duplicate frame " + std::to_string(frameNum) + " received, sending ACK again");
                    sendFrameAck(serial, frameNum);
                    i--;  // 카운터 유지
                    continue;
                }
                
                receivedFrames.insert(frameNum);
                serverResults.totalReceivedBytes += received;
                serverResults.receivedNum++;
                
                // ACK 전송
                if (!sendFrameAck(serial, frameNum)) {
                    logMessage("Warning: Failed to send ACK for frame " + std::to_string(frameNum));
                }
                
                if (i % 100 == 0 || i < 10) {
                    logMessage("Received frame " + std::to_string(i) + ": OK, ACK sent.");
                }
            } else {
                serverResults.errorCount++;
                
                // NAK 전송
                if (frameNum >= 0) {
                    if (!sendFrameNak(serial, frameNum)) {
                        logMessage("Warning: Failed to send NAK for frame " + std::to_string(frameNum));
                    }
                    logMessage("Received frame " + std::to_string(i) + ": NG - " + errorReason + ", NAK sent. Total errors: " + std::to_string(serverResults.errorCount));
                } else {
                    logMessage("Received frame " + std::to_string(i) + ": NG - " + errorReason + " (cannot send NAK)");
                }
                
                // 에러 후 재동기화 시도
                i--;  // 같은 프레임 번호로 재시도
            }
        }
        logMessage("Phase 1 complete: All frames received.");
    }

    // ===== Phase 2: 서버가 데이터 송신 =====
    logMessage("Phase 2: Server sending data...");
    {
        // 프레임 구성 (서버는 255 - (j % 256) 패턴)
        sendFrame[0] = SOF;
        for (int j = 0; j < datasize; ++j) {
            sendFrame[1 + FRAME_HEADER_SIZE + j] = static_cast<char>(255 - (j % 256));
        }
        sendFrame[frameSize - 1] = EOF_BYTE;

        for (int i = 0; i < num; ++i) {
            memcpy(&sendFrame[1], &i, FRAME_HEADER_SIZE);
            
            bool frameSent = false;
            int attempts = 0;
            
            while (!frameSent && attempts < MAX_RETRANSMIT_ATTEMPTS) {
                // 프레임 전송
                if (serial.write(sendFrame.data(), frameSize) != frameSize) {
                    logMessage("Error sending frame " + std::to_string(i) + " (attempt " + std::to_string(attempts + 1) + ")");
                    attempts++;
                    serverResults.retransmitCount++;
                    continue;
                }
                
                // ACK/NAK 대기
                int ackResult = waitForFrameAck(serial, i, 5000);
                
                if (ackResult == 1) {
                    // ACK 수신 성공
                    frameSent = true;
                    if (i % 100 == 0 || i < 10) {
                        logMessage("Frame " + std::to_string(i) + " sent and ACKed.");
                    }
                } else if (ackResult == 0) {
                    // NAK 수신 - 재전송 필요
                    logMessage("Frame " + std::to_string(i) + " NAKed, retransmitting (attempt " + std::to_string(attempts + 1) + ")");
                    attempts++;
                    serverResults.retransmitCount++;
                } else {
                    // 타임아웃 - 재전송 시도
                    logMessage("Frame " + std::to_string(i) + " ACK timeout, retransmitting (attempt " + std::to_string(attempts + 1) + ")");
                    attempts++;
                    serverResults.retransmitCount++;
                }
            }
            
            if (!frameSent) {
                logMessage("Error: Failed to send frame " + std::to_string(i) + " after " + std::to_string(MAX_RETRANSMIT_ATTEMPTS) + " attempts.");
                serverResults.errorCount++;
            }
        }
        logMessage("Phase 2 complete: All frames sent.");
    }


    // 성능 측정 종료
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    serverResults.elapsedSeconds = elapsed.count();
    
    // 성능 지표 계산
    if (serverResults.elapsedSeconds > 0) {
        serverResults.throughputMBps = (serverResults.totalReceivedBytes / (1024.0 * 1024.0)) / serverResults.elapsedSeconds;
        serverResults.charactersPerSecond = serverResults.totalReceivedBytes / serverResults.elapsedSeconds;
    }

    logMessage("Data exchange complete.");
    logMessage("Performance: " + std::to_string(serverResults.throughputMBps) + " MB/s, " + 
               std::to_string(serverResults.charactersPerSecond) + " chars/s (CPS)");
    
    // 버퍼 안정화 대기 (1초로 증가)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // === 동기화 ACK 교환 (양쪽 준비 완료 확인) ===
    // 1. READY ACK 전송
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

        // 13. 최종 결과 로깅 (확장된 정보 포함)
        logMessage("=== Final Server Report ===");
        logMessage("Test Configuration:");
        logMessage("  - Data size: " + std::to_string(settings.datasize) + " bytes");
        logMessage("  - Frame count: " + std::to_string(settings.num));
        logMessage("  - Protocol version: " + std::to_string(settings.protocolVersion));
        
        logMessage("\nServer Transmission Results:");
        logMessage("  - Retransmissions: " + std::to_string(serverResults.retransmitCount));
        
        logMessage("\nServer Reception Results:");
        logMessage("  - Received frames: " + std::to_string(serverResults.receivedNum) + "/" + std::to_string(settings.num));
        logMessage("  - Total bytes: " + std::to_string(serverResults.totalReceivedBytes));
        logMessage("  - Errors: " + std::to_string(serverResults.errorCount));
        logMessage("  - Elapsed time: " + std::to_string(serverResults.elapsedSeconds) + " seconds");
        logMessage("  - Throughput: " + std::to_string(serverResults.throughputMBps) + " MB/s");
        logMessage("  - CPS (chars/sec): " + std::to_string(serverResults.charactersPerSecond));
        
        logMessage("\nClient Reception Results:");
        logMessage("  - Received frames: " + std::to_string(clientResults.receivedNum) + "/" + std::to_string(settings.num));
        logMessage("  - Total bytes: " + std::to_string(clientResults.totalReceivedBytes));
        logMessage("  - Errors: " + std::to_string(clientResults.errorCount));
        logMessage("  - Retransmissions: " + std::to_string(clientResults.retransmitCount));
        logMessage("  - Elapsed time: " + std::to_string(clientResults.elapsedSeconds) + " seconds");
        logMessage("  - Throughput: " + std::to_string(clientResults.throughputMBps) + " MB/s");
        logMessage("  - CPS (chars/sec): " + std::to_string(clientResults.charactersPerSecond));
        
        logMessage("=========================");
    } else {
        logMessage("Error: Failed to receive results from client.");
    }
}