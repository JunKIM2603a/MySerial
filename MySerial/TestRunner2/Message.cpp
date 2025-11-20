#include "Message.h"
#include "nlohmann/json.hpp"
#include <stdexcept>

namespace TestRunner2 {

using json = nlohmann::json;

static json TestResultToJson(const TestResult& result) {
    return {
        {"role", result.role},
        {"portName", result.portName},
        {"duration", result.duration},
        {"throughput", result.throughput},
        {"cps", result.cps},
        {"totalBytes", result.totalBytes},
        {"totalPackets", result.totalPackets},
        {"expectedBytes", result.expectedBytes},
        {"expectedPackets", result.expectedPackets},
        {"sequenceErrors", result.sequenceErrors},
        {"checksumErrors", result.checksumErrors},
        {"contentMismatches", result.contentMismatches},
        {"retransmitCount", result.retransmitCount},           // Protocol V2
        {"elapsedSeconds", result.elapsedSeconds},            // Protocol V2
        {"throughputMBps", result.throughputMBps},            // Protocol V2
        {"failureReason", result.failureReason},
        {"success", result.success}
    };
}

static TestResult JsonToTestResult(const json& j) {
    TestResult r;
    r.role = j.value("role", "");
    r.portName = j.value("portName", "");
    r.duration = j.value("duration", 0.0);
    r.throughput = j.value("throughput", 0.0);
    r.cps = j.value("cps", 0.0);
    r.totalBytes = j.value("totalBytes", 0LL);
    r.totalPackets = j.value("totalPackets", 0LL);
    r.expectedBytes = j.value("expectedBytes", 0LL);
    r.expectedPackets = j.value("expectedPackets", 0LL);
    r.sequenceErrors = j.value("sequenceErrors", 0LL);
    r.checksumErrors = j.value("checksumErrors", 0LL);
    r.contentMismatches = j.value("contentMismatches", 0LL);
    r.retransmitCount = j.value("retransmitCount", 0);        // Protocol V2
    r.elapsedSeconds = j.value("elapsedSeconds", 0.0);       // Protocol V2
    r.throughputMBps = j.value("throughputMBps", 0.0);       // Protocol V2
    r.failureReason = j.value("failureReason", "");
    r.success = j.value("success", false);
    return r;
}

static json PortResultToJson(const PortTestResult& result) {
    return {
        {"serverPort", result.serverPort},
        {"clientPort", result.clientPort},
        {"serverResult", TestResultToJson(result.serverResult)},
        {"clientResult", TestResultToJson(result.clientResult)},
        {"success", result.success},
        {"errorMessage", result.errorMessage}
    };
}

static PortTestResult JsonToPortResult(const json& j) {
    PortTestResult r;
    r.serverPort = j.value("serverPort", "");
    r.clientPort = j.value("clientPort", "");
    if (j.contains("serverResult")) {
        r.serverResult = JsonToTestResult(j["serverResult"]);
    }
    if (j.contains("clientResult")) {
        r.clientResult = JsonToTestResult(j["clientResult"]);
    }
    r.success = j.value("success", false);
    r.errorMessage = j.value("errorMessage", "");
    return r;
}

std::string SerializeConfigRequest(const SerialTestConfig& config) {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::CONFIG_REQUEST);
    j["config"] = {
        {"repetitions", config.repetitions},
        {"dataSize", config.dataSize},
        {"numPackets", config.numPackets},
        {"baudrate", config.baudrate},
        {"saveLogs", config.saveLogs},
        {"comports", config.comportList},
        {"serialExecutable", config.serialExecutable}
    };
    return j.dump();
}

bool DeserializeConfigRequest(const std::string& text, SerialTestConfig& config) {
    try {
        auto j = json::parse(text);
        if (StringToMessageType(j.value("messageType", "")) != MessageType::CONFIG_REQUEST) {
            return false;
        }
        auto cfg = j.at("config");
        config.repetitions = cfg.value("repetitions", 1);
        config.dataSize = cfg.value("dataSize", 1024LL);
        config.numPackets = cfg.value("numPackets", 100LL);
        config.baudrate = cfg.value("baudrate", 115200);
        config.saveLogs = cfg.value("saveLogs", false);
        config.comportList = cfg.value("comports", "");
        config.serialExecutable = cfg.value("serialExecutable", "SerialCommunicator.exe");
        return true;
    } catch (...) {
        return false;
    }
}

