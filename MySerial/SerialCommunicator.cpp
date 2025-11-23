#include <iostream>

// Windows.h의 min/max 매크로 충돌 방지
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <sstream>
#include <mutex>
#include <set>
#include <map>
#include <queue>
#include <condition_variable>
#include <memory>

// ==========================================================
// Protocol Version 4: 최적화된 Selective Repeat ARQ with Burst Transmission
// 
// 프로토콜 특징:
// - Selective Repeat ARQ: 슬라이딩 윈도우 기반 연속 프레임 전송
// - 동적 윈도우 크기 조절: 네트워크 상태에 따라 4-32 프레임 자동 조절
// - 비트맵 기반 ACK: 32개 프레임 상태를 한 번에 확인
// - 멀티스레드 전송: Sender/Receiver 스레드 분리로 성능 최적화
// - 즉시 ACK 전송: 프레임 수신 즉시 ACK 전송으로 재전송 최소화
// - 3-way Handshake: 결과 교환 시 명확한 동기화
// - Burst 전송: 프레임 크기에 따라 최적화된 버스트 전송
// ==========================================================

// ==========================================================
// 전역 로그 관리
// ==========================================================
std::ofstream logFile;      // 로그 파일 스트림
std::mutex logMutex;        // 로그 쓰기 동기화용 뮤텍스

// Thread-safe 로그 출력 함수
// 콘솔과 로그 파일에 동시에 메시지 출력
void logMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " - " << message << std::endl;
    std::cout << message << std::endl;
}

// 디버그 모드 전역 변수 (대용량 프레임 감지 시 자동 활성화)
// DEBUG 매크로가 정의되지 않은 경우, debugMode가 true일 때만 디버그 로그 출력
bool debugMode = false;  // 디버그 모드 활성화 여부

#ifdef DEBUG
#define LOG_DEBUG(msg) logMessage("[DEBUG] " + std::string(msg))
#else
#define LOG_DEBUG(msg) do { if (debugMode) logMessage("[DEBUG] " + std::string(msg)); } while(0)
#endif

// ==========================================================
// Protocol V4 프로토콜 상수 정의
// ==========================================================
const int PROTOCOL_VERSION = 4;  // 현재 프로토콜 버전

// 프레임 구분 바이트 (프레임 경계 식별용)
const char SOF = 0x02;              // Start of Frame: 데이터 프레임 시작 바이트
const char SOF_ACK = 0x04;          // Start of ACK Frame: ACK 프레임 시작 바이트
const char EOF_BYTE = 0x03;         // End of Frame: 모든 프레임 종료 바이트

// 슬라이딩 윈도우 크기 제한 (동적 조절 범위)
const int WINDOW_SIZE_INIT = 16;    // 초기 윈도우 크기 (프레임 개수)
const int WINDOW_SIZE_MAX = 32;     // 최대 윈도우 크기 (프레임 개수)
const int WINDOW_SIZE_MIN = 4;      // 최소 윈도우 크기 (프레임 개수)

// 재전송 및 타임아웃 설정
const int MAX_RETRANSMIT_ATTEMPTS = 5;  // 최대 재전송 시도 횟수
const double TIMEOUT_SAFETY_FACTOR = 2.5;  // 타임아웃 안전 계수 (전송 시간의 2.5배)
const int BASE_TIMEOUT_MS = 500;         // 기본 타임아웃 (밀리초)

// 프레임 오버헤드 크기 정의
// V4 프로토콜 데이터 프레임 구조: [SOF(1)][FrameNum(4)][WindowSize(2)][Checksum(2)][Payload][EOF(1)]
// 헤더: SOF(1) + FrameNum(4) + WindowSize(2) + Checksum(2) = 9 bytes
// 트레일러: EOF(1) = 1 byte
// 총 오버헤드: 10 bytes
const int FRAME_HEADER_V3 = 1 + 4 + 2 + 2;  // 프레임 헤더 크기: 9 bytes
const int FRAME_TRAILER_V3 = 1;              // 프레임 트레일러 크기: 1 byte
const int FRAME_OVERHEAD_V3 = FRAME_HEADER_V3 + FRAME_TRAILER_V3;  // 총 오버헤드: 10 bytes

// ACK 프레임 구조: [SOF_ACK(1)][ACK(3)][BaseFrameNum(4)][Bitmap(4)][EOF(1)] = 13 bytes
// BaseFrameNum: 비트맵의 기준 프레임 번호
// Bitmap: 32비트로 최대 32개 프레임의 ACK 상태 표현
const int ACK_FRAME_SIZE = 13;

// READY ACK 프레임 구조: [SOF_ACK][R][E][A][D][Y][EOF] = 7 bytes
// Phase 3 결과 교환 시작 전 동기화 신호
const int READY_ACK_LEN = 7;
const char READY_ACK[] = {0x04, 'R', 'E', 'A', 'D', 'Y', 0x03};

// ==========================================================
// 데이터 구조체 정의
// ==========================================================

// V4 프로토콜 데이터 프레임 구조체
// 프레임 번호, 윈도우 크기, 체크섬, 페이로드를 포함하는 데이터 프레임
struct DataFrame {
    int frameNum;           // 프레임 순서 번호 (0부터 시작)
    uint16_t windowSize;    // 현재 슬라이딩 윈도우 크기
    uint16_t checksum;      // 페이로드 데이터의 체크섬 (XOR Rotate 방식)
    std::vector<char> payload;  // 실제 전송할 데이터
    
    DataFrame() : frameNum(0), windowSize(0), checksum(0) {}
    
    // 체크섬 계산 (XOR Rotate 체크섬)
    // 페이로드의 각 바이트를 XOR 연산하고 비트 회전을 수행하여 체크섬 생성
    uint16_t calculateChecksum() const {
        uint16_t sum = 0;
        for (size_t i = 0; i < payload.size(); ++i) {
            sum ^= static_cast<uint8_t>(payload[i]);
            sum = (sum << 1) | (sum >> 15);  // Rotate left (비트 왼쪽 회전)
        }
        return sum;
    }
    
    // 데이터 프레임을 바이트 배열로 직렬화
    // 구조: [SOF(1)][FrameNum(4)][WindowSize(2)][Checksum(2)][Payload][EOF(1)]
    void serialize(std::vector<char>& buffer) const {
        buffer.clear();
        buffer.reserve(FRAME_OVERHEAD_V3 + payload.size());
        
        buffer.push_back(SOF);
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&frameNum), 
                     reinterpret_cast<const char*>(&frameNum) + sizeof(int));
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&windowSize), 
                     reinterpret_cast<const char*>(&windowSize) + sizeof(uint16_t));
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&checksum), 
                     reinterpret_cast<const char*>(&checksum) + sizeof(uint16_t));
        buffer.insert(buffer.end(), payload.begin(), payload.end());
        buffer.push_back(EOF_BYTE);
    }
    
    // 바이트 배열로부터 데이터 프레임을 역직렬화
    // SOF/EOF 검증 및 각 필드 추출
    bool deserialize(const char* buffer, int length) {
        if (length < FRAME_OVERHEAD_V3) return false;
        if (buffer[0] != SOF || buffer[length - 1] != EOF_BYTE) return false;
        
        memcpy(&frameNum, buffer + 1, sizeof(int));
        memcpy(&windowSize, buffer + 5, sizeof(uint16_t));
        memcpy(&checksum, buffer + 7, sizeof(uint16_t));
        
        int payloadSize = length - FRAME_OVERHEAD_V3;
        payload.resize(payloadSize);
        memcpy(payload.data(), buffer + 9, payloadSize);
        
        return true;
    }
    
    // 체크섬 검증
    // 저장된 체크섬과 계산된 체크섬을 비교하여 데이터 무결성 확인
    bool verifyChecksum() const {
        return checksum == calculateChecksum();
    }
};

