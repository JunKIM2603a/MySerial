#include "ControlClient.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "nlohmann/json.hpp"

namespace TestRunner2 {

using json = nlohmann::json;

ControlClient::ControlClient(std::string serverAddress, int controlPort)
    : m_serverAddress(std::move(serverAddress)),
      m_controlPort(controlPort),
      m_socket(INVALID_SOCKET),
      m_connected(false) {
    InitializeWinsock();
}

ControlClient::~ControlClient() {
    Disconnect();
    WSACleanup();
}

bool ControlClient::InitializeWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "[ControlClient] WSAStartup failed: " << result << std::endl;
        return false;
    }
    return true;
}

bool ControlClient::Connect() {
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "[ControlClient] socket() failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    sockaddr_in clientService{};
    clientService.sin_family = AF_INET;
    clientService.sin_port = htons(static_cast<u_short>(m_controlPort));
    inet_pton(AF_INET, m_serverAddress.c_str(), &clientService.sin_addr);

    if (connect(m_socket, reinterpret_cast<SOCKADDR*>(&clientService), sizeof(clientService)) == SOCKET_ERROR) {
        std::cerr << "[ControlClient] connect() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    m_connected = true;
    std::cout << "[ControlClient] Connected to " << m_serverAddress << ":" << m_controlPort << std::endl;
    return true;
}

void ControlClient::Disconnect() {
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_connected = false;
}

bool ControlClient::SendMessage(const std::string& message) {
    if (!m_connected) {
        return false;
    }
    uint32_t length = htonl(static_cast<uint32_t>(message.size()));
    if (send(m_socket, reinterpret_cast<const char*>(&length), sizeof(length), 0) != sizeof(length)) {
        return false;
    }

    size_t remaining = message.size();
    const char* data = message.data();
    while (remaining > 0) {
        int chunk = send(m_socket, data, static_cast<int>(remaining), 0);
        if (chunk == SOCKET_ERROR) {
            return false;
        }
        data += chunk;
        remaining -= chunk;
    }
    return true;
}

bool ControlClient::ReceiveMessage(std::string& message, int timeoutMs) {
    if (!m_connected) {
        return false;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(m_socket, &readSet);

    TIMEVAL tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ready = select(0, &readSet, nullptr, nullptr, &tv);
    if (ready <= 0) {
        return false;
    }

    uint32_t lengthNetwork = 0;
    int received = recv(m_socket, reinterpret_cast<char*>(&lengthNetwork), sizeof(lengthNetwork), MSG_WAITALL);
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
        int chunk = recv(m_socket, buffer, static_cast<int>(remaining), 0);
        if (chunk <= 0) {
            return false;
        }
        buffer += chunk;
        remaining -= chunk;
    }

    return true;
}

bool ControlClient::Execute(const SerialTestConfig& config) {
    if (!Connect()) {
        return false;
    }

    if (!SendMessage(SerializeConfigRequest(config))) {
        std::cerr << "[ControlClient] Failed to send config request." << std::endl;
        return false;
    }

    std::string message;
    if (!ReceiveMessage(message)) {
        std::cerr << "[ControlClient] Did not receive SERVER_READY." << std::endl;
        return false;
    }

    MessageType type = MessageType::ERROR_MESSAGE;
    try {
        type = PeekMessageType(message);
    } catch (const std::exception& ex) {
        std::cerr << "[ControlClient] Invalid response: " << ex.what() << std::endl;
        return false;
    }

    if (type == MessageType::ERROR_MESSAGE) {
        std::string error;
        if (DeserializeError(message, error)) {
            std::cerr << "[ControlClient] Server error: " << error << std::endl;
        }
        return false;
    }

    if (type != MessageType::SERVER_READY) {
        std::cerr << "[ControlClient] Expected SERVER_READY but received different message." << std::endl;
        return false;
    }

    std::cout << "[ControlClient] Server acknowledged configuration. Waiting for completion..." << std::endl;

    bool testComplete = false;
    bool remoteSuccess = false;
    std::string completionMsg;

    while (!testComplete) {
        if (!ReceiveMessage(message, Protocol::RECV_TIMEOUT_MS * 10)) {
            std::cerr << "[ControlClient] Timed out waiting for TEST_COMPLETE." << std::endl;
            return false;
        }

        try {
            type = PeekMessageType(message);
        } catch (const std::exception& ex) {
            std::cerr << "[ControlClient] Invalid response: " << ex.what() << std::endl;
            return false;
        }

        if (type == MessageType::TEST_COMPLETE) {
            if (!DeserializeTestComplete(message, remoteSuccess, completionMsg)) {
                std::cerr << "[ControlClient] Failed to parse TEST_COMPLETE message." << std::endl;
                return false;
            }
            testComplete = true;
        } else if (type == MessageType::HEARTBEAT) {
            SendMessage(SerializeHeartbeat());
        } else if (type == MessageType::ERROR_MESSAGE) {
            std::string error;
            if (DeserializeError(message, error)) {
                std::cerr << "[ControlClient] Server error: " << error << std::endl;
            }
            return false;
        } else {
            std::cerr << "[ControlClient] Unexpected message while waiting for completion: "
                      << MessageTypeToString(type) << std::endl;
            return false;
        }
    }

    if (!completionMsg.empty()) {
        std::cout << "[ControlClient] Server message: " << completionMsg << std::endl;
    }

    if (!SendMessage(SerializeResultsRequest())) {
        std::cerr << "[ControlClient] Failed to request results." << std::endl;
        return false;
    }

    if (!ReceiveMessage(message, Protocol::RECV_TIMEOUT_MS * 10)) {
        std::cerr << "[ControlClient] Timed out waiting for results response." << std::endl;
        return false;
    }

    try {
        type = PeekMessageType(message);
    } catch (const std::exception& ex) {
        std::cerr << "[ControlClient] Invalid response: " << ex.what() << std::endl;
        return false;
    }

    if (type == MessageType::ERROR_MESSAGE) {
        std::string error;
        if (DeserializeError(message, error)) {
            std::cerr << "[ControlClient] Server error: " << error << std::endl;
        }
        return false;
    }

    if (type != MessageType::RESULTS_RESPONSE) {
        std::cerr << "[ControlClient] Unexpected message type in results exchange." << std::endl;
        return false;
    }

    std::vector<RunResult> runs;
    bool overallSuccess = false;
    if (!DeserializeResultsResponse(message, runs, overallSuccess)) {
        std::cerr << "[ControlClient] Failed to parse results." << std::endl;
        return false;
    }

    PrintRunSummaries(runs, overallSuccess);
    SaveRunReports(runs);
    return overallSuccess;
}

void ControlClient::PrintRunSummaries(const std::vector<RunResult>& runs, bool overallSuccess) {
    for (const auto& run : runs) {
        PrintSingleRun(run);
    }

    std::cout << "==================================================" << std::endl;
    std::cout << (overallSuccess ? "SUCCESS: All runs passed." : "WARNING: Some runs failed.") << std::endl;
    std::cout << "==================================================" << std::endl;
}

void ControlClient::PrintSingleRun(const RunResult& run) const {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "Run " << run.runNumber << " Summary" << std::endl;
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

void ControlClient::SaveRunReports(const std::vector<RunResult>& runs) const {
    for (const auto& run : runs) {
        json runJson;
        runJson["runNumber"] = run.runNumber;
        runJson["success"] = run.success;
        runJson["portResults"] = json::array();

        for (const auto& port : run.portResults) {
            json portJson;
            portJson["serverPort"] = port.serverPort;
            portJson["clientPort"] = port.clientPort;

            auto resultToJson = [](const TestResult& r) {
                return json{
                    {"role", r.role},
                    {"portName", r.portName},
                    {"duration", r.duration},
                    {"throughput", r.throughput},
                    {"cps", r.cps},
                    {"totalBytes", r.totalBytes},
                    {"totalPackets", r.totalPackets},
                    {"expectedBytes", r.expectedBytes},
                    {"expectedPackets", r.expectedPackets},
                    {"sequenceErrors", r.sequenceErrors},
                    {"checksumErrors", r.checksumErrors},
                    {"contentMismatches", r.contentMismatches},
                    {"failureReason", r.failureReason},
                    {"success", r.success}
                };
            };

            portJson["serverResult"] = resultToJson(port.serverResult);
            portJson["clientResult"] = resultToJson(port.clientResult);
            portJson["success"] = port.success;
            portJson["errorMessage"] = port.errorMessage;
            runJson["portResults"].push_back(portJson);
        }

        std::ostringstream filename;
        filename << "TestRunner2_run_" << run.runNumber << ".json";
        std::ofstream file(filename.str());
        if (file.is_open()) {
            file << runJson.dump(2);
        }
    }
}

} // namespace TestRunner2

