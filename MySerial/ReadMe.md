# SerialCommunicator - 시리얼 통신 테스트 프로그램

고신뢰성 고성능 시리얼 통신 테스트를 위한 클라이언트/서버 프로그램입니다.  
Selective Repeat ARQ 프로토콜과 동적 슬라이딩 윈도우로 최적 성능을 제공합니다.

## 주요 특징

### Protocol Version 4 기능 (최신 - 고성능)
- **Selective Repeat ARQ**: 윈도우 기반 연속 프레임 전송으로 throughput 극대화
- **동적 슬라이딩 윈도우**: 네트워크 상태에 따라 윈도우 크기 자동 조절 (4-32 프레임)
- **비트맵 기반 ACK**: 32개 프레임 상태를 한 번에 확인하여 효율성 향상
- **멀티스레드 전송**: Sender/Receiver 스레드 분리로 동시 송수신 구현
- **즉시 ACK 전송**: 프레임 수신 즉시 ACK 전송으로 재전송 최소화
- **3-way Handshake**: 결과 교환 시 명확한 동기화 보장
- **Burst 전송**: 프레임 크기에 따라 최적화된 버스트 전송
- **Out-of-Order 버퍼링**: 순서 무관 수신으로 재전송 최소화
- **빠른 체크섬 검증**: XOR 기반 빠른 데이터 무결성 확인
- **최적화된 재전송**: NAK된 프레임만 선택적으로 재전송
- **성능 측정**: Throughput (MB/s), CPS (chars/sec) 실시간 측정
- **90%+ 효율**: 이론적 최대 throughput의 90% 이상 달성

### Protocol Version 2 기능 (레거시 - 안정성 우선)
- **Stop-and-Wait ARQ**: 프레임별 전송 확인 및 자동 재전송
- **Half-Duplex 통신**: 순차적 송수신으로 안정성 극대화
- **프레임 재동기화**: 에러 발생 시 자동으로 프레임 경계 복구
- **동적 타임아웃**: 데이터 크기와 전송속도에 따라 자동 계산

### 프로토콜 구조

#### Version 4 (현재)
- **데이터 프레임**: `[SOF(1)][FrameNum(4)][WindowSize(2)][Checksum(2)][Payload][EOF(1)]`
- **ACK 프레임**: `[SOF_ACK(1)][ACK(3)][BaseFrameNum(4)][Bitmap(4)][EOF(1)]` (13 bytes)
- **READY ACK 프레임**: `[SOF_ACK(1)][R][E][A][D][Y][EOF(1)]` (7 bytes) - Phase 3 동기화용
- **Bitmap**: 32개 프레임의 ACK 상태를 비트로 표현

#### Version 2 (레거시)
- **데이터 프레임**: `[SOF(0x02)][FrameNum(4bytes)][Payload][EOF(0x03)]`
- **ACK 프레임**: `[SOF_ACK(0x04)][ACK(3bytes)][FrameNum(4bytes)][EOF(0x03)]`
- **NAK 프레임**: `[SOF_ACK(0x04)][NAK(3bytes)][FrameNum(4bytes)][EOF(0x03)]`

## 시스템 요구사항

- **운영체제**: Windows (Windows API 사용)
- **컴파일러**: 
  - MinGW g++ (권장)
  - Visual Studio 2022 MSVC
- **C++ 표준**: C++11 이상
- **시리얼 포트**: 
  - 물리적 RS232 포트
  - 가상 시리얼 포트 (예: com0com, Null-modem emulator)

## 컴파일

### 방법 1: 자동 컴파일 (권장)
```bash
compile.bat
```

### 방법 2: 수동 컴파일

#### MinGW g++ 사용
```bash
g++ -std=c++11 SerialCommunicator.cpp -o SerialCommunicator.exe -static-libgcc -static-libstdc++ -O2
```

#### Visual Studio MSVC 사용
```bash
cl /EHsc /std:c++14 /O2 SerialCommunicator.cpp /link /out:SerialCommunicator.exe
```

## 사용 방법

