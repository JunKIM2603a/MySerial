#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include "ProcessManager.h"
#include <atomic>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

namespace TestRunner2 {

class ControlServer {
public:
    ControlServer(int controlPort,
                  std::string serialExecutable);
    ~ControlServer();

    bool Start();
    void Stop();

private:
    struct SessionContext {
        SessionState state = SessionState::IDLE;
        SerialTestConfig config;
        std::vector<RunResult> runResults;
        bool executionSuccess = false;
        std::string lastError;
        std::thread workerThread;
        std::atomic<bool> workerRunning{false};
        std::mutex dataMutex;
        std::mutex sendMutex;
    };

    bool InitializeWinsock();
    bool CreateServerSocket();
    void AcceptConnections();
    void HandleClient(SOCKET clientSocket);

    bool SendMessage(SOCKET socket, const std::string& message);
    bool ReceiveMessage(SOCKET socket, std::string& message, int timeoutMs = Protocol::RECV_TIMEOUT_MS);

    bool ProcessConfigMessage(SessionContext& ctx, const std::string& payload);
    bool ProcessResultsRequest(SessionContext& ctx, SOCKET socket);

    int m_controlPort;
    std::string m_serialExecutable;
    SOCKET m_serverSocket;
    std::atomic<bool> m_running;
    ProcessManager m_processManager;
};

} // namespace TestRunner2

