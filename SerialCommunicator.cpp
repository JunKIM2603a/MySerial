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

// 로그 파일 스트림
std::ofstream logFile;

// 로그 기록 함수
void logMessage(const std::string& message) {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    // 콘솔과 파일에 동시 출력
    logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " - " << message << std::endl;
    std::cout << message << std::endl;
}

// ==========================================================
// UPDATE: 프레임 구조 정의
// ==========================================================
const char SOF = 0x02; // Start of Frame
const char EOF_BYTE = 0x03; // End of Frame
const int FRAME_HEADER_SIZE = sizeof(int); // 프레임 번호 (4 bytes)
const int FRAME_OVERHEAD = 1 + FRAME_HEADER_SIZE + 1; // SOF + FrameNum + EOF = 6 bytes

// 시리얼 포트 핸들 관리 클래스
class SerialPort {
public:
    SerialPort() : hComm(INVALID_HANDLE_VALUE) {}
    ~SerialPort() {
        if (hComm != INVALID_HANDLE_VALUE) {
            CloseHandle(hComm);
        }
    }

    bool open(const std::string& comport, int baudrate) {
        std::string portName = "\\\\.\\" + comport;
        hComm = CreateFileA(portName.c_str(),
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            0,
                            NULL);

        if (hComm == INVALID_HANDLE_VALUE) {
            logMessage("Error: Unable to open " + comport);
            return false;
        }

        DCB dcbSerialParams = { 0 };
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

        if (!GetCommState(hComm, &dcbSerialParams)) {
            logMessage("Error getting device state");
            CloseHandle(hComm);
            return false;
        }

        dcbSerialParams.BaudRate = baudrate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;

        if (!SetCommState(hComm, &dcbSerialParams)) {
            logMessage("Error setting device state");
            CloseHandle(hComm);
            return false;
        }

        // 데이터 교환을 위한 합리적인 타임아웃 설정
        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 3000; // 타임아웃을 3초로 약간 늘림
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 10;

        if (!SetCommTimeouts(hComm, &timeouts)) {
            logMessage("Error setting timeouts");
            CloseHandle(hComm);
            return false;
        }

        return true;
    }

    int write(const char* buffer, int length) {
        DWORD bytesWritten;
        if (!WriteFile(hComm, buffer, length, &bytesWritten, NULL)) {
            logMessage("Error writing to serial port");
            return -1;
        }
        return bytesWritten;
    }
    
    int read(char* buffer, int length) {
        DWORD totalBytesRead = 0;
        while (totalBytesRead < length) {
            DWORD bytesReadInThisCall = 0;
            if (!ReadFile(hComm, buffer + totalBytesRead, length - totalBytesRead, &bytesReadInThisCall, NULL)) {
                logMessage("Error during ReadFile call.");
                return -1;
            }
            if (bytesReadInThisCall > 0) {
                totalBytesRead += bytesReadInThisCall;
            } else {
                break;
            }
        }
        return totalBytesRead;
    }

private:
    HANDLE hComm;
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
    std::string logFileName = "serial_log_" + mode + ".txt";
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
    
    // 6 & 8. 데이터 송수신 및 무결성 검사
    const int frameSize = datasize + FRAME_OVERHEAD;
    std::vector<char> sendFrame(frameSize);
    std::vector<char> receiveFrame(frameSize);
    Results clientResults = {0, 0, 0};

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
    };

    std::thread senderThread(sender);
    senderThread.join();
    
    std::thread receiverThread(receiver);
    receiverThread.join();

    logMessage("Data exchange complete.");
    std::this_thread::sleep_for(std::chrono::seconds(1));

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

    // 7 & 9. 데이터 송수신 및 무결성 검사
    const int datasize = settings.datasize;
    const int num = settings.num;
    const int frameSize = datasize + FRAME_OVERHEAD;
    std::vector<char> sendFrame(frameSize);
    std::vector<char> receiveFrame(frameSize);
    Results serverResults = {0, 0, 0};

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
    };

    std::thread receiverThread(receiver);
    receiverThread.join();
    
    std::thread senderThread(sender);
    senderThread.join();

    logMessage("Data exchange complete.");
    std::this_thread::sleep_for(std::chrono::seconds(1));

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