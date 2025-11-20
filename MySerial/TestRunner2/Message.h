#pragma once

#include "Protocol.h"
#include <string>
#include <vector>

namespace TestRunner2 {

struct SerialTestConfig {
    int repetitions = 1;
    long long dataSize = 1024;
    long long numPackets = 100;
    int baudrate = 115200;
    bool saveLogs = false;
    std::string comportList; // comma separated
    std::string serialExecutable = "SerialCommunicator.exe";
};

struct TestResult {
    std::string role;
    std::string portName;
    double duration = 0.0;
    double throughput = 0.0;
    double cps = 0.0;
    long long totalBytes = 0;
    long long totalPackets = 0;
    long long expectedBytes = 0;
    long long expectedPackets = 0;
    long long sequenceErrors = 0;
    long long checksumErrors = 0;
    long long contentMismatches = 0;
    int retransmitCount = 0;        // Protocol V2: 재전송 횟수
    double elapsedSeconds = 0.0;    // Protocol V2: 경과 시간
    double throughputMBps = 0.0;    // Protocol V2: 처리량 (MB/s)
    std::string failureReason;
    bool success = false;
};

struct PortTestResult {
    std::string serverPort;
    std::string clientPort;
    TestResult serverResult;
    TestResult clientResult;
    bool success = false;
    std::string errorMessage;
};

struct RunResult {
    int runNumber = 0;
    bool success = false;
    std::vector<PortTestResult> portResults;
    std::string startTime;          // 시험 시작 시간 (예: "2025-11-20 23:15:30")
    std::string endTime;            // 시험 종료 시간
    double totalDuration = 0.0;     // 전체 소요 시간 (초)
};

struct MessageEnvelope {
    MessageType type = MessageType::HEARTBEAT;
    std::string payload;
};

std::string SerializeConfigRequest(const SerialTestConfig& config);
bool DeserializeConfigRequest(const std::string& json, SerialTestConfig& config);

std::string SerializeServerReady();
std::string SerializeTestComplete(bool success, const std::string& message);
bool DeserializeTestComplete(const std::string& json, bool& success, std::string& message);

std::string SerializeResultsRequest();
std::string SerializeResultsResponse(const std::vector<RunResult>& results, bool overallSuccess);
bool DeserializeResultsResponse(const std::string& json,
                                std::vector<RunResult>& results,
                                bool& overallSuccess);

std::string SerializeError(const std::string& message);
bool DeserializeError(const std::string& json, std::string& error);

std::string SerializeHeartbeat();

std::string SerializeRunCompleted(const RunResult& runResult);
bool DeserializeRunCompleted(const std::string& json, RunResult& runResult);

MessageType PeekMessageType(const std::string& json);

} // namespace TestRunner2

