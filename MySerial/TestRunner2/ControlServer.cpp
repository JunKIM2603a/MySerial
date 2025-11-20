#include "ControlServer.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <thread>
#include <utility>

namespace TestRunner2 {

ControlServer::ControlServer(int controlPort,
                             std::string serialExecutable)
    : m_controlPort(controlPort),
      m_serialExecutable(std::move(serialExecutable)),
      m_serverSocket(INVALID_SOCKET),
      m_running(false) {
}

ControlServer::~ControlServer() {
    Stop();
}

bool ControlServer::Start() {
    if (!InitializeWinsock()) {
        return false;
    }
    if (!CreateServerSocket()) {
        WSACleanup();
        return false;
    }

    m_running = true;
    std::cout << "[ControlServer] Listening on port " << m_controlPort << std::endl;
    AcceptConnections();
    return true;
}

void ControlServer::Stop() {
    m_running = false;
    if (m_serverSocket != INVALID_SOCKET) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

bool ControlServer::InitializeWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[ControlServer] WSAStartup failed: " << result << std::endl;
        return false;
    }
    return true;
}

bool ControlServer::CreateServerSocket() {
    m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_serverSocket == INVALID_SOCKET) {
        std::cerr << "[ControlServer] socket() failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    SOCKADDR_IN service{};
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = htonl(INADDR_ANY);
    service.sin_port = htons(static_cast<u_short>(m_controlPort));

    if (bind(m_serverSocket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR) {
        std::cerr << "[ControlServer] bind() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    if (listen(m_serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[ControlServer] listen() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        return false;
    }

    return true;
}

void ControlServer::AcceptConnections() {
    while (m_running) {
        SOCKET clientSocket = accept(m_serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            if (m_running) {
                std::cerr << "[ControlServer] accept() failed: " << WSAGetLastError() << std::endl;
            }
            continue;
        }

        std::cout << "[ControlServer] Client connected." << std::endl;
        HandleClient(clientSocket);
        closesocket(clientSocket);
        std::cout << "[ControlServer] Client disconnected." << std::endl;
    }
}

void ControlServer::HandleClient(SOCKET clientSocket) {
    SessionContext ctx;
    ctx.state = SessionState::IDLE;

    auto safeSend = [&](const std::string& payload) {
        std::lock_guard<std::mutex> lock(ctx.sendMutex);
        return SendMessage(clientSocket, payload);
    };

    auto joinWorkerIfNeeded = [&ctx]() {
        if (ctx.workerThread.joinable() && !ctx.workerRunning.load()) {
            ctx.workerThread.join();
        }
    };

    while (m_running) {
        std::string message;
        if (!ReceiveMessage(clientSocket, message)) {
            break;
        }

        MessageType type;
        try {
            type = PeekMessageType(message);
        } catch (const std::exception& ex) {
            std::cerr << "[ControlServer] Invalid message: " << ex.what() << std::endl;
            SendMessage(clientSocket, SerializeError("Invalid message format."));
            continue;
        }

        if (type == MessageType::CONFIG_REQUEST) {
            if (ctx.workerRunning.load()) {
                safeSend(SerializeError("A test is already running. Please wait for completion."));
                continue;
            }

            joinWorkerIfNeeded();

            if (!ProcessConfigMessage(ctx, message)) {
                std::string lastErrorCopy;
                {
                    std::lock_guard<std::mutex> lock(ctx.dataMutex);
                    lastErrorCopy = ctx.lastError;
                }
                safeSend(SerializeError(lastErrorCopy.empty() ? "Failed to start tests." : lastErrorCopy));
                break;
            }
            if (!safeSend(SerializeServerReady())) {
                break;
            }

            std::cout << "[ControlServer] Running SerialCommunicator plan..." << std::endl;
            {
                std::lock_guard<std::mutex> lock(ctx.dataMutex);
                ctx.state = SessionState::RUNNING_TESTS;
                ctx.lastError.clear();
                ctx.runResults.clear();
                ctx.executionSuccess = false;
            }

            ctx.workerRunning = true;
            ctx.workerThread = std::thread([this, &ctx, clientSocket]() {
                SerialTestConfig configCopy;
                {
                    std::lock_guard<std::mutex> lock(ctx.dataMutex);
                    configCopy = ctx.config;
                }

                // 각 Run 완료 시 클라이언트에게 즉시 전송하는 콜백
                auto onRunCompleted = [this, &ctx, clientSocket](const RunResult& runResult) {
                    std::lock_guard<std::mutex> lock(ctx.sendMutex);
                    if (!SendMessage(clientSocket, SerializeRunCompleted(runResult))) {
                        std::cerr << "[ControlServer] Failed to send RUN_COMPLETED message for run " 
                                  << runResult.runNumber << std::endl;
                    }
                };

                std::vector<RunResult> runResults;
                std::string errorMessage;
                bool success = m_processManager.ExecutePlan(configCopy, runResults, errorMessage, onRunCompleted);

                // 서버 측에도 결과 출력
                std::cout << "\n##################################################" << std::endl;
                std::cout << "### SERVER-SIDE RESULTS ###" << std::endl;
                std::cout << "##################################################\n" << std::endl;
                PrintServerResults(runResults, success);

                {
                    std::lock_guard<std::mutex> lock(ctx.dataMutex);
                    ctx.executionSuccess = success;
                    ctx.runResults = std::move(runResults);
                    ctx.lastError = errorMessage;
                    ctx.state = SessionState::READY_FOR_RESULTS;
                }

                {
                    std::lock_guard<std::mutex> lock(ctx.sendMutex);
                    if (!SendMessage(clientSocket, SerializeTestComplete(success, errorMessage))) {
                        std::cerr << "[ControlServer] Failed to send TEST_COMPLETE message." << std::endl;
                    }
                }

                ctx.workerRunning = false;
            });
        } else if (type == MessageType::RESULTS_REQUEST) {
            if (!ProcessResultsRequest(ctx, clientSocket)) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(ctx.dataMutex);
                ctx.state = SessionState::COMPLETED;
            }
        } else if (type == MessageType::HEARTBEAT) {
            safeSend(SerializeHeartbeat());
        } else {
            safeSend(SerializeError("Unsupported message type for server."));
        }
    }

    if (ctx.workerThread.joinable()) {
        ctx.workerThread.join();
    }
}

