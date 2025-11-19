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

#include "Message.h"
#include <string>
#include <vector>

namespace TestRunner2 {

class ControlClient {
public:
    ControlClient(std::string serverAddress, int controlPort);
    ~ControlClient();

    bool Execute(const SerialTestConfig& config);

private:
    bool InitializeWinsock();
    bool Connect();
    void Disconnect();

    bool SendMessage(const std::string& message);
    bool ReceiveMessage(std::string& message, int timeoutMs = Protocol::RECV_TIMEOUT_MS);

    void PrintRunSummaries(const std::vector<RunResult>& runs, bool overallSuccess);
    void SaveRunReports(const std::vector<RunResult>& runs) const;
    void PrintSingleRun(const RunResult& run) const;

    std::string m_serverAddress;
    int m_controlPort;
    SOCKET m_socket;
    bool m_connected;
};

} // namespace TestRunner2

