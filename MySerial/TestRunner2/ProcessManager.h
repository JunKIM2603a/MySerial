#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <utility>

#include "Message.h"

namespace TestRunner2 {

struct ProcessHandles {
    PROCESS_INFORMATION processInfo;
    HANDLE stdOutRead;
    HANDLE stdOutWrite;

    ProcessHandles();
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    bool ExecutePlan(const SerialTestConfig& config,
                     std::vector<RunResult>& results,
                     std::string& errorMessage);

private:
    bool ParseComportPairs(const std::string& list,
                           std::vector<std::pair<std::string, std::string>>& outPairs,
                           std::string& errorMessage) const;

    RunResult ExecuteSingleRun(const SerialTestConfig& config,
                               int runIndex,
                               const std::vector<std::pair<std::string, std::string>>& portPairs);

    PortTestResult ExecutePortPair(const SerialTestConfig& config,
                                   const std::pair<std::string, std::string>& portPair);

    bool LaunchProcess(const std::string& cmdline, ProcessHandles& handles);
    void CloseProcessHandles(ProcessHandles& handles);
    std::string CaptureProcessOutput(ProcessHandles& handles);
    TestResult ParseTestSummary(const std::string& output,
                                const std::string& role,
                                const std::string& portName,
                                long long expectedPackets,
                                long long expectedBytes);

    bool WaitForServerReady(ProcessHandles& handles,
                            std::string& accumulatedOutput,
                            int timeoutMs = 10000);
};

} // namespace TestRunner2