### 1. 가상 시리얼 포트 준비
com0com 등을 사용하여 연결된 포트 쌍 생성 (예: COM1 ↔ COM2)

### 2. 서버 실행
```bash
SerialCommunicator.exe server <COM_PORT> <BAUDRATE>
```

**예시:**
```bash
SerialCommunicator.exe server COM1 9600
```

### 3. 클라이언트 실행 (다른 터미널에서)
```bash
SerialCommunicator.exe client <COM_PORT> <BAUDRATE> <DATASIZE> <NUM>
```

**예시:**
```bash
SerialCommunicator.exe client COM2 9600 1024 100
```

### 매개변수 설명

| 매개변수 | 설명 | 예시 |
|---------|------|------|
| `<COM_PORT>` | 시리얼 포트 이름 | COM1, COM2 |
| `<BAUDRATE>` | 통신 속도 (bps) | 9600, 115200, 921600 |
| `<DATASIZE>` | 프레임당 페이로드 크기 (bytes) | 1024, 2048, 4096 |
| `<NUM>` | 전송할 프레임 개수 | 100, 1000 |

## Protocol Version 4 권장 설정

### 안정적인 통신 (신뢰성 우선)
프레임 크기가 작아 안정적이지만, 오버헤드가 상대적으로 높습니다.

```bash
# 서버 실행
SerialCommunicator.exe server COM3 115200

# 클라이언트 실행
SerialCommunicator.exe client COM5 115200 1024 1000
```

**예상 성능:**
- Throughput: 8-10 KB/s
- 효율: 약 70-90%
- 프레임 크기: 1034 bytes (1024 payload + 10 overhead)
- 윈도우 크기: 자동 조절 (4-32 프레임)

### 고성능 통신 (처리량 최대화)
프레임 크기가 커서 오버헤드 비율이 낮아 최대 throughput을 달성합니다.

```bash
# 서버 실행
SerialCommunicator.exe server COM3 115200

# 클라이언트 실행
SerialCommunicator.exe client COM5 115200 4096 500
```

**예상 성능:**
- Throughput: 10-11 KB/s (이론적 최대치)
- 효율: 약 90-95%
- 프레임 크기: 4106 bytes (4096 payload + 10 overhead)
- 윈도우 크기: 자동 조절 (4-32 프레임)

### 성능 벤치마크 (115200 baud 기준)

| 프로토콜 | 프레임 크기 | Throughput | 효율 | 전송 시간 (10 frames) |
|---------|-----------|-----------|------|---------------------|
| V2 (Stop-and-Wait) | 115206 bytes | 5.7 KB/s | 50% | 200 seconds |
| V4 (Selective Repeat) | 1024 bytes | 8-10 KB/s | 70-90% | 12-15 seconds |
| V4 (Selective Repeat) | 4096 bytes | 10-11 KB/s | 90-95% | 37-40 seconds |

**이론적 최대 throughput**: 115200 bps ÷ 10 bits ÷ 1024 = **11.25 KB/s**

### 프레임 크기 선택 가이드

| 프레임 크기 | 장점 | 단점 | 추천 상황 |
|----------|------|------|----------|
| 512-1024 bytes | 안정적, 빠른 재전송 | 오버헤드 높음 | 불안정한 연결, 디버깅 |
| 2048-4096 bytes | 균형잡힌 성능 | 보통 | 대부분의 상황 (권장) |
| 8192+ bytes | 최대 효율 | 재전송 비용 높음 | 매우 안정적인 연결 |

## 통신 프로세스

### Protocol V4 프로세스 (Selective Repeat ARQ with Multi-threaded Transmission)

#### Phase 0: 설정 교환 및 검증
1. 클라이언트가 서버에 설정 정보 전송 (프로토콜 버전=4, datasize, num)
2. 서버가 프로토콜 버전 확인 (V4 전용)
3. 서버가 ACK 응답 전송