bool ControlServer::SendMessage(SOCKET socket, const std::string& message) {
    uint32_t length = static_cast<uint32_t>(message.size());
    uint32_t lengthNetwork = htonl(length);

    int sent = send(socket, reinterpret_cast<const char*>(&lengthNetwork), sizeof(lengthNetwork), 0);
    if (sent != sizeof(lengthNetwork)) {
        return false;
    }

    const char* data = message.data();
    size_t remaining = length;
    while (remaining > 0) {
        int chunk = send(socket, data, static_cast<int>(remaining), 0);
        if (chunk == SOCKET_ERROR) {
            return false;
        }
        data += chunk;
        remaining -= chunk;
    }

    return true;
}

bool ControlServer::ReceiveMessage(SOCKET socket, std::string& message, int timeoutMs) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket, &readSet);

    TIMEVAL tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ready = select(0, &readSet, nullptr, nullptr, &tv);
    if (ready <= 0) {
        return false;
    }

    uint32_t lengthNetwork = 0;
    int received = recv(socket, reinterpret_cast<char*>(&lengthNetwork), sizeof(lengthNetwork), MSG_WAITALL);
    if (received != sizeof(lengthNetwork)) {
        return false;
    }
    uint32_t length = ntohl(lengthNetwork);
    if (length > Protocol::MAX_MESSAGE_SIZE) {
        return false;
    }

    message.resize(length);
    char* buffer = &message[0];
    size_t remaining = length;
    while (remaining > 0) {
        int chunk = recv(socket, buffer, static_cast<int>(remaining), 0);
        if (chunk <= 0) {
            return false;
        }
        buffer += chunk;
        remaining -= chunk;
    }

    return true;
}

bool ControlServer::ProcessConfigMessage(SessionContext& ctx, const std::string& payload) {
    SerialTestConfig parsedConfig;
    if (!DeserializeConfigRequest(payload, parsedConfig)) {
        std::lock_guard<std::mutex> lock(ctx.dataMutex);
        ctx.lastError = "Failed to parse configuration.";
        return false;
    }
    if (parsedConfig.serialExecutable.empty()) {
        parsedConfig.serialExecutable = m_serialExecutable;
    }
    {
        std::lock_guard<std::mutex> lock(ctx.dataMutex);
        ctx.config = parsedConfig;
        ctx.state = SessionState::CONFIG_RECEIVED;
    }
    return true;
}

bool ControlServer::ProcessResultsRequest(SessionContext& ctx, SOCKET socket) {
    std::vector<RunResult> resultsCopy;
    bool overallSuccess = false;
    SessionState stateSnapshot;
    {
        std::lock_guard<std::mutex> lock(ctx.dataMutex);
        stateSnapshot = ctx.state;
        resultsCopy = ctx.runResults;
        overallSuccess = ctx.executionSuccess;
    }

    if (stateSnapshot != SessionState::READY_FOR_RESULTS && stateSnapshot != SessionState::COMPLETED) {
        return SendMessage(socket, SerializeError("Results not ready yet."));
    }

    std::lock_guard<std::mutex> lock(ctx.sendMutex);
    return SendMessage(socket, SerializeResultsResponse(resultsCopy, overallSuccess));
}