std::string SerializeServerReady() {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::SERVER_READY);
    return j.dump();
}

std::string SerializeTestComplete(bool success, const std::string& message) {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::TEST_COMPLETE);
    j["success"] = success;
    j["message"] = message;
    return j.dump();
}

bool DeserializeTestComplete(const std::string& text, bool& success, std::string& message) {
    try {
        auto j = json::parse(text);
        if (StringToMessageType(j.value("messageType", "")) != MessageType::TEST_COMPLETE) {
            return false;
        }
        success = j.value("success", false);
        message = j.value("message", "");
        return true;
    } catch (...) {
        return false;
    }
}

std::string SerializeResultsRequest() {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::RESULTS_REQUEST);
    return j.dump();
}

std::string SerializeResultsResponse(const std::vector<RunResult>& results, bool overallSuccess) {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::RESULTS_RESPONSE);
    j["overallSuccess"] = overallSuccess;
    j["runs"] = json::array();
    for (const auto& run : results) {
        json runJson;
        runJson["runNumber"] = run.runNumber;
        runJson["success"] = run.success;
        runJson["startTime"] = run.startTime;
        runJson["endTime"] = run.endTime;
        runJson["totalDuration"] = run.totalDuration;
        runJson["portResults"] = json::array();
        for (const auto& port : run.portResults) {
            runJson["portResults"].push_back(PortResultToJson(port));
        }
        j["runs"].push_back(runJson);
    }
    return j.dump();
}

bool DeserializeResultsResponse(const std::string& text,
                                std::vector<RunResult>& results,
                                bool& overallSuccess) {
    try {
        auto j = json::parse(text);
        if (StringToMessageType(j.value("messageType", "")) != MessageType::RESULTS_RESPONSE) {
            return false;
        }
        overallSuccess = j.value("overallSuccess", false);
        results.clear();
        if (j.contains("runs") && j["runs"].is_array()) {
            for (const auto& runJson : j["runs"]) {
                RunResult run;
                run.runNumber = runJson.value("runNumber", 0);
                run.success = runJson.value("success", false);
                run.startTime = runJson.value("startTime", "");
                run.endTime = runJson.value("endTime", "");
                run.totalDuration = runJson.value("totalDuration", 0.0);
                if (runJson.contains("portResults")) {
                    for (const auto& portJson : runJson["portResults"]) {
                        run.portResults.push_back(JsonToPortResult(portJson));
                    }
                }
                results.push_back(std::move(run));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string SerializeError(const std::string& message) {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::ERROR_MESSAGE);
    j["error"] = message;
    return j.dump();
}

bool DeserializeError(const std::string& text, std::string& error) {
    try {
        auto j = json::parse(text);
        if (StringToMessageType(j.value("messageType", "")) != MessageType::ERROR_MESSAGE) {
            return false;
        }
        error = j.value("error", "");
        return true;
    } catch (...) {
        return false;
    }
}

std::string SerializeHeartbeat() {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::HEARTBEAT);
    return j.dump();
}

std::string SerializeRunCompleted(const RunResult& runResult) {
    json j;
    j["messageType"] = MessageTypeToString(MessageType::RUN_COMPLETED);
    j["runNumber"] = runResult.runNumber;
    j["success"] = runResult.success;
    j["startTime"] = runResult.startTime;
    j["endTime"] = runResult.endTime;
    j["totalDuration"] = runResult.totalDuration;
    j["portResults"] = json::array();
    for (const auto& port : runResult.portResults) {
        j["portResults"].push_back(PortResultToJson(port));
    }
    return j.dump();
}

bool DeserializeRunCompleted(const std::string& text, RunResult& runResult) {
    try {
        auto j = json::parse(text);
        if (StringToMessageType(j.value("messageType", "")) != MessageType::RUN_COMPLETED) {
            return false;
        }
        runResult.runNumber = j.value("runNumber", 0);
        runResult.success = j.value("success", false);
        runResult.startTime = j.value("startTime", "");
        runResult.endTime = j.value("endTime", "");
        runResult.totalDuration = j.value("totalDuration", 0.0);
        runResult.portResults.clear();
        if (j.contains("portResults") && j["portResults"].is_array()) {
            for (const auto& portJson : j["portResults"]) {
                runResult.portResults.push_back(JsonToPortResult(portJson));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

MessageType PeekMessageType(const std::string& text) {
    auto j = json::parse(text);
    return StringToMessageType(j.value("messageType", ""));
}

} // namespace TestRunner2