// ACK 프레임 구조체
// 비트맵 기반으로 최대 32개 프레임의 ACK 상태를 한 번에 전송
struct AckFrame {
    int baseFrameNum;    // 비트맵의 기준이 되는 프레임 번호
    uint32_t bitmap;     // 32비트로 최대 32개 프레임의 ACK 상태를 비트맵으로 표현
    
    AckFrame() : baseFrameNum(0), bitmap(0) {}
    
    // ACK 프레임을 바이트 배열로 직렬화
    // 구조: [SOF_ACK(1)][ACK(3)][BaseFrameNum(4)][Bitmap(4)][EOF(1)]
    void serialize(std::vector<char>& buffer) const {
        buffer.clear();
        buffer.resize(ACK_FRAME_SIZE);
        
        buffer[0] = SOF_ACK;
        buffer[1] = 'A';
        buffer[2] = 'C';
        buffer[3] = 'K';
        memcpy(buffer.data() + 4, &baseFrameNum, sizeof(int));
        memcpy(buffer.data() + 8, &bitmap, sizeof(uint32_t));
        buffer[12] = EOF_BYTE;
    }
    
    // 바이트 배열로부터 ACK 프레임을 역직렬화
    // SOF_ACK/EOF 및 "ACK" 문자열 검증 후 필드 추출
    bool deserialize(const char* buffer, int length) {
        if (length != ACK_FRAME_SIZE) return false;
        if (buffer[0] != SOF_ACK || buffer[12] != EOF_BYTE) return false;
        if (buffer[1] != 'A' || buffer[2] != 'C' || buffer[3] != 'K') return false;
        
        memcpy(&baseFrameNum, buffer + 4, sizeof(int));
        memcpy(&bitmap, buffer + 8, sizeof(uint32_t));
        
        return true;
    }
    
    // 특정 프레임 번호가 ACK되었는지 확인
    // baseFrameNum을 기준으로 offset을 계산하여 비트맵에서 해당 비트 확인
    bool isAcked(int frameNum) const {
        int offset = frameNum - baseFrameNum;
        if (offset < 0 || offset >= 32) return false;
        return (bitmap & (1u << offset)) != 0;
    }
    
    // 특정 프레임 번호에 대해 ACK를 설정
    // baseFrameNum을 기준으로 offset을 계산하여 비트맵의 해당 비트를 1로 설정
    void setAck(int frameNum) {
        int offset = frameNum - baseFrameNum;
        if (offset >= 0 && offset < 32) {
            bitmap |= (1u << offset);
        }
    }
};

// NAK 프레임 구조체 (ACK와 동일한 구조, 현재는 사용하지 않음)
// ACK와 동일한 비트맵 구조를 사용하여 NAK 상태를 표현
struct NakFrame {
    int baseFrameNum;    // 비트맵의 기준 프레임 번호
    uint32_t bitmap;     // 32비트 비트맵 (NAK된 프레임 표시)
    
    NakFrame() : baseFrameNum(0), bitmap(0) {}
    
    // NAK 프레임을 바이트 배열로 직렬화
    // 구조: [SOF_ACK(1)][NAK(3)][BaseFrameNum(4)][Bitmap(4)][EOF(1)]
    void serialize(std::vector<char>& buffer) const {
        buffer.clear();
        buffer.resize(ACK_FRAME_SIZE);
        
        buffer[0] = SOF_ACK;
        buffer[1] = 'N';
        buffer[2] = 'A';
        buffer[3] = 'K';
        memcpy(buffer.data() + 4, &baseFrameNum, sizeof(int));
        memcpy(buffer.data() + 8, &bitmap, sizeof(uint32_t));
        buffer[12] = EOF_BYTE;
    }
    
    // 바이트 배열로부터 NAK 프레임을 역직렬화
    bool deserialize(const char* buffer, int length) {
        if (length != ACK_FRAME_SIZE) return false;
        if (buffer[0] != SOF_ACK || buffer[12] != EOF_BYTE) return false;
        if (buffer[1] != 'N' || buffer[2] != 'A' || buffer[3] != 'K') return false;
        
        memcpy(&baseFrameNum, buffer + 4, sizeof(int));
        memcpy(&bitmap, buffer + 8, sizeof(uint32_t));
        
        return true;
    }
};

// 클라이언트 설정 정보 구조체
// Phase 1에서 클라이언트가 서버에 전송하는 통신 설정 정보
struct Settings {
    int protocolVersion;  // 프로토콜 버전 (현재 4)
    int datasize;          // 프레임당 페이로드 크기 (bytes)
    int num;               // 전송할 총 프레임 개수
    int reserved;          // 예약 필드 (확장용)
};

// 통신 결과 구조체
// Phase 3에서 클라이언트와 서버가 서로 교환하는 통신 결과 통계
struct Results {
    long long totalReceivedBytes;  // 총 수신 바이트 수
    int receivedNum;               // 수신한 프레임 개수
    int errorCount;                 // 에러 발생 횟수
    int retransmitCount;            // 재전송 횟수
    double elapsedSeconds;          // 경과 시간 (초)
    double throughputMBps;         // 처리량 (MB/s)
    double charactersPerSecond;    // 초당 문자 수 (CPS)
};

// ==========================================================
// SerialPort 클래스 (Overlapped I/O 기반 비동기 시리얼 통신)
// ==========================================================
// Windows API의 Overlapped I/O를 사용하여 비동기 시리얼 통신 구현
// 읽기/쓰기 작업을 동시에 수행할 수 있으며, 각 작업은 별도의 뮤텍스로 보호됨
class SerialPort {
public:
    // 생성자: 모든 핸들을 초기화하고 OVERLAPPED 구조체를 0으로 초기화
    SerialPort() : hComm(INVALID_HANDLE_VALUE), readEvent(NULL), writeEvent(NULL), baudRate(0) {
        ZeroMemory(&readOverlapped, sizeof(OVERLAPPED));
        ZeroMemory(&writeOverlapped, sizeof(OVERLAPPED));
    }
    
    // 소멸자: 생성된 모든 핸들을 안전하게 해제
    ~SerialPort() {
        if (readEvent) CloseHandle(readEvent);
        if (writeEvent) CloseHandle(writeEvent);
        if (hComm != INVALID_HANDLE_VALUE) {
            CloseHandle(hComm);
        }
    }
    
    // 시리얼 포트 열기 및 초기화
    // Overlapped I/O 모드로 포트를 열고, 통신 파라미터 설정, 버퍼 크기 설정
    bool open(const std::string& comport, int baudrate) {
        std::string portName = "\\\\.\\" + comport;  // Windows에서 COM10 이상을 위해 필요
        
        // Overlapped I/O 모드로 시리얼 포트 열기
        hComm = CreateFileA(portName.c_str(),
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED,  // 비동기 I/O 모드
                            NULL);

        if (hComm == INVALID_HANDLE_VALUE) {
            logMessage("Error: Unable to open " + comport);
            return false;
        }

        // 읽기/쓰기 작업 완료를 알리는 이벤트 객체 생성
        readEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        writeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        if (!readEvent || !writeEvent) {
            logMessage("Error: Failed to create event objects");
            CloseHandle(hComm);
            hComm = INVALID_HANDLE_VALUE;
            return false;
        }

        // OVERLAPPED 구조체에 이벤트 핸들 연결
        readOverlapped.hEvent = readEvent;
        writeOverlapped.hEvent = writeEvent;

        // DCB 구조체 초기화 및 현재 포트 상태 읽기
        DCB dcbSerialParams = { 0 };
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

        if (!GetCommState(hComm, &dcbSerialParams)) {
            logMessage("Error getting device state");
            CloseHandle(hComm);
            hComm = INVALID_HANDLE_VALUE;
            return false;
        }

        // 통신 파라미터 설정
        dcbSerialParams.BaudRate = baudrate;
        dcbSerialParams.ByteSize = 8;           // 8비트 데이터
        dcbSerialParams.StopBits = ONESTOPBIT; // 1 스톱 비트
        dcbSerialParams.Parity = NOPARITY;      // 패리티 없음
        
        // 외부 루프백을 위한 플로우 컨트롤 비활성화
        // DTR/RTS는 활성화하되, CTS/DSR/XON/XOFF 플로우 컨트롤은 비활성화
        dcbSerialParams.fOutxCtsFlow = FALSE;   // CTS 플로우 컨트롤 비활성화
        dcbSerialParams.fOutxDsrFlow = FALSE;   // DSR 플로우 컨트롤 비활성화
        dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;  // DTR 활성화
        dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;  // RTS 활성화
        dcbSerialParams.fOutX = FALSE;          // XON/XOFF 송신 플로우 컨트롤 비활성화
        dcbSerialParams.fInX = FALSE;           // XON/XOFF 수신 플로우 컨트롤 비활성화

        if (!SetCommState(hComm, &dcbSerialParams)) {
            logMessage("Error setting device state");
            CloseHandle(hComm);
            hComm = INVALID_HANDLE_VALUE;
            return false;
        }

        // 타임아웃 설정: Overlapped I/O에서는 0으로 설정하여 비동기 동작
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

        // 성능 향상을 위해 버퍼 크기를 128KB에서 1MB로 증가
        if (!SetupComm(hComm, 1048576, 1048576)) {
            logMessage("Warning: Failed to set buffer size to 1MB");
        }

        baudRate = baudrate;
        
        // 포트 오픈 시 기존 버퍼 내용 제거
        DWORD flags = PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT;
        if (!PurgeComm(hComm, flags)) {
            logMessage("Warning: Failed to purge buffers on open");
        } else {
            logMessage("Port buffers purged on open.");
        }

        return true;
    }
    
