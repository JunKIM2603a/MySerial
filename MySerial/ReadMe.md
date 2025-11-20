# SerialCommunicator - 시리얼 통신 테스트 프로그램

고신뢰성 시리얼 통신 테스트를 위한 클라이언트/서버 프로그램입니다.  
ACK/NAK 재전송 프로토콜과 성능 측정 기능을 제공합니다.

## 주요 특징

### Protocol Version 2 기능
- **ACK/NAK 재전송 프로토콜**: 프레임별 전송 확인 및 자동 재전송 (최대 3회)
- **Half-Duplex 통신**: 순차적 송수신으로 안정성 극대화
- **프레임 재동기화**: 에러 발생 시 자동으로 프레임 경계 복구
- **동적 타임아웃**: 데이터 크기와 전송속도에 따라 자동 계산
- **Thread-Safe 구현**: Mutex를 통한 안전한 멀티스레드 처리
- **버퍼 관리**: 자동 버퍼 플러싱으로 데이터 오염 방지
- **성능 측정**: Throughput (MB/s), CPS (chars/sec) 실시간 측정

### 프로토콜 구조
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
| `<DATASIZE>` | 프레임당 페이로드 크기 (bytes) | 1024, 2048 |
| `<NUM>` | 전송할 프레임 개수 | 100, 1000 |

## 통신 프로세스

### Phase 1: 설정 교환 및 검증
1. 클라이언트가 서버에 설정 정보 전송 (프로토콜 버전, datasize, num)
2. 서버가 프로토콜 버전 확인
3. 서버가 ACK 응답 전송

### Phase 2: 데이터 교환 (Half-Duplex)

#### 단계 1: 클라이언트 → 서버
- 클라이언트가 모든 프레임 순차 전송
- 각 프레임마다 서버의 ACK/NAK 대기
- NAK 또는 타임아웃 시 자동 재전송 (최대 3회)

#### 단계 2: 서버 → 클라이언트
- 서버가 모든 프레임 순차 전송
- 각 프레임마다 클라이언트의 ACK/NAK 대기
- NAK 또는 타임아웃 시 자동 재전송 (최대 3회)

### Phase 3: 결과 교환
1. READY ACK 동기화
2. 클라이언트가 결과 데이터 전송
3. 서버가 결과 데이터 응답
4. 양쪽 모두 상세 리포트 출력

## 출력 결과 예시

### 콘솔 출력
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

### 최종 리포트
```
=== Final Client Report ===
Test Configuration:
  - Data size: 1024 bytes
  - Frame count: 100
  - Protocol version: 2

Client Transmission Results:
  - Retransmissions: 0

Client Reception Results:
  - Received frames: 100/100
  - Total bytes: 103000
  - Errors: 0
  - Elapsed time: 1.024 seconds
  - Throughput: 0.096 MB/s
  - CPS (chars/sec): 100585.937500

Server Reception Results:
  - Received frames: 100/100
  - Total bytes: 103000
  - Errors: 0
  - Retransmissions: 0
  - Elapsed time: 1.020 seconds
  - Throughput: 0.096 MB/s
  - CPS (chars/sec): 100980.392157
=========================
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

### Thread Safety
- 모든 시리얼 포트 작업은 Mutex로 보호
- 읽기/쓰기 작업 간 상호 배제 보장
- OVERLAPPED 구조체 재사용 시 완전 초기화

### 동적 타임아웃 계산
```
timeout = (dataSize * 10 / baudrate) * 1000 * 3.0 + 1000ms
```
- 데이터 크기와 전송속도 고려
- 3배 안전 여유율 적용
- 최소 2초, 최대 60초 제한

### 버퍼 관리
- 포트 오픈 시 자동 플러싱
- 128KB 송수신 버퍼
- OVERLAPPED I/O 비동기 처리

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

## 제한사항

- Windows 전용 (Linux/macOS 지원 안 함)
- Half-Duplex 통신으로 Full-Duplex 대비 속도 감소
- 재전송 한계 도달 시 해당 프레임 손실
- 매우 높은 baudrate(>921600)에서는 안정성 검증 필요

## 개발 이력

### Version 2.0
- ACK/NAK 재전송 프로토콜 추가
- Half-Duplex 통신 방식 적용
- 프레임 재동기화 메커니즘 구현
- 동적 타임아웃 계산
- Thread-safe 구현
- 성능 지표 확장 (CPS 추가)
- 버퍼 관리 개선

### Version 1.0
- 기본 시리얼 통신 기능
- Full-Duplex 동시 송수신
- 기본 에러 감지

## 라이선스

이 프로그램은 테스트 및 개발 목적으로 자유롭게 사용 가능합니다.

## 기여

버그 리포트 및 개선 제안은 언제든 환영합니다.