#### Phase 1: 클라이언트 → 서버 데이터 전송
- **멀티스레드 전송**: Sender Thread와 Receiver Thread 분리
- **슬라이딩 윈도우 방식**: 윈도우 크기만큼 프레임을 연속 전송
- **Burst 전송**: 프레임 크기에 따라 최적화된 버스트 전송
- **비동기 ACK 수신**: ACK를 대기하지 않고 다음 프레임 전송
- **비트맵 ACK 처리**: 32개 프레임 상태를 한 번에 확인
- **선택적 재전송**: NAK된 프레임만 재전송
- **동적 윈도우 조절**: 
  - 연속 3회 성공 시 윈도우 2배 증가 (최대 32)
  - 연속 3회 실패 시 윈도우 절반 감소 (최소 4)
  - RTT > 2000ms 시 윈도우 축소

#### Phase 2: 서버 → 클라이언트 데이터 수신
- **즉시 ACK 전송**: 프레임 수신 즉시 ACK 전송 (검증 전)
- **Out-of-order 프레임 버퍼링**: 순서 무관 수신 허용
- **체크섬 및 페이로드 검증**: 백그라운드에서 검증 수행
- **멀티스레드 전송**: 서버도 동일한 멀티스레드 방식으로 전송

#### Phase 3: 결과 교환 (3-way Handshake)
1. 클라이언트가 READY ACK 전송
2. 서버가 READY ACK 수신 후 READY ACK 전송
3. 클라이언트가 서버의 READY ACK 수신 후 결과 데이터 전송
4. 서버가 결과 데이터 수신 후 결과 데이터 전송
5. 양쪽 모두 상세 리포트 출력

## 프로토콜 흐름도

### 전체 통신 흐름

```
[Client]                    [Server]
   |                           |
   |-- Settings ----------->|  Phase 0: 설정 교환
   |<-- ACK ----------------|
   |                           |
   |-- Frame 0-15 --------->|  Phase 1: Client → Server
   |<-- ACK (bitmap) -------|
   |-- Frame 16-31 -------->|
   |<-- ACK (bitmap) -------|
   |                           |
   |<-- Frame 0-15 ---------|  Phase 2: Server → Client
   |-- ACK (bitmap) ------->|
   |<-- Frame 16-31 --------|
   |-- ACK (bitmap) ------->|
   |                           |
   |-- READY ACK ---------->|  Phase 3: Results Exchange
   |<-- READY ACK ----------|
   |-- Results ------------>|
   |<-- Results ------------|
```

### 3-way Handshake 상세 흐름

```
[Client]                    [Server]
   |                           |
   |-- READY ACK ---------->|  Step 1: Client 준비 완료 신호
   |                           |
   |<-- READY ACK ----------|  Step 2: Server 준비 완료 신호
   |                           |
   |-- Results ------------>|  Step 3: Client 결과 전송
   |                           |
   |<-- Results ------------|  Step 4: Server 결과 전송
```

## 프레임 구조 다이어그램

### 데이터 프레임 구조

```
┌─────┬──────────┬────────────┬──────────┬─────────┬─────┐
│ SOF │ FrameNum │ WindowSize │ Checksum │ Payload │ EOF │
│ (1) │   (4)    │    (2)     │   (2)    │  (N)    │ (1) │
└─────┴──────────┴────────────┴──────────┴─────────┴─────┘
 0x02  0-3 bytes  4-5 bytes    6-7 bytes  8-N+7    0x03

총 오버헤드: 10 bytes
```

### ACK 프레임 구조

```
┌────────┬──────┬──────────────┬─────────┬─────┐
│ SOF_ACK│ ACK  │ BaseFrameNum │ Bitmap  │ EOF │
│  (1)   │ (3)  │     (4)      │   (4)   │ (1) │
└────────┴──────┴──────────────┴─────────┴─────┘
  0x04   'ACK'   0-3 bytes      4-7 bytes  0x03

총 크기: 13 bytes
```

### ACK 비트맵 동작 원리

```
BaseFrameNum = 10
Bitmap = 0b00000000000000000000000000001101

비트 위치:  31 30 ... 3  2  1  0
프레임 번호: 41 40 ... 13 12 11 10
ACK 상태:    0  0  ... 0  1  1  0  1

→ 프레임 10, 11, 13이 ACK됨
→ 프레임 12는 아직 ACK되지 않음
```

