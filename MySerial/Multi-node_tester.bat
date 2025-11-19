@echo off
setlocal enabledelayedexpansion

:: 반복 횟수 입력
rem set /p count=How many time to repeat? :

set count=1

:: 설정
set BAUDRATE=9600
set DATASIZE=1024
set NUM=100

:: COM 포트 쌍 (서버 COM 클라이언트 COM)
set PAIRS[0]=COM3 COM4
set PAIRS[1]=COM5 COM6
set PAIRS[2]=COM7 COM8

:: 반복 시작
for /L %%i in (1,1,%count%) do (
    echo ===============================
    echo START: Repeatation %%i
    echo ===============================

    for /L %%j in (0,1,2) do (
        for /f "tokens=1,2" %%a in ("!PAIRS[%%j]!") do (
            set SERVER_COM=%%a
            set CLIENT_COM=%%b

            :: 서버 COM이 이미 실행 중인지 확인
            :WAIT_SERVER
            tasklist /FI "IMAGENAME eq SerialCommunicator.exe" /V | findstr /I "server !SERVER_COM!" >nul
            if %errorlevel%==0 (
                echo Server on !SERVER_COM! is running. Waiting...
                timeout /t 1 >nul
                goto WAIT_SERVER
            )

            :: 클라이언트 COM이 이미 실행 중인지 확인
            :WAIT_CLIENT
            tasklist /FI "IMAGENAME eq SerialCommunicator.exe" /V | findstr /I "client !CLIENT_COM!" >nul
            if %errorlevel%==0 (
                echo Client on !CLIENT_COM! is running. Waiting...
                timeout /t 1 >nul
                goto WAIT_CLIENT
            )

            echo Starting Server on !SERVER_COM!...
            start "" /b SerialCommunicator.exe server !SERVER_COM! %BAUDRATE%

            echo Starting Client on !CLIENT_COM!...
            start "" /b SerialCommunicator.exe client !CLIENT_COM! %BAUDRATE% %DATASIZE% %NUM%

            echo -------------------------------
        )
    )

    echo COMPLETE: Repeatation %%i
    echo.
)

echo Total Repeatation Complete
rem pause
