#include <iostream>
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <conio.h>
#include <queue>
#include <mutex>
#include <condition_variable>

// An atomic variable to safely signal termination between threads
std::atomic<bool> quit(false);

// A thread-safe queue for storing log messages
std::queue<std::string> logQueue;
std::mutex queueMutex;
std::condition_variable queueCondVar;


void ConnectPipeThread(const std::string& pipeName) {
    HANDLE hPipe;

    while (!quit) {
        if (!WaitNamedPipeA(pipeName.c_str(), 1000)) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND && !quit) {
                std::cout << "Consumer: Pipe '" << pipeName << "' not found. Retrying in 1s..." << std::endl;
                Sleep(1000);
                continue;
            } else if (quit) {
                break;
            } else {
                std::cout << "Error waiting for pipe: " << GetLastError() << std::endl;
                break;
            }
        }

        hPipe = CreateFileA(
            pipeName.c_str(),
            GENERIC_READ,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) {
                std::cout << "Consumer: Pipe is busy. Retrying..." << std::endl;
                WaitNamedPipeA(pipeName.c_str(), 5000);
                continue;
            }
            std::cout << "Error connecting to pipe: " << GetLastError() << std::endl;
            Sleep(1000);
            continue;
        }

        std::cout << "Connected to pipe. Reading data..." << std::endl;

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
            std::cout << "Consumer: Pipe is closed by the producer. Reconnecting..." << std::endl;
        } else if (GetLastError() != 0) {
            std::cout << "Error reading from pipe: " << GetLastError() << std::endl;
        }

        CloseHandle(hPipe);
    }

    std::cout << "Pipe thread exiting." << std::endl;
}

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
    std::cout << "Log processing thread exiting." << std::endl;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: consumer.exe <mode> <Port>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    std::string port = argv[2];
    std::string pipeName = "\\\\.\\pipe\\MySerial_" + mode + "_" + port;

    std::cout << __LINE__ << std::endl;
    std::cout << "Info: Named pipe '" + pipeName + "' created. Waiting for a client to connect..." << std::endl;
    std::thread pipeThread(ConnectPipeThread, pipeName);
    std::thread logThread(LogProcessingThread);

    std::cout << "Consumer is running. Press 'q' or 'Q' to exit." << std::endl;

    while (true) {
        // MSVC와 MinGW/GCC 간의 호환성을 위한 조건부 컴파일
        #if defined(_MSC_VER)
            if (_kbhit()) {
                char ch = _getch();
        #else
            if (kbhit()) {
                char ch = getch();
        #endif
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

    std::cout << "Consumer application has been gracefully shut down." << std::endl;
    return 0;
}