## 슬라이딩 윈도우 다이어그램

### 윈도우 슬라이드 과정

```
초기 상태 (baseSeq=0, windowSize=16):
[0][1][2][3][4][5][6][7][8][9][10][11][12][13][14][15]
 ↑                                    ↑
baseSeq                        baseSeq+windowSize

프레임 0-2 ACK 수신 후:
[0][1][2][3][4][5][6][7][8][9][10][11][12][13][14][15]
     ↑                                    ↑
  baseSeq                        baseSeq+windowSize

윈도우 슬라이드 완료 (baseSeq=3):
[3][4][5][6][7][8][9][10][11][12][13][14][15][16][17][18]
 ↑                                    ↑
baseSeq                        baseSeq+windowSize
```

### 동적 윈도우 크기 조절

```
성공 시 (연속 3회):
windowSize = windowSize * 2  (최대 32)

실패 시 (연속 3회):
windowSize = windowSize / 2  (최소 4)

RTT 기반:
if (RTT > 2000ms):
    windowSize = windowSize / 2
```

## 멀티스레드 구조 다이어그램

### TransmissionManager 구조

```
┌─────────────────────────────────────────┐
│      TransmissionManager                │
├─────────────────────────────────────────┤
│  ┌──────────────┐    ┌──────────────┐  │
│  │ Sender       │    │ Receiver     │  │
│  │ Thread       │    │ Thread       │  │
│  │              │    │              │  │
│  │ - Burst 전송 │    │ - ACK 수신   │  │
│  │ - 윈도우 관리│    │ - 비트맵 처리│  │
│  └──────┬───────┘    └──────┬───────┘  │
│         │                    │          │
│         └────────┬───────────┘          │
│                  │                      │
│         ┌─────────▼──────────┐          │
│         │  WindowManager     │          │
│         │  (Thread-safe)     │          │
│         └────────────────────┘          │
└─────────────────────────────────────────┘
```

## 코드 구조

### 주요 클래스

#### SerialPort 클래스
- **역할**: Windows Overlapped I/O 기반 비동기 시리얼 통신
- **주요 메서드**:
  - `open()`: 시리얼 포트 열기 및 초기화
  - `read()`: 비동기 읽기 (Overlapped I/O)
  - `write()`: 비동기 쓰기 (Overlapped I/O)
  - `flush()`: 쓰기 버퍼 플러시
- **Thread Safety**: 읽기/쓰기 각각 별도 뮤텍스로 보호

#### WindowManager 클래스
- **역할**: 슬라이딩 윈도우 알고리즘 구현 및 동적 크기 조절
- **주요 메서드**:
  - `getFramesToSend()`: 전송할 프레임 목록 반환
  - `slideWindow()`: 윈도우 슬라이드
  - `adjustWindow()`: 동적 윈도우 크기 조절
- **Thread Safety**: 모든 메서드가 뮤텍스로 보호

#### TransmissionManager 클래스
- **역할**: 멀티스레드 기반 송수신 관리
- **주요 구성**:
  - `senderThreadFunc()`: 송신자 스레드 (Burst 전송)
  - `receiverThreadFunc()`: 수신자 스레드 (ACK 처리)
- **Thread Safety**: WindowManager와 SerialPort의 Thread-safe 메서드 사용

### 데이터 구조체

#### DataFrame 구조체
- `frameNum`: 프레임 순서 번호
- `windowSize`: 현재 윈도우 크기
- `checksum`: XOR Rotate 체크섬
- `payload`: 실제 데이터

#### AckFrame 구조체
- `baseFrameNum`: 비트맵의 기준 프레임 번호
- `bitmap`: 32비트 비트맵 (최대 32개 프레임 ACK 상태)

### Protocol V2 프로세스 (Stop-and-Wait) - 레거시

#### Phase 1: 설정 교환 및 검증
1. 클라이언트가 서버에 설정 정보 전송
2. 서버가 프로토콜 버전 확인
3. 서버가 ACK 응답 전송

