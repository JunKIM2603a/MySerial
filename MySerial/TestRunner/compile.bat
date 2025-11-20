@echo off
setlocal enabledelayedexpansion

REM ========================================
REM TestRunner Compile Script
REM ========================================

echo ========================================
echo TestRunner Build Script
echo ========================================
echo.

REM Check if CMake is available
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo CMake not found in PATH. Trying direct compilation...
    echo.
    goto :DirectCompile
)

echo CMake found: 
cmake --version
echo.

REM Create build directory if it doesn't exist
if not exist build (
    echo Creating build directory...
    mkdir build
)

REM Navigate to build directory
cd build

REM Configure CMake
echo ========================================
echo Configuring CMake project...
echo ========================================
cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo CMake configuration failed!
    echo ========================================
    echo.
    echo If you don't have Visual Studio 2022, try:
    echo   cmake .. -G "Visual Studio 16 2019" -A x64
    echo   cmake .. -G "Visual Studio 15 2017" -A x64
    echo   cmake .. -G "MinGW Makefiles"
    echo.
    cd ..
    pause
    exit /b 1
)

echo.
echo ========================================
echo Building TestRunner (Release)...
echo ========================================
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo Build failed!
    echo ========================================
    cd ..
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build successful!
echo ========================================
echo.
echo Executable location:
if exist Release\TestRunner.exe (
    echo   %CD%\Release\TestRunner.exe
    echo.
    echo File size:
    for %%A in (Release\TestRunner.exe) do echo   %%~zA bytes
) else (
    echo   TestRunner.exe (check build output for location)
)
echo.
echo ========================================

cd ..
pause
exit /b 0

REM ========================================
REM Direct Compilation (without CMake)
REM ========================================
:DirectCompile

echo ========================================
echo Direct Compilation Mode
echo ========================================
echo.

REM Check for g++ (MinGW)
where g++ >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found g++ compiler. Compiling with g++...
    echo.
    g++ -std=c++11 TestRunner.cpp -o TestRunner.exe -static-libgcc -static-libstdc++ -O2
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ========================================
        echo Compilation successful!
        echo ========================================
        echo.
        echo Executable: %CD%\TestRunner.exe
        if exist TestRunner.exe (
            for %%A in (TestRunner.exe) do echo File size: %%~zA bytes
        )
        echo.
        pause
        exit /b 0
    ) else (
        echo.
        echo Compilation failed with g++
        echo.
        goto :TryMSVC
    )
)

:TryMSVC
REM Check for cl.exe (MSVC)
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found MSVC compiler. Compiling with cl.exe...
    echo.
    cl /EHsc /std:c++14 /O2 /nologo TestRunner.cpp /link /out:TestRunner.exe
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ========================================
        echo Compilation successful!
        echo ========================================
        echo.
        echo Executable: %CD%\TestRunner.exe
        if exist TestRunner.exe (
            for %%A in (TestRunner.exe) do echo File size: %%~zA bytes
        )
        if exist TestRunner.obj del TestRunner.obj >nul 2>nul
        echo.
        pause
        exit /b 0
    ) else (
        echo.
        echo Compilation failed with MSVC
        echo.
        goto :TryVS2022
    )
)

:TryVS2022
REM Try to find and setup Visual Studio 2022
set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if exist "%VCVARS_BAT%" (
    echo Found Visual Studio 2022. Setting up environment...
    echo.
    
    REM Create temporary batch file
    echo @echo off > _temp_compile.bat
    echo call "%VCVARS_BAT%" ^>nul 2^>^&1 >> _temp_compile.bat
    echo cl /EHsc /std:c++14 /O2 /nologo TestRunner.cpp /link /out:TestRunner.exe 2^>^&1 >> _temp_compile.bat
    echo exit /b %%ERRORLEVEL%% >> _temp_compile.bat
    
    call _temp_compile.bat
    set "COMPILE_RESULT=!ERRORLEVEL!"
    
    if exist _temp_compile.bat del _temp_compile.bat >nul 2>nul
    
    if !COMPILE_RESULT! EQU 0 (
        echo.
        echo ========================================
        echo Compilation successful!
        echo ========================================
        echo.
        echo Executable: %CD%\TestRunner.exe
        if exist TestRunner.exe (
            for %%A in (TestRunner.exe) do echo File size: %%~zA bytes
        )
        if exist TestRunner.obj del TestRunner.obj >nul 2>nul
        echo.
        pause
        exit /b 0
    )
)

REM No compiler found
echo ========================================
echo ERROR: No C++ compiler found!
echo ========================================
echo.
echo Please install one of the following:
echo   1. CMake (https://cmake.org/download/)
echo   2. MinGW-w64 g++ (https://www.mingw-w64.org/)
echo   3. Visual Studio (https://visualstudio.microsoft.com/)
echo.
echo Or run from "Developer Command Prompt for VS"
echo.
pause
exit /b 1

