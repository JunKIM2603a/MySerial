사용 방법
컴파일:
C++ 컴파일러(예: g++, Visual Studio)를 사용하여 위 코드를 컴파일합니다.

g++ 사용 시: g++ -o SerialCommunicator.exe SerialCommunicator.cpp -lws2_32 -lpthread

Visual Studio: 새 C++ 콘솔 앱 프로젝트를 생성하고, 코드를 붙여넣은 후 빌드합니다.

실행:
두 개의 명령 프롬프트(CLI) 창을 엽니다. 가상 시리얼 포트 프로그램(예: com0com)을 사용하여 두 개의 연결된 가상 포트(예: COM3, COM4)를 생성해야 합니다.

서버 실행:

Bash

SerialCommunicator.exe server <COM_PORT> <BAUDRATE>
예: SerialCommunicator.exe server COM3 9600

클라이언트 실행:

Bash

SerialCommunicator.exe client <COM_PORT> <BAUDRATE> <DATASIZE> <NUM>
예: SerialCommunicator.exe client COM4 9600 1024 100

<COM_PORT>: 연결할 시리얼 포트 (예: COM3)

<BAUDRATE>: 통신 속도 (예: 9600, 115200)

<DATASIZE>: 1회 전송할 데이터 크기 (bytes)

<NUM>: 총 전송 횟수

결과 확인:
프로그램 실행이 완료되면, 실행 폴더에 serial_log_client.txt와 serial_log_server.txt 로그 파일이 생성됩니다. 이 파일들을 통해 상세한 송수신 과정과 최종 결과를 확인할 수 있습니다.

코드 설명
SerialPort 클래스: Windows API (CreateFile, GetCommState, SetCommState 등)를 사용하여 시리얼 포트의 열기, 설정, 읽기, 쓰기 기능을 캡슐화합니다.

logMessage 함수: 콘솔 출력과 파일 로깅을 동시에 수행하여 프로그램의 동작을 쉽게 추적할 수 있도록 합니다.

Settings 및 Results 구조체: 클라이언트와 서버 간에 설정 및 결과 정보를 주고받기 위한 데이터 구조입니다.

main 함수: 명령줄 인수를 파싱하여 프로그램이 'client' 모드로 실행될지 'server' 모드로 실행될지 결정합니다.

clientMode, serverMode 함수: 각 모드의 동작 로직을 구현합니다.

초기 설정 교환 (Settings -> ACK)

C++ std::thread를 사용하여 데이터 송신과 수신을 동시에 처리합니다.

데이터 교환 완료 후 1초 대기 (std::this_thread::sleep_for)

최종 결과 교환 및 로깅

주의사항
이 코드는 Windows 운영체제에서만 동작합니다.

실제 RS232 통신 또는 가상 시리얼 포트 프로그램이 설정되어 있어야 합니다.

에러 처리는 기본적인 수준으로 구현되어 있으므로, 실제 산업 환경에 적용하기 위해서는 더욱 견고한 예외 처리 로직이 필요할 수 있습니다.

데이터 무결성 검증은 현재 수신된 데이터의 크기를 기준으로만 판단합니다. 더 정밀한 검증을 원한다면 체크섬(Checksum)이나 CRC(Cyclic Redundancy Check)와 같은 기법을 데이터 패킷에 추가해야 합니다.