    // 데이터 쓰기 (비동기 Overlapped I/O)
    // 쓰기 작업은 writeMutex로 보호되어 동시에 하나의 쓰기 작업만 수행 가능
    int write(const char* buffer, int length) {
        std::lock_guard<std::mutex> lock(writeMutex);
        
        if (hComm == INVALID_HANDLE_VALUE) return -1;

        DWORD bytesWritten = 0;
        
        // OVERLAPPED 구조체 초기화 및 이벤트 리셋
        ZeroMemory(&writeOverlapped, sizeof(OVERLAPPED));
        writeOverlapped.hEvent = writeEvent;
        ResetEvent(writeOverlapped.hEvent);
        
        // 비동기 쓰기 작업 시작
        BOOL result = WriteFile(hComm, buffer, length, &bytesWritten, &writeOverlapped);
        
        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 비동기 작업이 진행 중이므로 완료 대기
                DWORD timeout = calculateTimeout(length);
                DWORD waitResult = WaitForSingleObject(writeOverlapped.hEvent, timeout);
                
                if (waitResult == WAIT_OBJECT_0) {
                    // 쓰기 작업 완료, 결과 확인
                    if (GetOverlappedResult(hComm, &writeOverlapped, &bytesWritten, FALSE)) {
                        return bytesWritten;
                    }
                } else if (waitResult == WAIT_TIMEOUT) {
                    // 타임아웃 발생, 작업 취소
                    logMessage("Error: Write timeout (" + std::to_string(timeout) + "ms)");
                    CancelIo(hComm);
                    return -1;
                }
            }
            logMessage("Error writing to serial port: " + std::to_string(error));
            return -1;
        }
        
        // 동기적으로 즉시 완료된 경우
        return bytesWritten;
    }
    
    // 데이터 읽기 (비동기 Overlapped I/O)
    // 읽기 작업은 readMutex로 보호되어 동시에 하나의 읽기 작업만 수행 가능
    // 요청한 길이만큼 읽거나 타임아웃이 발생할 때까지 반복 읽기 수행
    int read(char* buffer, int length, DWORD timeoutMs = 0) {
        std::lock_guard<std::mutex> lock(readMutex);
        
        if (hComm == INVALID_HANDLE_VALUE) return -1;

        DWORD totalBytesRead = 0;
        
        // 타임아웃이 지정되지 않은 경우 데이터 크기 기반으로 자동 계산
        if (timeoutMs == 0) {
            timeoutMs = calculateTimeout(length);
        }
        
        // 요청한 길이만큼 읽을 때까지 반복
        while (totalBytesRead < length) {
            DWORD bytesReadInThisCall = 0;
            
            // OVERLAPPED 구조체 초기화 및 이벤트 리셋
            ZeroMemory(&readOverlapped, sizeof(OVERLAPPED));
            readOverlapped.hEvent = readEvent;
            ResetEvent(readOverlapped.hEvent);
            
            // 비동기 읽기 작업 시작 (남은 길이만큼 읽기)
            BOOL result = ReadFile(hComm, 
                                  buffer + totalBytesRead, 
                                  length - totalBytesRead, 
                                  &bytesReadInThisCall, 
                                  &readOverlapped);
            
            if (!result) {
                DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING) {
                    // 비동기 작업이 진행 중이므로 완료 대기
                    DWORD waitResult = WaitForSingleObject(readOverlapped.hEvent, timeoutMs);
                    
                    if (waitResult == WAIT_OBJECT_0) {
                        // 읽기 작업 완료, 결과 확인
                        if (GetOverlappedResult(hComm, &readOverlapped, &bytesReadInThisCall, FALSE)) {
                            if (bytesReadInThisCall > 0) {
                                totalBytesRead += bytesReadInThisCall;
                            } else {
                                // 더 이상 읽을 데이터가 없음
                                break;
                            }
                        } else {
                            return -1;
                        }
                    } else if (waitResult == WAIT_TIMEOUT) {
                        // 타임아웃 발생
                        if (totalBytesRead > 0) {
                            // 일부라도 읽었으면 반환
                            break;
                        }
                        // 아무것도 읽지 못했으면 작업 취소하고 에러 반환
                        CancelIo(hComm);
                        return -1;
                    }
                } else {
                    return -1;
                }
            } else {
                // 동기적으로 즉시 완료된 경우
                if (bytesReadInThisCall > 0) {
                    totalBytesRead += bytesReadInThisCall;
                } else {
                    // 더 이상 읽을 데이터가 없음
                    break;
                }
            }
        }
        
        return totalBytesRead;
    }
    
    // 쓰기 버퍼 플러시
    // 시리얼 포트의 쓰기 버퍼에 남아있는 모든 데이터를 즉시 전송
    // Phase 3 결과 교환 시 동기화를 위해 사용
    bool flush() {
        std::lock_guard<std::mutex> lock(writeMutex);
        if (hComm == INVALID_HANDLE_VALUE) return false;
        return FlushFileBuffers(hComm) != 0;
    }
    
    // 현재 설정된 보레이트 반환
    int getBaudRate() const { return baudRate; }

private:
    HANDLE hComm;                    // 시리얼 포트 핸들
    OVERLAPPED readOverlapped;       // 읽기 작업용 OVERLAPPED 구조체
    OVERLAPPED writeOverlapped;      // 쓰기 작업용 OVERLAPPED 구조체
    HANDLE readEvent;                // 읽기 작업 완료 이벤트
    HANDLE writeEvent;               // 쓰기 작업 완료 이벤트
    std::mutex readMutex;            // 읽기 작업 동기화용 뮤텍스
    std::mutex writeMutex;            // 쓰기 작업 동기화용 뮤텍스
    int baudRate;                    // 현재 보레이트 설정
    
    // 데이터 크기 기반 타임아웃 계산
    // 전송 시간의 2.5배 + 기본 타임아웃(500ms)을 사용하여 안전한 타임아웃 값 계산
    // 최소 200ms, 최대 60초로 제한
    DWORD calculateTimeout(int dataSize) const {
        if (baudRate == 0) return 5000;
        
        // 전송 시간 계산: (데이터 크기 * 10비트/바이트) / 보레이트 * 1000ms * 안전 계수
        // 10비트 = 8비트 데이터 + 1 스타트 비트 + 1 스톱 비트
        double transmitTime = (static_cast<double>(dataSize) * 10.0 / baudRate) * 1000.0 * TIMEOUT_SAFETY_FACTOR;
        DWORD timeout = static_cast<DWORD>(transmitTime) + BASE_TIMEOUT_MS;
        
        if (timeout < 200) timeout = 200;    // 최소 타임아웃: 200ms
        // 대용량 프레임의 경우 더 긴 타임아웃 허용 (최대 60초)
        if (timeout > 60000) timeout = 60000;  // 최대 타임아웃: 60초 (매우 큰 데이터용)
        
        return timeout;
    }
};