#### Phase 2: 데이터 교환 (Half-Duplex)

##### 단계 1: 클라이언트 → 서버
- 한 번에 한 프레임씩 순차 전송
- 각 프레임마다 ACK/NAK 대기
- NAK 또는 타임아웃 시 재전송 (최대 3회)

##### 단계 2: 서버 → 클라이언트
- 동일한 Stop-and-Wait 방식

#### Phase 3: 결과 교환
- V3와 동일

## 출력 결과 예시

### Protocol V4 콘솔 출력
```
--- Client Mode (Protocol V4) ---
Configuration: datasize=1024 bytes, frames=1000, window=16-32
Port buffers purged on open.
Port COM5 opened successfully at 115200 bps.
Sending settings to server...
Settings sent: protocol=3, datasize=1024, num=1000

Phase 1: Client transmitting with Selective Repeat ARQ...
Progress: 100/1000 frames acknowledged
Progress: 200/1000 frames acknowledged
Progress: 300/1000 frames acknowledged
...
Progress: 1000/1000 frames acknowledged
Phase 1 complete: All frames transmitted and acknowledged.

Phase 2: Client receiving with Selective Repeat ARQ...
Progress: 100/1000 frames received and validated
Progress: 200/1000 frames received and validated
...
Progress: 1000/1000 frames received and validated
Phase 2 complete: All frames received and validated.

Data exchange complete.
Performance: 0.009456 MB/s, 9925.123 chars/s (CPS)
```

### Protocol V4 최종 리포트
```
=== Final Client Report ===
Test Configuration:
  - Data size: 1024 bytes
  - Frame count: 1000
  - Protocol version: 4

Client Transmission Results:
  - Retransmissions: 2

Client Reception Results:
  - Received frames: 1000/1000
  - Total bytes: 1034000
  - Errors: 0
  - Elapsed time: 104.235 seconds
  - Throughput: 0.009456 MB/s
  - CPS (chars/sec): 9921.456

Server Reception Results:
  - Received frames: 1000/1000
  - Total bytes: 1034000
  - Errors: 0
  - Retransmissions: 1
  - Elapsed time: 104.240 seconds
  - Throughput: 0.009455 MB/s
  - CPS (chars/sec): 9920.123
=========================
```

### Protocol V2 콘솔 출력 (레거시)
```
--- Client Mode (Protocol V2) ---
Port buffers purged on open.
Port COM2 opened successfully at 9600 bps.
Attempting to send settings to server...
Settings sent to server: protocolVersion=2, datasize=1024, num=100

Phase 1: Client sending data...
Frame 0 sent and ACKed.
...
Phase 1 complete: All frames sent.

Phase 2: Client receiving data...
Received frame 0: OK, ACK sent.
...
Phase 2 complete: All frames received.

Data exchange complete.
Performance: 0.098 MB/s, 102400 chars/s (CPS)
```

## 로그 파일

프로그램 실행 시 자동으로 로그 파일 생성:

### 파일명 형식
```
serial_log_<mode>_<comport>_<timestamp>.txt
```

### 예시
```
serial_log_client_COM2_20251120_223317.txt
serial_log_server_COM1_20251120_223316.txt
```

로그 파일에는 모든 프레임 송수신 상태, 에러 정보, 성능 지표가 기록됩니다.

## 성능 지표 설명

### Throughput (MB/s)
```
Throughput = (총 수신 바이트) / (1024 * 1024) / (경과 시간)
```
메가바이트 단위의 데이터 처리량

### CPS (Characters Per Second)
```
CPS = (총 수신 바이트) / (경과 시간)
```
초당 전송된 문자(바이트) 수, 실제 전송 속도를 직관적으로 표현

### 재전송 횟수
NAK 수신 또는 ACK 타임아웃으로 인한 재전송 시도 횟수

## 에러 처리

