@echo off
setlocal enabledelayedexpansion
REM ========================================
REM SerialCommunicator Compile Script
REM ========================================

echo Compiling SerialCommunicator.cpp...

REM ========================================
REM Option 1: Using MinGW g++ (Recommended)
REM ========================================
where g++ >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Using MinGW g++ compiler...
    g++ -std=c++11 SerialCommunicator.cpp -o SerialCommunicator.exe -static-libgcc -static-libstdc++ -O2
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ========================================
        echo Compilation successful!
        echo Output: SerialCommunicator.exe
        echo ========================================
        goto :end
    ) else (
        echo.
        echo ========================================
        echo Compilation failed with g++
        echo ========================================
        goto :error
    )
)

REM ========================================
REM Option 2: Using MSVC cl.exe (if already in PATH)
REM ========================================
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Using MSVC compiler...
    cl /EHsc /std:c++14 /O2 /nologo SerialCommunicator.cpp /link /out:SerialCommunicator.exe
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ========================================
        echo Compilation successful!
        echo Output: SerialCommunicator.exe
        echo ========================================
        REM Clean up MSVC temporary files
        if exist SerialCommunicator.obj del SerialCommunicator.obj >nul 2>nul
        goto :end
    ) else (
        echo.
        echo ========================================
        echo Compilation failed with MSVC
        echo ========================================
        goto :error
    )
)

REM ========================================
REM Option 3: Setup Visual Studio 2022 environment
REM ========================================
set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if exist "%VCVARS_BAT%" (
    echo Found Visual Studio 2022 Community at: %VCVARS_BAT%
    echo Creating temporary build script...
    
    REM Create temporary batch file
    echo @echo off > _temp_compile.bat
    echo echo Setting up Visual Studio environment... >> _temp_compile.bat
    echo call "%VCVARS_BAT%" ^>nul 2^>^&1 >> _temp_compile.bat
    echo echo. >> _temp_compile.bat
    echo echo Compiling with MSVC... >> _temp_compile.bat
    echo cl /EHsc /std:c++14 /O2 /nologo SerialCommunicator.cpp /link /out:SerialCommunicator.exe 2^>^&1 >> _temp_compile.bat
    echo exit /b %%ERRORLEVEL%% >> _temp_compile.bat
    
    call _temp_compile.bat
    set "COMPILE_RESULT=!ERRORLEVEL!"
    
    REM Clean up temporary file
    if exist _temp_compile.bat del _temp_compile.bat >nul 2>nul
    
    if !COMPILE_RESULT! EQU 0 (
        echo.
        echo ========================================
        echo Compilation successful!
        echo Output: SerialCommunicator.exe
        echo ========================================
        REM Clean up MSVC temporary files
        if exist SerialCommunicator.obj del SerialCommunicator.obj >nul 2>nul
        goto :end
    ) else (
        echo.
        echo ========================================
        echo Compilation failed with MSVC
        echo ========================================
        echo.
        echo You can try running this manually from Developer Command Prompt:
        echo   cl /EHsc /std:c++14 /O2 SerialCommunicator.cpp
        goto :error
    )
)

REM ========================================
REM No compiler found
REM ========================================
echo.
echo ========================================
echo ERROR: No C++ compiler found!
echo ========================================
echo.
echo Please install one of the following:
echo   1. MinGW-w64 (g++)
echo   2. Visual Studio (MSVC)
echo.
echo For MinGW: https://www.mingw-w64.org/
echo.
echo Or open "Developer Command Prompt for VS 2022" and run:
echo   cl /EHsc /std:c++14 /O2 SerialCommunicator.cpp
echo ========================================
goto :error

:error
pause
exit /b 1

:end
pause
exit /b 0
