#include <iostream>
#include <windows.h>
#include <string>
#include <tchar.h>
#include <vector>
#include <thread>
#include <atomic>
#include <conio.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <codecvt>
#include <locale>


#ifdef UNICODE
#define tcout std::wcout
#define tstring std::wstring
#else
#define tcout std::cout
#define tstring std::string
#endif

// An atomic variable to safely signal termination between threads
std::atomic<bool> quit(false);

// A thread-safe queue for storing log messages
std::queue<std::string> logQueue;
std::mutex queueMutex;
std::condition_variable queueCondVar;

// Thread function responsible for pipe connection and data reception
void ConnectPipeThread(const tstring& pipeName) {
    HANDLE hPipe;

    while (!quit) {
        if (!WaitNamedPipe(pipeName.c_str(), 1000)) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND && !quit) {
                tcout << TEXT("Consumer: Pipe '") << pipeName << TEXT("' not found. Retrying in 1s...") << std::endl;
                Sleep(1000);
                continue;
            } else if (quit) {
                break;
            } else {
                tcout << TEXT("Error waiting for pipe: ") << GetLastError() << std::endl;
                break;
            }
        }

        hPipe = CreateFile(
            pipeName.c_str(),
            GENERIC_READ,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) {
                tcout << TEXT("Consumer: Pipe is busy. Retrying...") << std::endl;
                WaitNamedPipe(pipeName.c_str(), 5000); 
                continue;
            }
            tcout << TEXT("Error connecting to pipe: ") << GetLastError() << std::endl;
            Sleep(1000);
            continue;
        }

        tcout << TEXT("Connected to pipe. Reading data...") << std::endl;

        char buffer[1024];
        DWORD bytesRead = 0;

        while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                logQueue.push(buffer);
            }
            queueCondVar.notify_one();
        }

        if (GetLastError() == ERROR_BROKEN_PIPE) {
            tcout << TEXT("Consumer: Pipe is closed by the producer. Reconnecting...") << std::endl;
        } else if (GetLastError() != 0) {
            tcout << TEXT("Error reading from pipe: ") << GetLastError() << std::endl;
        }

        CloseHandle(hPipe);
    }

    tcout << TEXT("Pipe thread exiting.") << std::endl;
}

// Thread function for processing and displaying log messages
void LogProcessingThread() {
    while (!quit || !logQueue.empty()) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCondVar.wait(lock, [] { return !logQueue.empty() || quit; });

        while (!logQueue.empty()) {
            std::string message = logQueue.front();
            logQueue.pop();
            lock.unlock();
            std::cout << "Log Processor: " << message << std::endl;
            lock.lock();
        }
    }
    tcout << TEXT("Log processing thread exiting.") << std::endl;
}

int _tmain(int argc, TCHAR* argv[]) {
    if (argc < 3) {
        tcout << TEXT("Usage: consumer.exe <mode> <Port>") << std::endl;
        return 1;
    }

    tstring mode = argv[1];
    tstring port = argv[2];
    tstring pipeName = TEXT("\\\\.\\pipe\\MySerial_") + mode + TEXT("_") + port;
        
    // std::wstring -> std::string 변환
    using convert_type = std::codecvt_utf8<wchar_t>;
    // std::wstring_convert 객체 생성
    std::wstring_convert<convert_type, wchar_t> converter;
    // std::string으로 변환
    std::string str = converter.to_bytes(pipeName);

    std::cout << __LINE__ << std::endl;
    std::cout << "Info: Named pipe '" + str + "' created. Waiting for a client to connect..." << std::endl;
    std::thread pipeThread(ConnectPipeThread, pipeName);
    std::thread logThread(LogProcessingThread);

    tcout << TEXT("Consumer is running. Press 'q' or 'Q' to exit.") << std::endl;

    while (true) {
        if (_kbhit()) {
            TCHAR ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                quit = true;
                queueCondVar.notify_all(); 
                break;
            }
        }
        Sleep(100);
    }

    pipeThread.join();
    logThread.join();

    tcout << TEXT("Consumer application has been gracefully shut down.") << std::endl;
    return 0;
}