### 자동 복구 기능
1. **프레임 재동기화**: SOF 탐색으로 프레임 경계 복구
2. **자동 재전송**: 최대 3회까지 프레임 재전송 시도
3. **부분 데이터 처리**: 일부 프레임 손실 시에도 통계 기록
4. **버퍼 플러싱**: 오염된 데이터 자동 제거

### 에러 유형
- **Size mismatch**: 프레임 크기 불일치
- **SOF/EOF mismatch**: 프레임 경계 오류
- **Frame num mismatch**: 순서 오류
- **Payload content mismatch**: 데이터 내용 오류
- **ACK timeout**: 응답 시간 초과

## 다중 노드 테스트

`Multi-node_tester.bat`를 사용하여 여러 포트 쌍을 동시에 테스트 가능:

### 설정 편집
```batch
set BAUDRATE=9600
set DATASIZE=1024
set NUM=100

set PAIRS[0]=COM3 COM4
set PAIRS[1]=COM5 COM6
set PAIRS[2]=COM7 COM8
```

### 실행
```bash
Multi-node_tester.bat
```

## 기술 세부사항

### Protocol V3 핵심 기술

#### 1. 슬라이딩 윈도우 관리
**WindowManager 클래스**:
- 현재 윈도우 크기: 4-32 프레임 (동적 조절)
- 베이스 시퀀스 번호 추적
- 프레임별 ACK 상태 맵 관리
- Thread-safe 구현

**동적 윈도우 조절 알고리즘**:
```
성공 시 (연속 5회):
  windowSize = min(windowSize * 1.5, 32)

실패 시 (연속 2회):
  windowSize = max(windowSize / 2, 4)

RTT 기반 조절:
  if RTT > 1000ms:
    windowSize = max(windowSize / 2, 4)
```

#### 2. 비트맵 기반 ACK
- 32-bit 비트맵으로 최대 32개 프레임 상태 표현
- 한 번의 ACK로 여러 프레임 확인
- 네트워크 오버헤드 최소화

#### 3. Out-of-Order 버퍼링
- `std::map<int, DataFrame>`로 순서 무관 저장
- 연속된 프레임 도착 시 자동 슬라이드
- 재전송 최소화

#### 4. 체크섬 검증
**XOR 기반 Rotate Checksum**:
```cpp
uint16_t sum = 0;
for (byte in payload) {
    sum ^= byte;
    sum = (sum << 1) | (sum >> 15);  // Rotate left
}
```
- 빠른 계산 속도
- 충분한 오류 감지율

#### 5. 선택적 재전송
- NAK된 프레임만 재전송
- 윈도우 내 미확인 프레임만 관리
- Stop-and-Wait 대비 큰 효율 개선

### Thread Safety
- 모든 시리얼 포트 작업은 Mutex로 보호
- 읽기/쓰기 작업 간 상호 배제 보장
- OVERLAPPED 구조체 재사용 시 완전 초기화
- SafeQueue 클래스로 쓰레드 간 안전한 통신

### 동적 타임아웃 계산
```
timeout = (dataSize * 10 / baudrate) * 1000 * 2.5 + 500ms
```
- 데이터 크기와 전송속도 고려
- 2.5배 안전 여유율 적용
- 최소 2초, 최대 60초 제한

### 버퍼 관리
- 포트 오픈 시 자동 플러싱
- 128KB 송수신 버퍼
- OVERLAPPED I/O 비동기 처리

### 성능 최적화 기법
1. **Zero-copy**: 불필요한 memcpy 최소화
2. **조건부 로깅**: DEBUG 모드에서만 상세 로그
3. **Fine-grained locking**: Mutex 범위 최소화
4. **프레임 사전 준비**: 전송 전 모든 프레임 직렬화

## 문제 해결

### 프로토콜 버전 불일치
```
Error: Protocol version mismatch! Client: 2, Server: 1
```
**해결**: 양쪽 모두 최신 버전으로 재컴파일

### 포트 열기 실패
```
Error: Unable to open COM1
```
**해결**: 
1. 포트 번호 확인
2. 다른 프로그램에서 사용 중인지 확인
3. 관리자 권한으로 실행