// ==========================================================
// WindowManager 클래스: 슬라이딩 윈도우 알고리즘 구현 및 동적 크기 조절
// ==========================================================
// Selective Repeat ARQ 프로토콜의 슬라이딩 윈도우를 관리하는 클래스
// Thread-safe 구현으로 멀티스레드 환경에서 안전하게 사용 가능
class WindowManager {
public:
    // 생성자: 초기 윈도우 크기와 베이스 시퀀스 번호 초기화
    WindowManager(int totalFrames) 
        : baseSeq(0),                    // 윈도우의 시작 프레임 번호
          windowSize(WINDOW_SIZE_INIT),  // 초기 윈도우 크기 (16 프레임)
          totalFrames(totalFrames),       // 전체 프레임 개수
          consecutiveSuccesses(0),       // 연속 성공 횟수 (윈도우 확장용)
          consecutiveFailures(0) {        // 연속 실패 횟수 (윈도우 축소용)
    }
    
    // 현재 윈도우의 베이스 시퀀스 번호 반환
    int getBase() const {
        std::lock_guard<std::mutex> lock(windowMutex);
        return baseSeq;
    }
    
    // 현재 윈도우 크기 반환
    int getWindowSize() const {
        std::lock_guard<std::mutex> lock(windowMutex);
        return windowSize;
    }
    
    // 특정 프레임 번호가 현재 윈도우 범위 내에 있는지 확인
    bool isInWindow(int frameNum) const {
        std::lock_guard<std::mutex> lock(windowMutex);
        return (frameNum >= baseSeq && frameNum < baseSeq + windowSize);
    }
    
    // 특정 프레임 번호에 대해 ACK 상태로 표시
    void markAcked(int frameNum) {
        std::lock_guard<std::mutex> lock(windowMutex);
        ackedFrames[frameNum] = true;
    }
    
    // 특정 프레임 번호가 ACK되었는지 확인
    bool isAcked(int frameNum) const {
        std::lock_guard<std::mutex> lock(windowMutex);
        auto it = ackedFrames.find(frameNum);
        return (it != ackedFrames.end() && it->second);
    }
    
    // 윈도우 슬라이드: 연속된 ACK된 프레임만큼 윈도우를 앞으로 이동
    // 반환값: 슬라이드된 프레임 개수
    int slideWindow() {
        std::lock_guard<std::mutex> lock(windowMutex);
        int slidCount = 0;
        
        // 베이스 시퀀스부터 연속으로 ACK된 프레임만큼 윈도우 이동
        while (baseSeq < totalFrames && ackedFrames[baseSeq]) {
            ackedFrames.erase(baseSeq);
            baseSeq++;
            slidCount++;
        }
        
        return slidCount;
    }
    
    // 모든 프레임 전송 완료 여부 확인 (베이스 시퀀스가 전체 프레임 수를 초과)
    bool isComplete() const {
        std::lock_guard<std::mutex> lock(windowMutex);
        return baseSeq >= totalFrames;
    }
    
    // 동적 윈도우 크기 조절
    // success: 전송 성공 여부
    // rtt: Round Trip Time (왕복 시간, 밀리초)
    void adjustWindow(bool success, double rtt) {
        std::lock_guard<std::mutex> lock(windowMutex);
        
        if (success) {
            consecutiveSuccesses++;
            consecutiveFailures = 0;
            
            // 성공 시 윈도우 크기 확장 (더 공격적인 증가)
            // 연속 3회 성공 시 윈도우 크기를 2배로 증가 (기존 5회->3회, 1.5배->2배로 변경)
            if (consecutiveSuccesses >= 3) {
                int newSize = windowSize * 2;  // 더 공격적: 1.5배 대신 2배
                if (newSize > WINDOW_SIZE_MAX) newSize = WINDOW_SIZE_MAX;
                
                if (newSize != windowSize) {
                    LOG_DEBUG("Window size increased: " + std::to_string(windowSize) + 
                             " -> " + std::to_string(newSize));
                    windowSize = newSize;
                }
                consecutiveSuccesses = 0;
            }
            
            // RTT 기반 윈도우 축소: RTT가 너무 높으면 윈도우 크기 감소 (완화된 임계값)
            // RTT > 2000ms: 네트워크 지연이 크므로 윈도우 축소 (기존 1000ms -> 2000ms로 완화)
            if (rtt > 2000.0) {
                int newSize = std::max(windowSize / 2, WINDOW_SIZE_MIN);
                if (newSize != windowSize) {
                    LOG_DEBUG("Window size decreased due to high RTT (" + 
                             std::to_string(rtt) + "ms): " + std::to_string(windowSize) + 
                             " -> " + std::to_string(newSize));
                    windowSize = newSize;
                }
                consecutiveSuccesses = 0;
            }
            
        } else {
            // 실패 시 윈도우 크기 축소 (Multiplicative Decrease)
            consecutiveFailures++;
            consecutiveSuccesses = 0;
            
            // 연속 3회 실패 시 윈도우 크기를 절반으로 감소 (기존 2회->3회로 완화하여 덜 공격적)
            if (consecutiveFailures >= 3) {
                int newSize = std::max(windowSize / 2, WINDOW_SIZE_MIN);
                if (newSize != windowSize) {
                    LOG_DEBUG("Window size decreased due to failures: " + 
                             std::to_string(windowSize) + " -> " + std::to_string(newSize));
                    windowSize = newSize;
                }
                consecutiveFailures = 0;
            }
        }
    }
    
    // 전송할 프레임 목록 반환 (윈도우 내에서 ACK되지 않은 프레임들)
    std::vector<int> getFramesToSend() const {
        std::lock_guard<std::mutex> lock(windowMutex);
        std::vector<int> frames;
        
        // 윈도우 범위 내에서 ACK되지 않은 프레임만 추가
        for (int i = baseSeq; i < baseSeq + windowSize && i < totalFrames; ++i) {
            auto it = ackedFrames.find(i);
            if (it == ackedFrames.end() || !it->second) {
                frames.push_back(i);
            }
        }
        
        return frames;
    }

private:
    mutable std::mutex windowMutex;   // 윈도우 상태 접근 동기화용 뮤텍스
    int baseSeq;                      // 윈도우의 시작 프레임 번호
    int windowSize;                   // 현재 윈도우 크기 (프레임 개수)
    int totalFrames;                  // 전체 프레임 개수
    std::map<int, bool> ackedFrames;  // 프레임별 ACK 상태 맵
    int consecutiveSuccesses;         // 연속 성공 횟수 (윈도우 확장용)
    int consecutiveFailures;          // 연속 실패 횟수 (윈도우 축소용)
};

// ==========================================================
// ????????? ?? ????????? ?????? ????????? ???
// ==========================================================
template<typename T>
class SafeQueue {
public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        cv_.notify_one();
    }
    
    bool pop(T& item, int timeoutMs = -1) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (timeoutMs < 0) {
            cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });
        } else {
            if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this] { return !queue_.empty() || stopped_; })) {
                return false;  // Timeout
            }
        }
        
        if (stopped_ && queue_.empty()) {
            return false;
        }
        
        item = queue_.front();
        queue_.pop();
        return true;
    }
    
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
    bool stopped_ = false;
};

