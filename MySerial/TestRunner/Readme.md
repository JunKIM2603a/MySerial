# TestRunner 빌드 및 실행 방법 (한국어)

아래는 `TestRunner.cpp` 와 해당 `CMakeLists.txt` 를 기반으로 한 **테스트 오케스트레이터(TestRunner)** 프로그램을 빌드하고 실행하는 방법을 한글로 정리한 문서입니다.

---

## 📁 1. TestRunner 빌드 디렉토리 생성

먼저 TestRunner 전용 빌드 폴더를 생성하고 그 폴더로 이동합니다.

```bash
mkdir TestRunner\build
cd TestRunner\build
```

---

## ⚙️ 2. CMake 로 빌드 파일 생성

`CMakeLists.txt` 가 있는 TestRunner 디렉토리를 기준으로 CMake 설정을 생성합니다.

```bash
cmake ..
```

---

## 🔨 3. 프로그램 컴파일

Release 모드로 빌드합니다.

```bash
cmake --build . --config Release
```

빌드가 완료되면 실행 파일이 아래 경로에 생성됩니다:

```
TestRunner\build\Release\TestRunner.exe
```

---

## ▶️ 4. TestRunner 실행 방법

프로젝트 루트 경로에서 실행합니다.

### 실행 형식

```
.\TestRunner\build\Release\TestRunner.exe <반복횟수> <데이터크기> <패킷개수> <보드레이트> <COM포트쌍> <로그저장>
```

### 실행 예시

```
.\TestRunner\build\Release\TestRunner.exe 5 1024 100 115200 COM3,COM4,COM5,COM6 false
```

* **반복횟수:** 5회 반복 실행
* **데이터크기:** 1024 bytes (프레임당 payload 크기)
* **패킷개수:** 100개
* **보드레이트:** 115200 bps
* **COM포트쌍:** COM3,COM4,COM5,COM6
  - (COM3,COM4): 서버는 COM3, 클라이언트는 COM4 사용
  - (COM5,COM6): 서버는 COM5, 클라이언트는 COM6 사용
* **로그저장:** false (로그 파일 저장 여부)

### 중요: COM 포트 쌍 설정

SerialCommunicator.exe는 서버와 클라이언트가 **서로 다른 COM 포트**를 사용해야 합니다.

1. **가상 시리얼 포트 프로그램 설치 필요**
   - com0com 같은 가상 시리얼 포트 프로그램을 설치해야 합니다
   - 예: COM3 ↔ COM4, COM5 ↔ COM6 쌍을 생성

2. **COM 포트 쌍 지정 방법**
   - COM 포트는 반드시 **짝수 개**로 지정해야 합니다
   - 형식: `서버포트1,클라이언트포트1,서버포트2,클라이언트포트2,...`
   - 예: `COM3,COM4` → 서버 COM3, 클라이언트 COM4
   - 예: `COM3,COM4,COM5,COM6` → (COM3,COM4) 쌍과 (COM5,COM6) 쌍

3. **포트 쌍 연결 확인**
   - 각 서버/클라이언트 포트 쌍은 가상 시리얼 포트 프로그램으로 연결되어 있어야 합니다
   - 예: COM3과 COM4가 서로 연결되어 있어야 함

---

## 📊 5. 기능 설명

TestRunner는 SerialCommunicator.exe 프로그램을 반복적으로 실행하고 결과를 자동 수집하는 테스트 오케스트레이터입니다:

### 핵심 기능
* **반복 테스트 실행**: 지정된 횟수만큼 자동으로 서버/클라이언트 프로세스 실행
* **멀티노드 테스트**: 여러 COM 포트 쌍을 동시에 테스트
* **결과 자동 수집**: 각 테스트의 stdout을 캡처하여 통계 추출
* **상세한 파싱**: "Final Server Report" / "Final Client Report" 섹션을 정밀하게 파싱
* **엄격한 검증**: 패킷 수, 바이트 수, 에러 수를 검증하여 PASS/FAIL 판정
* **종합 요약**: 전체 반복에 대한 PASS/FAIL 통계 및 총계 제공

### 최신 개선사항
* **동적 타임아웃**: `numPackets`와 `baudrate` 기반으로 서버 대기 시간 자동 계산
* **무제한 모드 거부**: `numPackets == 0` (무한 전송)은 TestRunner에서 명시적으로 비지원
* **프로세스 조기 종료 감지**: 서버가 준비 메시지 출력 전에 종료되는 경우 즉시 감지
* **리소스 정리 대기**: 
  - 각 서버 프로세스 종료 후 200ms 대기
  - Iteration 간 3초 대기로 포트 및 리소스 완전 해제 보장
* **상세한 실패 사유**: 파싱 실패 시 정확한 위치와 원인 표시
* **포트 쌍 지원**: 서버와 클라이언트가 서로 다른 COM 포트를 사용하도록 지원

### 출력 형식
```
--- FINAL TEST SUMMARY ---
Role   COM Port     Duration (s)   Throughput (Mbps)   Total Bytes Rx   Total Packets Rx   Status
---------------------------------------------------------------------------------------------------------
Server COM3/COM4    0.00           0.00                103000           100                PASS
Client COM3/COM4    0.00           0.00                103000           100                PASS
```

### 참고사항
* TestRunner는 프로젝트 루트에서 실행해야 합니다 (SerialCommunicator.exe 상대 경로 사용)
* 로그 파일은 `serial_log_server.txt`와 `serial_log_client.txt`에 자동 저장됩니다
* 간헐적 실패 발생 시 로그 파일을 확인하여 디버깅하세요
* COM 포트는 반드시 짝수 개로 지정해야 하며, 서버/클라이언트 쌍으로 구성되어야 합니다
* 각 포트 쌍은 가상 시리얼 포트 프로그램(com0com 등)으로 연결되어 있어야 합니다