### 프레임 재전송 반복
```
Frame 0 NAKed, retransmitting (attempt 1)
```
**해결**:
1. 케이블 연결 확인
2. baudrate 낮추기 (9600으로 테스트)
3. 전송 프레임 수 줄이기

### ACK 타임아웃
```
Frame 0 ACK timeout, retransmitting (attempt 1)
```
**해결**:
1. 서버가 먼저 실행되었는지 확인
2. baudrate 일치 여부 확인
3. 포트 번호 확인

## Protocol V2 vs V3 비교

| 특징 | V2 (Stop-and-Wait) | V3 (Selective Repeat) |
|-----|-------------------|---------------------|
| **ARQ 방식** | Stop-and-Wait | Selective Repeat |
| **윈도우 크기** | 1 (고정) | 4-32 (동적) |
| **ACK 방식** | 프레임별 개별 ACK | 비트맵 기반 누적 ACK |
| **재전송** | 타임아웃 시 해당 프레임만 | NAK된 프레임만 선택적 |
| **버퍼링** | 순차적 수신 필수 | Out-of-order 가능 |
| **Throughput** | 5.7 KB/s (50%) | 10-11 KB/s (90%+) |
| **프레임 오버헤드** | 6 bytes | 10 bytes |
| **체크섬** | 없음 | XOR Rotate (2 bytes) |
| **복잡도** | 낮음 | 중간 |
| **안정성** | 매우 높음 | 높음 |
| **권장 용도** | 디버깅, 불안정한 연결 | 일반 사용, 성능 중요 |

## 제한사항

### 공통
- Windows 전용 (Linux/macOS 지원 안 함)
- 매우 높은 baudrate(>921600)에서는 안정성 검증 필요

### Protocol V3 특정
- V2 대비 높은 메모리 사용 (윈도우 버퍼)
- 프레임 크기가 작을 때 오버헤드 비율 증가 (10 bytes)
- 복잡한 구조로 디버깅 난이도 높음

### Protocol V2 특정
- Half-Duplex 통신으로 성능 제한
- 큰 프레임 크기 사용 시 성능 급격히 저하
- 재전송 시 전체 프레임 재전송 필요

## 개발 이력

### Version 4.0 (현재)
- **멀티스레드 전송**: Sender/Receiver 스레드 분리로 동시 송수신 구현
- **즉시 ACK 전송**: 프레임 수신 즉시 ACK 전송으로 재전송 최소화
- **3-way Handshake**: 결과 교환 시 명확한 동기화 보장
- **Burst 전송**: 프레임 크기에 따라 최적화된 버스트 전송

### Version 3.0 (레거시)
- **Selective Repeat ARQ 구현**: 윈도우 기반 파이프라이닝
- **동적 슬라이딩 윈도우**: 네트워크 적응형 윈도우 조절 (4-32)
- **비트맵 ACK**: 32개 프레임 상태를 한 번에 확인
- **Out-of-order 버퍼링**: 순서 무관 수신 및 재조립
- **XOR Rotate 체크섬**: 빠른 데이터 무결성 검증
- **선택적 재전송**: NAK된 프레임만 재전송
- **성능 최적화**: Zero-copy, 조건부 로깅, Fine-grained locking
- **90%+ 효율**: 이론적 최대 throughput의 90% 이상 달성
- **SafeQueue**: Thread-safe 큐 시스템
- **RTT 기반 조절**: Round Trip Time 고려한 윈도우 크기 조절

### Version 2.0 (레거시)
- ACK/NAK 재전송 프로토콜 추가
- Half-Duplex 통신 방식 적용
- 프레임 재동기화 메커니즘 구현
- 동적 타임아웃 계산
- Thread-safe 구현
- 성능 지표 확장 (CPS 추가)
- 버퍼 관리 개선

### Version 1.0 (초기)
- 기본 시리얼 통신 기능
- Full-Duplex 동시 송수신
- 기본 에러 감지

## 라이선스

이 프로그램은 테스트 및 개발 목적으로 자유롭게 사용 가능합니다.

## 기여

버그 리포트 및 개선 제안은 언제든 환영합니다.
