#pragma once

#include <string>
#include <stdexcept>

namespace TestRunner2 {

enum class MessageType {
    CONFIG_REQUEST,
    SERVER_READY,
    TEST_COMPLETE,
    RESULTS_REQUEST,
    RESULTS_RESPONSE,
    ERROR_MESSAGE,
    HEARTBEAT
};

inline std::string MessageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::CONFIG_REQUEST: return "CONFIG_REQUEST";
        case MessageType::SERVER_READY: return "SERVER_READY";
        case MessageType::TEST_COMPLETE: return "TEST_COMPLETE";
        case MessageType::RESULTS_REQUEST: return "RESULTS_REQUEST";
        case MessageType::RESULTS_RESPONSE: return "RESULTS_RESPONSE";
        case MessageType::ERROR_MESSAGE: return "ERROR_MESSAGE";
        case MessageType::HEARTBEAT: return "HEARTBEAT";
        default: return "UNKNOWN";
    }
}

inline MessageType StringToMessageType(const std::string& str) {
    if (str == "CONFIG_REQUEST") return MessageType::CONFIG_REQUEST;
    if (str == "SERVER_READY") return MessageType::SERVER_READY;
    if (str == "TEST_COMPLETE") return MessageType::TEST_COMPLETE;
    if (str == "RESULTS_REQUEST") return MessageType::RESULTS_REQUEST;
    if (str == "RESULTS_RESPONSE") return MessageType::RESULTS_RESPONSE;
    if (str == "ERROR_MESSAGE") return MessageType::ERROR_MESSAGE;
    if (str == "HEARTBEAT") return MessageType::HEARTBEAT;
    throw std::runtime_error("Unknown message type: " + str);
}

namespace Protocol {
    constexpr int DEFAULT_CONTROL_PORT = 9001;
    constexpr int MAX_MESSAGE_SIZE = 65536;
    constexpr int HEARTBEAT_INTERVAL_MS = 5000;
    constexpr int RECV_TIMEOUT_MS = 30000;
}

enum class SessionState {
    IDLE,
    CONFIG_RECEIVED,
    RUNNING_TESTS,
    READY_FOR_RESULTS,
    COMPLETED,
    ERROR_STATE
};

inline std::string SessionStateToString(SessionState state) {
    switch (state) {
        case SessionState::IDLE: return "IDLE";
        case SessionState::CONFIG_RECEIVED: return "CONFIG_RECEIVED";
        case SessionState::RUNNING_TESTS: return "RUNNING_TESTS";
        case SessionState::READY_FOR_RESULTS: return "READY_FOR_RESULTS";
        case SessionState::COMPLETED: return "COMPLETED";
        case SessionState::ERROR_STATE: return "ERROR_STATE";
        default: return "UNKNOWN";
    }
}

} // namespace TestRunner2