// ==========================================================
// ACK Batcher: Accumulates ACKs and sends them in batches
// ==========================================================
class AckBatcher {
public:
    AckBatcher(int batchSize = 5, int flushIntervalMs = 10) 
        : batchSize_(batchSize), flushIntervalMs_(flushIntervalMs), pendingCount_(0) {
        lastFlushTime_ = std::chrono::steady_clock::now();
    }
    
    // Add an ACK to the batch
    void addAck(int frameNum, AckFrame& ackFrame) {
        ackFrame.setAck(frameNum);
        pendingCount_++;
    }
    
    // Check if we should flush the batch
    bool shouldFlush() const {
        if (pendingCount_ >= batchSize_) return true;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlushTime_).count();
        return (elapsed >= flushIntervalMs_ && pendingCount_ > 0);
    }
    
    // Flush the ACKs
    void flush(SerialPort& serial, AckFrame& ackFrame, std::vector<char>& ackSendBuffer) {
        if (pendingCount_ > 0) {
            ackFrame.serialize(ackSendBuffer);
            serial.write(ackSendBuffer.data(), ackSendBuffer.size());
            
            // CRITICAL: Reset bitmap after flush to avoid sending stale ACKs
            ackFrame.bitmap = 0;
            
            pendingCount_ = 0;
            lastFlushTime_ = std::chrono::steady_clock::now();
        }
    }
    
    int getPendingCount() const { return pendingCount_; }

private:
    int batchSize_;
    int flushIntervalMs_;
    int pendingCount_;
    std::chrono::steady_clock::time_point lastFlushTime_;
};

// ==========================================================
// TransmissionManager: 멀티스레드 기반 송신자 및 수신자 관리
// ==========================================================
// Sender Thread와 Receiver Thread를 분리하여 동시에 송수신 수행
// 윈도우 관리자와 협력하여 Selective Repeat ARQ 프로토콜 구현
class TransmissionManager {
public:
    // 생성자: 시리얼 포트, 윈도우 관리자, 프레임 벡터, 재전송 카운터 참조 저장
    TransmissionManager(SerialPort& serial, WindowManager& windowMgr, 
                       std::vector<DataFrame>& frames, int& retransmitCount)
        : serial_(serial), windowMgr_(windowMgr), frames_(frames), 
          retransmitCount_(retransmitCount), stopped_(false) {}
    
    // 송신자 및 수신자 스레드 시작
    void start() {
        stopped_ = false;
        senderThread_ = std::thread(&TransmissionManager::senderThreadFunc, this);
        receiverThread_ = std::thread(&TransmissionManager::receiverThreadFunc, this);
    }
    
    // 송신자 및 수신자 스레드 종료 대기
    void stop() {
        stopped_ = true;
        if (senderThread_.joinable()) senderThread_.join();
        if (receiverThread_.joinable()) receiverThread_.join();
    }
    