void ControlServer::PrintServerResults(const std::vector<RunResult>& runs, bool overallSuccess) const {
    // 개별 Run 결과는 이미 ProcessManager에서 출력됨
    // 여기서는 전체 Summary만 출력
    PrintServerOverallSummary(runs, overallSuccess);
}

void ControlServer::PrintServerSingleRun(const RunResult& run) const {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "Run " << run.runNumber << " Summary" << std::endl;
    std::cout << "Start Time: " << run.startTime << std::endl;
    std::cout << "End Time:   " << run.endTime << std::endl;
    std::cout << "Duration:   " << std::fixed << std::setprecision(2) 
              << run.totalDuration << " seconds" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::left << std::setw(10) << "Role"
              << std::setw(16) << "Port"
              << std::setw(15) << "Duration (s)"
              << std::setw(18) << "Throughput (Mbps)"
              << std::setw(16) << "CPS (Bytes/s)"
              << std::setw(16) << "Total Bytes"
              << std::setw(16) << "Total Packets"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(117, '-') << std::endl;

    auto printResult = [](const TestResult& result) {
        auto status = (result.success &&
                       result.totalBytes == result.expectedBytes &&
                       result.totalPackets == result.expectedPackets &&
                       result.sequenceErrors == 0 &&
                       result.checksumErrors == 0 &&
                       result.contentMismatches == 0) ? "PASS" : "FAIL";
        std::cout << std::left << std::setw(10) << result.role
                  << std::setw(16) << result.portName
                  << std::setw(15) << std::fixed << std::setprecision(2) << result.duration
                  << std::setw(18) << std::fixed << std::setprecision(2) << result.throughput
                  << std::setw(16) << std::fixed << std::setprecision(0) << result.cps
                  << std::setw(16) << result.totalBytes
                  << std::setw(16) << result.totalPackets
                  << std::setw(10) << status << std::endl;
        if (std::string(status) == "FAIL" && !result.failureReason.empty()) {
            std::cout << "  -> " << result.failureReason << std::endl;
        }
    };

    for (const auto& port : run.portResults) {
        printResult(port.serverResult);
        printResult(port.clientResult);
        if (!port.errorMessage.empty()) {
            std::cout << "  Pair (" << port.serverPort << "," << port.clientPort
                      << ") error: " << port.errorMessage << std::endl;
        }
    }
}

void ControlServer::PrintServerOverallSummary(const std::vector<RunResult>& runs, bool overallSuccess) const {
    std::cout << "\n##################################################" << std::endl;
    std::cout << "### OVERALL TEST SUMMARY - ALL RUNS ###" << std::endl;
    std::cout << "##################################################\n" << std::endl;
    
    // Run별 요약 테이블
    std::cout << std::left << std::setw(8) << "Run#"
              << std::setw(22) << "Start Time"
              << std::setw(22) << "End Time"
              << std::setw(12) << "Duration(s)"
              << std::setw(12) << "Port Pairs"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(86, '=') << std::endl;
    
    double totalDuration = 0.0;
    int passedRuns = 0;
    
    for (const auto& run : runs) {
        totalDuration += run.totalDuration;
        if (run.success) passedRuns++;
        
        std::cout << std::left << std::setw(8) << run.runNumber
                  << std::setw(22) << run.startTime
                  << std::setw(22) << run.endTime
                  << std::setw(12) << std::fixed << std::setprecision(2) << run.totalDuration
                  << std::setw(12) << run.portResults.size()
                  << std::setw(10) << (run.success ? "PASS" : "FAIL") << std::endl;
    }
    
    std::cout << std::string(86, '=') << std::endl;
    std::cout << "Total Runs: " << runs.size() 
              << " | Passed: " << passedRuns 
              << " | Failed: " << (runs.size() - passedRuns) << std::endl;
    std::cout << "Total Test Duration: " << std::fixed << std::setprecision(2) 
              << totalDuration << " seconds" << std::endl;
    
    std::cout << "\n##################################################" << std::endl;
    std::cout << (overallSuccess ? "### FINAL RESULT: SUCCESS ###" : "### FINAL RESULT: FAILED ###") << std::endl;
    std::cout << "##################################################\n" << std::endl;
}

} // namespace TestRunner2

