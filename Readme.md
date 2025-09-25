# MySerial 프로젝트

이 프로젝트는 두 개의 하위 프로젝트로 구성된 C++ 직렬 통신 및 파이프 통신 솔루션입니다.

## 📝 프로젝트 구성

1.  **MySerial**: 직렬(Serial) 통신을 담당하는 메인 프로그램입니다.
2.  **MySerial_Pipe**: 명명된 파이프(Named Pipe)를 통해 데이터를 수신하는 소비자(Consumer) 프로그램입니다.

---

## 1. MySerial

`MySerial`은 `SerialCommunicator.exe`를 통해 직렬 통신을 수행하는 프로그램입니다. 클라이언트 또는 서버 역할을 할 수 있으며, 통신 내용은 `serial_log_client.txt`와 `serial_log_server.txt`에 각각 기록됩니다.

### 주요 파일
- `SerialCommunicator.cpp`: 메인 소스 코드
- `SerialCommunicator.exe`: 컴파일된 실행 파일
- `serial_log_client.txt`: 클라이언트 모드 실행 시 로그
- `serial_log_server.txt`: 서버 모드 실행 시 로그

### 실행 방법
- `SerialCommunicator.exe`를 직접 실행하여 사용합니다. (필요에 따라 명령줄 인자를 사용할 수 있습니다.)

---

## 2. MySerial_Pipe

`MySerial_Pipe`는 다른 프로세스에서 명명된 파이프를 통해 보낸 데이터를 읽어 처리하는 CMake 기반의 콘솔 애플리케이션입니다.

### 주요 파일
- `main.cpp`: 소비자(Consumer) 프로그램의 소스 코드
- `CMakeLists.txt`: 빌드를 위한 CMake 설정 파일
- `build/`: 빌드 결과물이 저장되는 디렉터리
- `build/Release/consumer.exe`: 컴파일된 실행 파일

### 빌드 방법
1.  `MySerial_Pipe` 디렉터리로 이동합니다.
2.  `mkdir build` 명령어로 빌드 디렉터리를 생성합니다.
3.  `cd build`로 빌드 디렉터리에 들어갑니다.
4.  `cmake ..` 명령어로 빌드 설정을 생성합니다.
5.  생성된 솔루션 파일(.sln)을 Visual Studio로 열어 빌드하거나, `cmake --build .` 명령어를 사용하여 빌드합니다.

---

## 🚀 프로젝트 연동 시나리오

`SerialCommunicator.exe`가 직렬 포트로부터 데이터를 수신한 후, 이 데이터를 명명된 파이프를 통해 `consumer.exe`에게 전달하는 방식으로 두 프로그램이 연동될 수 있습니다.

1.  `consumer.exe`를 실행하여 파이프 데이터 수신을 대기합니다.
2.  `SerialCommunicator.exe`를 실행하여 직렬 통신을 시작하고, 수신된 데이터를 파이프로 전달하도록 설정합니다.
3.  `consumer.exe`는 파이프를 통해 들어온 데이터를 화면에 출력하거나 파일로 기록하는 등의 처리를 수행합니다.