    // 스레드 종료 여부 확인
    bool isStopped() const { return stopped_.load(); }

private:
    // 송신자 스레드 함수: 윈도우 내 미전송/미확인 프레임을 버스트 전송
    void senderThreadFunc() {
        std::vector<char> sendBuffer;
        std::vector<char> burstBuffer;
        
        // 프레임 크기에 따른 버스트 전송 전략 결정
        int frameSize = frames_[0].payload.size() + FRAME_OVERHEAD_V3;
        int maxBurstFrames = 16;  // 기본값
        
        // 대용량 프레임의 경우 버스트 크기를 제한하여 타임아웃 방지
        if (frameSize > 50000) {
            maxBurstFrames = 1;  // 매우 큰 프레임은 한 번에 하나씩 전송
            logMessage("Large frame detected (" + std::to_string(frameSize) + 
                      " bytes). Using single-frame transmission.");
        } else if (frameSize > 10000) {
            maxBurstFrames = 4;  // 큰 프레임은 최대 4개까지 버스트 전송
        } else if (frameSize > 1000) {
            maxBurstFrames = 8;  // 중간 크기 프레임은 최대 8개까지 버스트 전송
        }
        
        // 모든 프레임 전송 완료 또는 중지 신호까지 반복
        while (!stopped_ && !windowMgr_.isComplete()) {
            std::vector<int> framesToSend = windowMgr_.getFramesToSend();
            
            if (!framesToSend.empty()) {
                // 대용량 프레임을 위한 버스트 크기 제한
                int burstSize = std::min(static_cast<int>(framesToSend.size()), maxBurstFrames);
                
                // 버스트 전송: 여러 프레임을 하나의 버퍼에 모아 한 번에 전송
                burstBuffer.clear();
                int estimatedSize = burstSize * frameSize;
                burstBuffer.reserve(estimatedSize);
                
                // 전송할 프레임들을 직렬화하여 버스트 버퍼에 추가
                for (int i = 0; i < burstSize; ++i) {
                    int frameNum = framesToSend[i];
                    frames_[frameNum].windowSize = windowMgr_.getWindowSize();
                    frames_[frameNum].serialize(sendBuffer);
                    burstBuffer.insert(burstBuffer.end(), sendBuffer.begin(), sendBuffer.end());
                }
                
                // 버스트 전송 실행
                if (serial_.write(burstBuffer.data(), burstBuffer.size()) != burstBuffer.size()) {
                    LOG_DEBUG("Error sending burst of " + std::to_string(burstSize) + " frames");
                    retransmitCount_ += burstSize;
                    windowMgr_.adjustWindow(false, 0);  // 실패 시 윈도우 크기 축소
                } else {
                    LOG_DEBUG("Sent burst of " + std::to_string(burstSize) + " frames");
                }
                
                // 수신자 과부하 방지를 위한 짧은 지연
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } else {
                // 전송할 프레임이 없으면 대기 (10ms는 수신자가 ACK를 처리할 시간 제공)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
    // 수신자 스레드 함수: ACK 프레임을 수신하여 윈도우 상태 업데이트
    void receiverThreadFunc() {
        std::vector<char> ackBuffer(ACK_FRAME_SIZE);
        
        // 모든 프레임 전송 완료 또는 중지 신호까지 반복
        while (!stopped_ && !windowMgr_.isComplete()) {
            // ACK 프레임 수신 (100ms 타임아웃)
            int received = serial_.read(ackBuffer.data(), ACK_FRAME_SIZE, 100);
            
            if (received == ACK_FRAME_SIZE) {
                AckFrame ackFrame;
                if (ackFrame.deserialize(ackBuffer.data(), ACK_FRAME_SIZE)) {
                    int ackedCount = 0;
                    int totalFrames = frames_.size();
                    
                    // 비트맵에서 ACK된 프레임 확인 (최대 32개)
                    for (int i = 0; i < 32; ++i) {
                        int frameNum = ackFrame.baseFrameNum + i;
                        if (frameNum >= totalFrames) break;
                        
                        // ACK되었고 아직 윈도우 관리자에 기록되지 않은 경우
                        if (ackFrame.isAcked(frameNum) && !windowMgr_.isAcked(frameNum)) {
                            windowMgr_.markAcked(frameNum);
                            ackedCount++;
                        }
                    }
                    
                    // 새로운 ACK가 있으면 윈도우 크기 조절 및 슬라이드
                    if (ackedCount > 0) {
                        windowMgr_.adjustWindow(true, 100);  // 성공, RTT 100ms로 가정
                        windowMgr_.slideWindow();            // 윈도우 슬라이드
                    }
                }
            }
        }
    }
    
    SerialPort& serial_;              // 시리얼 포트 참조
    WindowManager& windowMgr_;        // 윈도우 관리자 참조
    std::vector<DataFrame>& frames_;  // 프레임 벡터 참조
    int& retransmitCount_;            // 재전송 카운터 참조
    std::atomic<bool> stopped_;       // 스레드 중지 플래그
    std::thread senderThread_;         // 송신자 스레드
    std::thread receiverThread_;       // 수신자 스레드
};

// ==========================================================
// READY ACK 동기화 함수 (Phase 3 결과 교환용)
// ==========================================================
// Phase 3에서 결과 교환 전 상대방과 동기화하기 위한 3-way handshake에 사용

// READY ACK 전송
// 상대방에게 결과 교환 준비 완료 신호 전송
bool sendReadyAck(SerialPort& serial) {
    int sent = serial.write(READY_ACK, READY_ACK_LEN);
    if (sent == READY_ACK_LEN) {
        logMessage("READY ACK sent.");
        return true;
    }
    logMessage("Error: Failed to send READY ACK.");
    return false;
}

// READY ACK 수신 대기
// 상대방으로부터 READY ACK를 수신할 때까지 대기 (최대 30초)
bool waitForReadyAck(SerialPort& serial) {
    logMessage("Waiting for READY ACK...");
    
    char ackBuffer[10];
    int attempts = 0;
    const int MAX_ATTEMPTS = 300;  // 300회 * 100ms = 30초
    
    while (attempts < MAX_ATTEMPTS) {
        int received = serial.read(ackBuffer, READY_ACK_LEN, 100);
        
        if (received == READY_ACK_LEN) {
            // READY ACK 프레임 검증: [SOF_ACK][R][E][A][D][Y][EOF]
            if (ackBuffer[0] == SOF_ACK && 
                ackBuffer[1] == 'R' && 
                ackBuffer[2] == 'E' && 
                ackBuffer[3] == 'A' && 
                ackBuffer[4] == 'D' && 
                ackBuffer[5] == 'Y' && 
                ackBuffer[6] == EOF_BYTE) {
                logMessage("READY ACK received.");
                return true;
            }
        }
        
        attempts++;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    logMessage("Error: Timeout waiting for READY ACK (30 seconds).");
    return false;
}

// ==========================================================
// 안전한 결과 읽기 (재시도 로직 포함)
// ==========================================================
// Phase 3에서 결과를 안정적으로 수신하기 위한 헬퍼 함수
// 부분 읽기나 타임아웃 발생 시 자동으로 재시도
bool readResults(SerialPort& serial, Results& results, const std::string& source, int maxRetries = 3) {
    const size_t RESULTS_SIZE = sizeof(Results);
    
    // 최대 재시도 횟수만큼 반복 시도
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        logMessage("Attempting to read results from " + source + " (attempt " + std::to_string(attempt) + "/" + std::to_string(maxRetries) + ")...");
        
        // 15초 타임아웃으로 결과 읽기 시도
        int bytesRead = serial.read(reinterpret_cast<char*>(&results), RESULTS_SIZE, 15000);
        
        if (bytesRead == RESULTS_SIZE) {
            // 전체 결과를 성공적으로 읽음
            logMessage("Results successfully received from " + source + " (" + std::to_string(bytesRead) + " bytes).");
            return true;
        } else if (bytesRead > 0) {
            // 부분 읽기 발생: 재시도
            logMessage("Warning: Partial read from " + source + " (" + std::to_string(bytesRead) + "/" + std::to_string(RESULTS_SIZE) + " bytes). Retrying...");
        } else {
            // 타임아웃 또는 에러 발생: 재시도
            logMessage("Warning: Read timeout or error from " + source + " (attempt " + std::to_string(attempt) + "). Retrying...");
        }
        
        // 마지막 시도가 아니면 500ms 대기 후 재시도
        if (attempt < maxRetries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    logMessage("Error: Failed to receive results from " + source + " after " + std::to_string(maxRetries) + " attempts.");
    return false;
}

// 함수 선언
void clientMode(const std::string& comport, int baudrate, int datasize, int num);
void serverMode(const std::string& comport, int baudrate);

// ==========================================================
// Main
// ==========================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: program.exe <mode> [options]" << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  client <comport> <baudrate> <datasize> <num>" << std::endl;
        std::cerr << "  server <comport> <baudrate>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    
    std::string comport = "";
    if (mode == "client" && argc >= 3) {
        comport = argv[2];
    } else if (mode == "server" && argc >= 3) {
        comport = argv[2];
    }
    
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

// ==========================================================
// Client Mode: Selective Repeat ARQ 프로토콜 구현
// ==========================================================
// Phase 1: 클라이언트 → 서버 데이터 전송
// Phase 2: 서버 → 클라이언트 데이터 수신
// Phase 3: 결과 교환 및 리포트 출력
void clientMode(const std::string& comport, int baudrate, int datasize, int num) {
    logMessage("--- Client Mode (Protocol V" + std::to_string(PROTOCOL_VERSION) + ") ---");
    logMessage("Configuration: datasize=" + std::to_string(datasize) + 
               " bytes, frames=" + std::to_string(num) + 
               ", window=" + std::to_string(WINDOW_SIZE_INIT) + "-" + std::to_string(WINDOW_SIZE_MAX));
    
    // 대용량 프레임 감지 시 디버그 모드 자동 활성화
    if (datasize > 10000) {
        debugMode = true;
        logMessage("Large frame size detected (" + std::to_string(datasize) + 
                   " bytes). Enabling detailed logging.");
    }
    
    // 시리얼 포트 열기
    SerialPort serial;
    if (!serial.open(comport, baudrate)) {
        return;
    }
    logMessage("Port " + comport + " opened successfully at " + std::to_string(baudrate) + " bps.");

    // 포트 안정화 대기 (외부 루프백 연결 시 필요)
    logMessage("Waiting for port stabilization...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Phase 0: 서버에 설정 정보 전송
    Settings settings = {PROTOCOL_VERSION, datasize, num, 0};
    logMessage("Connecting to server...");
    logMessage("Sending settings to server...");
    if (serial.write(reinterpret_cast<char*>(&settings), sizeof(Settings)) != sizeof(Settings)) {
        logMessage("Error: Failed to send settings to server.");
        return;
    }
    logMessage("Settings sent: protocol=" + std::to_string(PROTOCOL_VERSION) + 
               ", datasize=" + std::to_string(datasize) + ", num=" + std::to_string(num));

    // 케이블을 통한 설정 전송 완료를 보장하기 위한 짧은 지연
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 서버로부터 ACK 수신 대기
    logMessage("Waiting for server acknowledgment...");
    logMessage("Waiting for ACK from server (timeout: 10 seconds)...");
    char ack[4];
    int bytesRead = serial.read(ack, 3, 10000);
    if (bytesRead != 3) {
        logMessage("Error: Did not receive full ACK from server. Received " + 
                   std::to_string(bytesRead) + " bytes. (Timeout: 10 seconds)");
        logMessage("Possible causes:");
        logMessage("  1. Server not started or wrong COM port");
        logMessage("  2. Protocol version mismatch");
        logMessage("  3. Baud rate mismatch");
        return;
    }
    ack[3] = '\0';
    if (std::string(ack) != "ACK") {
        logMessage("Error: Invalid response from server.");
        return;
    }
    logMessage("ACK received from server.");
    
    const int frameSize = datasize + FRAME_OVERHEAD_V3;
    Results clientResults = {0, 0, 0, 0, 0.0, 0.0, 0.0};
    
    auto startTime = std::chrono::high_resolution_clock::now();

    // ===== Phase 1: 클라이언트 → 서버 데이터 전송 (Multi-threaded Transmission) =====
    // 멀티스레드 기반 Selective Repeat ARQ로 데이터 전송
    logMessage("Phase 1: Client transmitting with Multi-threaded Selective Repeat ARQ...");
    {
        WindowManager windowMgr(num);
        
        // ????????? ????????? ????
        std::vector<DataFrame> frames(num);
        for (int i = 0; i < num; ++i) {
            frames[i].frameNum = i;
            frames[i].windowSize = WINDOW_SIZE_INIT;
            frames[i].payload.resize(datasize);
            
            // ??????占싸듸옙 ?????? (0-255 占쌥븝옙)
            for (int j = 0; j < datasize; ++j) {
                frames[i].payload[j] = static_cast<char>(j % 256);
            }
            
            frames[i].checksum = frames[i].calculateChecksum();
        }
        
        // Start multi-threaded transmission
        TransmissionManager transmissionMgr(serial, windowMgr, frames, clientResults.retransmitCount);
        transmissionMgr.start();
        
        // Monitor progress
        int lastBase = 0;
        while (!windowMgr.isComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            int currentBase = windowMgr.getBase();
            if (currentBase != lastBase) {
                // Improved logging: show progress for small tests and milestones
                if (currentBase % 100 == 0 || currentBase <= 10 || 
                    currentBase == num || num <= 20) {
                    logMessage("Progress: " + std::to_string(currentBase) + "/" + 
                              std::to_string(num) + " frames acknowledged, window: " + 
                              std::to_string(windowMgr.getWindowSize()));
                }
                lastBase = currentBase;
            }
        }
        
        transmissionMgr.stop();
        logMessage("Phase 1 complete: All frames transmitted and acknowledged.");
    }

    // ===== Phase 2: 서버 → 클라이언트 데이터 수신 (즉시 ACK 전송 방식) =====
    // 프레임 수신 즉시 ACK를 전송하여 재전송 최소화
    logMessage("Phase 2: Client receiving with Selective Repeat ARQ and Immediate ACK...");
    {
        std::map<int, DataFrame> receivedFrames;  // 순서 무관 수신 프레임 버퍼
        AckFrame ackFrame;
        ackFrame.baseFrameNum = 0;
        ackFrame.bitmap = 0;
        
        std::vector<char> receiveBuffer(frameSize);
        std::vector<char> ackSendBuffer;
        
        int nextExpectedFrame = 0;
        
        // 모든 프레임 수신 완료까지 반복
        while (nextExpectedFrame < num) {
            int received = serial.read(receiveBuffer.data(), frameSize, 3000);
            
            if (received == frameSize) {
                DataFrame frame;
                if (frame.deserialize(receiveBuffer.data(), frameSize)) {
                    // SOF/EOF 검증 완료 (즉시 ACK 전송)
                    if (receiveBuffer[0] == SOF && receiveBuffer[frameSize - 1] == EOF_BYTE) {
                        // 프레임 수신 즉시 ACK 전송 (검증 전에 전송하여 재전송 최소화)
                        // 수신한 프레임 번호를 base로 사용하여 ACK가 올바르게 전송되도록 보장
                        ackFrame.baseFrameNum = frame.frameNum;
                        ackFrame.bitmap = 0;
                        ackFrame.setAck(frame.frameNum);
                        ackFrame.serialize(ackSendBuffer);
                        serial.write(ackSendBuffer.data(), ackSendBuffer.size());
                        
                        // 중복 프레임 확인
                        if (receivedFrames.find(frame.frameNum) != receivedFrames.end()) {
                            LOG_DEBUG("Duplicate frame " + std::to_string(frame.frameNum) + " received");
                            continue;
                        }
                        
                        // 체크섬 검증 (Out-of-order 수신을 허용하므로, 체크섬 검증 후에도 프레임을 버퍼에 저장)
                        if (frame.verifyChecksum()) {
                            // 페이로드 내용 검증
                            bool payloadOk = true;
                            for (size_t j = 0; j < frame.payload.size(); ++j) {
                                if (frame.payload[j] != static_cast<char>(255 - (j % 256))) {
                                    payloadOk = false;
                                    break;
                                }
                            }
                            
                            if (payloadOk) {
                                receivedFrames[frame.frameNum] = frame;
                                clientResults.totalReceivedBytes += received;
                                
                                // ????????? ??????????????? 처占쏙옙????? ?????? 占쏙옙??? ????????? ????????????
                                while (receivedFrames.find(nextExpectedFrame) != receivedFrames.end()) {
                                    clientResults.receivedNum++;
                                    nextExpectedFrame++;
                                    
                                    if (nextExpectedFrame % 100 == 0 || nextExpectedFrame <= 10) {
                                        logMessage("Progress: " + std::to_string(nextExpectedFrame) + 
                                                  "/" + std::to_string(num) + " frames received and validated");
                                    }
                                }
                                // ACK already sent before validation
                            } else {
                                clientResults.errorCount++;
                                logMessage("Frame " + std::to_string(frame.frameNum) + " payload validation failed");
                            }
                        } else {
                            clientResults.errorCount++;
                            logMessage("Frame " + std::to_string(frame.frameNum) + " checksum validation failed");
                        }
                    }
                } else {
                    clientResults.errorCount++;
                    logMessage("Frame deserialization failed");
                }
            } else {
                // Log timeout for debugging
                if (received == 0 && nextExpectedFrame < num) {
                    LOG_DEBUG("Read timeout at frame " + std::to_string(nextExpectedFrame));
                }
            }
        }
        
        logMessage("Phase 2 complete: All frames received and validated.");
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    clientResults.elapsedSeconds = elapsed.count();
    
    if (clientResults.elapsedSeconds > 0) {
        clientResults.throughputMBps = (clientResults.totalReceivedBytes / (1024.0 * 1024.0)) / clientResults.elapsedSeconds;
        clientResults.charactersPerSecond = clientResults.totalReceivedBytes / clientResults.elapsedSeconds;
    }

    logMessage("Data exchange complete.");
    logMessage("Performance: " + std::to_string(clientResults.throughputMBps) + " MB/s, " + 
               std::to_string(clientResults.charactersPerSecond) + " chars/s (CPS)");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // ===== Phase 3: 결과 교환 (3-way Handshake) =====
    // 3-way handshake를 통한 명확한 동기화 후 결과 교환
    if (!sendReadyAck(serial)) {
        logMessage("Error: Failed to synchronize with server.");
        return;
    }
    
    if (!waitForReadyAck(serial)) {
        logMessage("Error: Server not ready for result exchange.");
        return;
    }
    
    logMessage("Synchronization complete. Starting result exchange.");
    // 서버의 READY ACK 수신 후 즉시 결과 전송 (3-way handshake 완료)
    if (serial.write(reinterpret_cast<char*>(&clientResults), sizeof(Results)) != sizeof(Results)) {
        logMessage("Error: Failed to send results to server.");
    } else {
        logMessage("Client results sent to server.");
        // 데이터 전송 보장을 위한 버퍼 플러시
        if (!serial.flush()) {
            logMessage("Warning: Failed to flush serial port buffers.");
        }
    }

    // 서버로부터 결과 수신 (재시도 로직 포함)
    Results serverResults;
    if (!readResults(serial, serverResults, "server", 3)) {
        logMessage("Error: Failed to receive results from server.");
        return;
    }
    
    logMessage("Results received from server.");
        
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
}

// ==========================================================
// Server Mode with Selective Repeat ARQ
// ==========================================================
// ==========================================================
// Server Mode: Selective Repeat ARQ 프로토콜 구현
// ==========================================================
// Phase 1: 클라이언트 → 서버 데이터 수신
// Phase 2: 서버 → 클라이언트 데이터 전송
// Phase 3: 결과 교환 및 리포트 출력
void serverMode(const std::string& comport, int baudrate) {
    logMessage("--- Server Mode (Protocol V" + std::to_string(PROTOCOL_VERSION) + ") ---");
    SerialPort serial;
    if (!serial.open(comport, baudrate)) {
        return;
    }
    logMessage("Server waiting for a client on " + comport + "...");
    logMessage("Please start the client within 60 seconds.");

    // 클라이언트로부터 설정 정보 수신 대기
    Settings settings;
    logMessage("Waiting for client settings (timeout: 60 seconds)...");
    if (serial.read(reinterpret_cast<char*>(&settings), sizeof(Settings), 60000) != sizeof(Settings)) {
        logMessage("Error: Failed to receive settings from client. Connection timed out (60 seconds).");
        logMessage("Possible causes:");
        logMessage("  1. Client not started or wrong COM port");
        logMessage("  2. Baud rate mismatch");
        logMessage("  3. Connection cable issue");
        return;
    }
    
    // 프로토콜 버전 확인
    if (settings.protocolVersion != PROTOCOL_VERSION) {
        logMessage("Error: Protocol version mismatch! Client: " + std::to_string(settings.protocolVersion) + 
                   ", Server: " + std::to_string(PROTOCOL_VERSION));
        return;
    }
    
    logMessage("Client connected. Settings: protocol=" + std::to_string(settings.protocolVersion) + 
               ", datasize=" + std::to_string(settings.datasize) + ", num=" + std::to_string(settings.num));

    // 클라이언트에 ACK 전송
    if (serial.write("ACK", 3) != 3) {
        logMessage("Error: Failed to send ACK to client.");
        return;
    }
    logMessage("ACK sent to client.");

    const int datasize = settings.datasize;
    const int num = settings.num;
    const int frameSize = datasize + FRAME_OVERHEAD_V3;
    Results serverResults = {0, 0, 0, 0, 0.0, 0.0, 0.0};
    
    auto startTime = std::chrono::high_resolution_clock::now();

    // ===== Phase 1: 클라이언트 → 서버 데이터 수신 (즉시 ACK 전송 방식) =====
    // 프레임 수신 즉시 ACK를 전송하여 재전송 최소화
    logMessage("Phase 1: Server receiving with Selective Repeat ARQ and Immediate ACK...");
    {
        std::map<int, DataFrame> receivedFrames;  // 순서 무관 수신 프레임 버퍼
        AckFrame ackFrame;
        ackFrame.baseFrameNum = 0;
        ackFrame.bitmap = 0;
        
        std::vector<char> receiveBuffer(frameSize);
        std::vector<char> ackSendBuffer;
        
        int nextExpectedFrame = 0;
        
        // 모든 프레임 수신 완료까지 반복
        while (nextExpectedFrame < num) {
            int received = serial.read(receiveBuffer.data(), frameSize, 3000);
            
            if (received == frameSize) {
                DataFrame frame;
                if (frame.deserialize(receiveBuffer.data(), frameSize)) {
                    if (receiveBuffer[0] == SOF && receiveBuffer[frameSize - 1] == EOF_BYTE) {
                        // 프레임 수신 즉시 ACK 전송 (검증 전에 전송하여 재전송 최소화)
                        // 수신한 프레임 번호를 base로 사용하여 ACK가 올바르게 전송되도록 보장
                        ackFrame.baseFrameNum = frame.frameNum;
                        ackFrame.bitmap = 0;
                        ackFrame.setAck(frame.frameNum);
                        ackFrame.serialize(ackSendBuffer);
                        serial.write(ackSendBuffer.data(), ackSendBuffer.size());
                        
                        // Check for duplicate frame
                        if (receivedFrames.find(frame.frameNum) != receivedFrames.end()) {
                            LOG_DEBUG("Duplicate frame " + std::to_string(frame.frameNum) + " received");
                            continue;
                        }
                        
                        if (frame.verifyChecksum()) {
                            bool payloadOk = true;
                            for (size_t j = 0; j < frame.payload.size(); ++j) {
                                if (frame.payload[j] != static_cast<char>(j % 256)) {
                                    payloadOk = false;
                                    break;
                                }
                            }
                            
                            if (payloadOk) {
                                receivedFrames[frame.frameNum] = frame;
                                serverResults.totalReceivedBytes += received;
                                
                                while (receivedFrames.find(nextExpectedFrame) != receivedFrames.end()) {
                                    serverResults.receivedNum++;
                                    nextExpectedFrame++;
                                    
                                    if (nextExpectedFrame % 100 == 0 || nextExpectedFrame <= 10) {
                                        logMessage("Progress: " + std::to_string(nextExpectedFrame) + 
                                                  "/" + std::to_string(num) + " frames received and validated");
                                    }
                                }
                                // ACK already sent before validation
                            } else {
                                serverResults.errorCount++;
                                logMessage("Frame " + std::to_string(frame.frameNum) + " payload validation failed");
                            }
                        } else {
                            serverResults.errorCount++;
                            logMessage("Frame " + std::to_string(frame.frameNum) + " checksum validation failed");
                        }
                    }
                }
            } else {
                // Log timeout for debugging
                if (received == 0 && nextExpectedFrame < num) {
                    LOG_DEBUG("Read timeout at frame " + std::to_string(nextExpectedFrame));
                }
            }
        }
        
        logMessage("Phase 1 complete: All frames received and validated.");
    }

    // ===== Phase 2: 서버 → 클라이언트 데이터 전송 (Multi-threaded Transmission) =====
    // 멀티스레드 기반 Selective Repeat ARQ로 데이터 전송
    logMessage("Phase 2: Server transmitting with Multi-threaded Selective Repeat ARQ...");
    {
        WindowManager windowMgr(num);
        
        // 전송할 프레임들 준비
        std::vector<DataFrame> frames(num);
        for (int i = 0; i < num; ++i) {
            frames[i].frameNum = i;
            frames[i].windowSize = WINDOW_SIZE_INIT;
            frames[i].payload.resize(datasize);
            
            // 테스트 데이터 생성: 255, 254, 253, ... 패턴
            for (int j = 0; j < datasize; ++j) {
                frames[i].payload[j] = static_cast<char>(255 - (j % 256));
            }
            
            frames[i].checksum = frames[i].calculateChecksum();
        }
        
        // 멀티스레드 전송 시작
        TransmissionManager transmissionMgr(serial, windowMgr, frames, serverResults.retransmitCount);
        transmissionMgr.start();
        
        // Monitor progress
        int lastBase = 0;
        while (!windowMgr.isComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            int currentBase = windowMgr.getBase();
            if (currentBase != lastBase) {
                // Improved logging: show progress for small tests and milestones
                if (currentBase % 100 == 0 || currentBase <= 10 || 
                    currentBase == num || num <= 20) {
                    logMessage("Progress: " + std::to_string(currentBase) + "/" + 
                              std::to_string(num) + " frames acknowledged, window: " + 
                              std::to_string(windowMgr.getWindowSize()));
                }
                lastBase = currentBase;
            }
        }
        
        transmissionMgr.stop();
        logMessage("Phase 2 complete: All frames transmitted and acknowledged.");
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    serverResults.elapsedSeconds = elapsed.count();
    
    if (serverResults.elapsedSeconds > 0) {
        serverResults.throughputMBps = (serverResults.totalReceivedBytes / (1024.0 * 1024.0)) / serverResults.elapsedSeconds;
        serverResults.charactersPerSecond = serverResults.totalReceivedBytes / serverResults.elapsedSeconds;
    }

    logMessage("Data exchange complete.");
    logMessage("Performance: " + std::to_string(serverResults.throughputMBps) + " MB/s, " + 
               std::to_string(serverResults.charactersPerSecond) + " chars/s (CPS)");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // 3-way handshake: Wait for client's READY ACK first, then send our READY ACK
    if (!waitForReadyAck(serial)) {
        logMessage("Error: Client not ready for result exchange.");
        return;
    }
    
    // Send our READY ACK to signal we're ready to receive client's results
    if (!sendReadyAck(serial)) {
        logMessage("Error: Failed to synchronize with client.");
        return;
    }
    
    logMessage("Synchronization complete. Starting result exchange.");
    // Read client results immediately after sending READY ACK (client will send after receiving our READY ACK)
    Results clientResults;
    if (!readResults(serial, clientResults, "client", 3)) {
        logMessage("Error: Failed to receive results from client.");
        return;
    }
    
    logMessage("Results received from client.");

    if (serial.write(reinterpret_cast<char*>(&serverResults), sizeof(Results)) != sizeof(Results)) {
        logMessage("Error: Failed to send results to client.");
    } else {
        logMessage("Server results sent to client.");
        // Flush to ensure data is transmitted
        if (!serial.flush()) {
            logMessage("Warning: Failed to flush serial port buffers.");
        }
    }

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
}
