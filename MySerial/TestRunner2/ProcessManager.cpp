#include "ProcessManager.h"

#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>
#include <vector>

namespace TestRunner2 {

namespace {

std::string Trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool IsProcessRunning(HANDLE processHandle) {
    DWORD exitCode = 0;
    if (GetExitCodeProcess(processHandle, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

void TerminateProcessIfRunning(ProcessHandles& handles) {
    if (handles.processInfo.hProcess && IsProcessRunning(handles.processInfo.hProcess)) {
        ::TerminateProcess(handles.processInfo.hProcess, 1);
        WaitForSingleObject(handles.processInfo.hProcess, 5000);
    }
}

} // namespace

ProcessHandles::ProcessHandles() : stdOutRead(NULL), stdOutWrite(NULL) {
    ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
}

ProcessManager::ProcessManager() = default;
ProcessManager::~ProcessManager() = default;

bool ProcessManager::ExecutePlan(const SerialTestConfig& config,
                                 std::vector<RunResult>& results,
                                 std::string& errorMessage) {
    std::vector<std::pair<std::string, std::string>> portPairs;
    if (!ParseComportPairs(config.comportList, portPairs, errorMessage)) {
        return false;
    }

    if (config.numPackets == 0) {
        errorMessage = "numPackets cannot be zero (infinite mode not supported).";
        return false;
    }

    bool overallSuccess = true;
    results.clear();

    for (int run = 1; run <= config.repetitions; ++run) {
        std::cout << "==================================================" << std::endl;
        std::cout << "Starting run " << run << " of " << config.repetitions << std::endl;
        std::cout << "==================================================" << std::endl;

        RunResult runResult = ExecuteSingleRun(config, run, portPairs);
        results.push_back(runResult);
        overallSuccess = overallSuccess && runResult.success;

        if (run < config.repetitions) {
            std::cout << "Waiting 3 seconds before next run..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }

    return overallSuccess;
}

bool ProcessManager::ParseComportPairs(const std::string& list,
                                       std::vector<std::pair<std::string, std::string>>& outPairs,
                                       std::string& errorMessage) const {
    outPairs.clear();
    std::vector<std::string> tokens;
    std::stringstream ss(list);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = Trim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    if (tokens.empty() || tokens.size() % 2 != 0) {
        errorMessage = "COM ports must be specified in pairs (server,client,server,client, ...).";
        return false;
    }

    for (size_t i = 0; i < tokens.size(); i += 2) {
        outPairs.emplace_back(tokens[i], tokens[i + 1]);
    }
    return true;
}

RunResult ProcessManager::ExecuteSingleRun(const SerialTestConfig& config,
                                           int runIndex,
                                           const std::vector<std::pair<std::string, std::string>>& portPairs) {
    RunResult runResult;
    runResult.runNumber = runIndex;
    runResult.portResults.resize(portPairs.size());

    std::vector<std::thread> threads;

    for (size_t i = 0; i < portPairs.size(); ++i) {
        threads.emplace_back([this, &config, &portPairs, i, &runResult]() {
            runResult.portResults[i] = ExecutePortPair(config, portPairs[i]);
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    runResult.success = true;
    for (const auto& portResult : runResult.portResults) {
        runResult.success = runResult.success && portResult.success;
    }

    return runResult;
}

PortTestResult ProcessManager::ExecutePortPair(const SerialTestConfig& config,
                                               const std::pair<std::string, std::string>& portPair) {
    PortTestResult result;
    result.serverPort = portPair.first;
    result.clientPort = portPair.second;

    const long long expectedBytes = (config.dataSize + 6) * config.numPackets;
    const long long expectedPackets = config.numPackets;

    std::stringstream serverCmd;
    serverCmd << "\"" << config.serialExecutable << "\""
              << " server " << portPair.first << " " << config.baudrate;

    std::stringstream clientCmd;
    clientCmd << "\"" << config.serialExecutable << "\""
              << " client " << portPair.second << " " << config.baudrate
              << " " << config.dataSize << " " << config.numPackets;

    ProcessHandles serverHandles;
    if (!LaunchProcess(serverCmd.str(), serverHandles)) {
        result.success = false;
        result.errorMessage = "Failed to launch server process for " + portPair.first;
        return result;
    }

    std::string serverOutput;
    if (!WaitForServerReady(serverHandles, serverOutput)) {
        TerminateProcessIfRunning(serverHandles);
        serverOutput += "\n[TestRunner2] Server failed to enter ready state.";
        result.serverResult = ParseTestSummary(serverOutput,
                                               "Server",
                                               portPair.first,
                                               expectedPackets,
                                               expectedBytes);
        result.clientResult.success = false;
        result.clientResult.failureReason = "Server not ready.";
        result.success = false;
        CloseProcessHandles(serverHandles);
        return result;
    }

    std::cout << "[ProcessManager] Server on " << portPair.first
              << " ready. Launching client on " << portPair.second << std::endl;

    ProcessHandles clientHandles;
    std::string clientOutput;
    if (!LaunchProcess(clientCmd.str(), clientHandles)) {
        TerminateProcessIfRunning(serverHandles);
        CloseProcessHandles(serverHandles);
        result.success = false;
        result.errorMessage = "Failed to launch client process for " + portPair.second;
        return result;
    }

    // Calculate timeout for both processes
    double timeoutSec = 30.0;
    if (config.baudrate > 0 && config.numPackets > 0) {
        long long bytesPerFrame = config.dataSize + 6;
        long long totalBytes = bytesPerFrame * config.numPackets * 2;
        double estimatedSec = (totalBytes * 10.0 / static_cast<double>(config.baudrate)) * 1.5;
        if (estimatedSec > timeoutSec) {
            timeoutSec = (std::min)(estimatedSec, 600.0);
        }
    }
    const auto timeout = std::chrono::milliseconds(static_cast<long long>(timeoutSec * 1000.0));

    // Start timing for duration/throughput calculation
    auto testStartTime = std::chrono::high_resolution_clock::now();
    auto startMonitoring = std::chrono::steady_clock::now();

    // Monitor both client and server output simultaneously
    bool clientFinished = false;
    bool serverFinished = false;

    while ((!clientFinished || !serverFinished) &&
           (std::chrono::steady_clock::now() - startMonitoring < timeout)) {
        
        // Read client output if still running
        if (!clientFinished) {
            DWORD bytesAvailable = 0;
            if (PeekNamedPipe(clientHandles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
                CHAR buffer[4096];
                DWORD bytesRead = 0;
                if (ReadFile(clientHandles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                    buffer[bytesRead] = '\0';
                    clientOutput.append(buffer, bytesRead);
                }
            }

            DWORD exitCode = 0;
            if (GetExitCodeProcess(clientHandles.processInfo.hProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    clientFinished = true;
                }
            }
        }

        // Read server output if still running
        if (!serverFinished) {
            DWORD bytesAvailable = 0;
            if (PeekNamedPipe(serverHandles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
                CHAR buffer[4096];
                DWORD bytesRead = 0;
                if (ReadFile(serverHandles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                    buffer[bytesRead] = '\0';
                    serverOutput.append(buffer, bytesRead);
                }
            }

            DWORD exitCode = 0;
            if (GetExitCodeProcess(serverHandles.processInfo.hProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    serverFinished = true;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    auto testEndTime = std::chrono::high_resolution_clock::now();
    double durationSec = std::chrono::duration<double>(testEndTime - testStartTime).count();

    // Read remaining output from both pipes
    DWORD bytesAvailable = 0;
    while (PeekNamedPipe(clientHandles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
        CHAR buffer[4096];
        DWORD bytesRead = 0;
        if (ReadFile(clientHandles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            clientOutput.append(buffer, bytesRead);
        } else {
            break;
        }
    }

    bytesAvailable = 0;
    while (PeekNamedPipe(serverHandles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
        CHAR buffer[4096];
        DWORD bytesRead = 0;
        if (ReadFile(serverHandles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            serverOutput.append(buffer, bytesRead);
        } else {
            break;
        }
    }

    // Terminate processes if they didn't finish
    if (!clientFinished) {
        clientOutput += "\n[TestRunner2] Client timed out and was terminated.";
        TerminateProcessIfRunning(clientHandles);
    }

    if (!serverFinished) {
        serverOutput += "\n[TestRunner2] Server timed out and was terminated.";
        TerminateProcessIfRunning(serverHandles);
    }

    // Wait for processes to fully terminate
    WaitForSingleObject(clientHandles.processInfo.hProcess, 1000);
    WaitForSingleObject(serverHandles.processInfo.hProcess, 1000);

    // Clean up handles
    CloseProcessHandles(clientHandles);
    CloseProcessHandles(serverHandles);

    result.serverResult = ParseTestSummary(serverOutput,
                                           "Server",
                                           portPair.first,
                                           expectedPackets,
                                           expectedBytes);
    result.clientResult = ParseTestSummary(clientOutput,
                                           "Client",
                                           portPair.second,
                                           expectedPackets,
                                           expectedBytes);

    // Set duration for both server and client (they share the same execution window)
    result.serverResult.duration = durationSec;
    result.clientResult.duration = durationSec;

    // Calculate throughput (Mbps) = (bits / 1,000,000) / seconds
    // Calculate CPS (Characters/Bytes Per Second)
    if (durationSec > 0.001) {  // Avoid divide-by-zero
        double serverBits = result.serverResult.totalBytes * 8.0;
        result.serverResult.throughput = (serverBits / 1000000.0) / durationSec;

        double clientBits = result.clientResult.totalBytes * 8.0;
        result.clientResult.throughput = (clientBits / 1000000.0) / durationSec;

        // Calculate CPS (Bytes Per Second)
        result.serverResult.cps = result.serverResult.totalBytes / durationSec;
        result.clientResult.cps = result.clientResult.totalBytes / durationSec;
    } else {
        result.serverResult.throughput = 0.0;
        result.clientResult.throughput = 0.0;
        result.serverResult.cps = 0.0;
        result.clientResult.cps = 0.0;
    }

    auto validate = [](const TestResult& r) {
        bool countsMatch = (r.totalBytes == r.expectedBytes) && (r.totalPackets == r.expectedPackets);
        bool noErrors = (r.sequenceErrors == 0 && r.checksumErrors == 0 && r.contentMismatches == 0);
        return r.success && countsMatch && noErrors;
    };

    result.success = validate(result.serverResult) && validate(result.clientResult);
    if (!result.success && result.errorMessage.empty()) {
        result.errorMessage = "Validation failed for " + result.serverPort + "/" + result.clientPort;
    }

    return result;
}

bool ProcessManager::LaunchProcess(const std::string& cmdline, ProcessHandles& handles) {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&handles.stdOutRead, &handles.stdOutWrite, &saAttr, 65536)) {
        std::cerr << "[ProcessManager] CreatePipe failed" << std::endl;
        return false;
    }
    if (!SetHandleInformation(handles.stdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "[ProcessManager] SetHandleInformation failed" << std::endl;
        CloseHandle(handles.stdOutRead);
        CloseHandle(handles.stdOutWrite);
        handles.stdOutRead = handles.stdOutWrite = NULL;
        return false;
    }

    STARTUPINFOA siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
    siStartInfo.cb = sizeof(STARTUPINFOA);
    siStartInfo.hStdError = handles.stdOutWrite;
    siStartInfo.hStdOutput = handles.stdOutWrite;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::vector<char> cmdlineWritable(cmdline.begin(), cmdline.end());
    cmdlineWritable.push_back('\0');

    BOOL bSuccess = CreateProcessA(
        NULL,
        cmdlineWritable.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &siStartInfo,
        &handles.processInfo);

    CloseHandle(handles.stdOutWrite);
    handles.stdOutWrite = NULL;

    if (!bSuccess) {
        DWORD err = GetLastError();
        std::cerr << "[ProcessManager] CreateProcess failed (" << err
                  << ") cmd: " << cmdline << std::endl;
        CloseProcessHandles(handles);
        return false;
    }

    return true;
}

void ProcessManager::CloseProcessHandles(ProcessHandles& handles) {
    if (handles.processInfo.hProcess) {
        CloseHandle(handles.processInfo.hProcess);
        handles.processInfo.hProcess = NULL;
    }
    if (handles.processInfo.hThread) {
        CloseHandle(handles.processInfo.hThread);
        handles.processInfo.hThread = NULL;
    }
    if (handles.stdOutRead) {
        CloseHandle(handles.stdOutRead);
        handles.stdOutRead = NULL;
    }
    if (handles.stdOutWrite) {
        CloseHandle(handles.stdOutWrite);
        handles.stdOutWrite = NULL;
    }
}

std::string ProcessManager::CaptureProcessOutput(ProcessHandles& handles) {
    std::string output;
    DWORD dwRead = 0;
    CHAR chBuf[4096];

    while (true) {
        bool dataRead = false;

        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(handles.stdOutRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL)) {
                chBuf[dwRead] = '\0';
                output.append(chBuf, dwRead);
                dataRead = true;
                continue;
            } else {
                break;
            }
        }

        if (!dataRead) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(handles.processInfo.hProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    while (true) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(handles.stdOutRead, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead > 0) {
                output.append(chBuf, dwRead);
            } else {
                break;
            }
        } else {
            break;
        }
    }

    WaitForSingleObject(handles.processInfo.hProcess, INFINITE);
    return output;
}

TestResult ProcessManager::ParseTestSummary(const std::string& output,
                                            const std::string& role,
                                            const std::string& portName,
                                            long long expectedPackets,
                                            long long expectedBytes) {
    TestResult result;
    result.role = role;
    result.portName = portName;
    result.expectedPackets = expectedPackets;
    result.expectedBytes = expectedBytes;

    if (output.empty()) {
        result.success = false;
        result.failureReason = "No output captured from process";
        return result;
    }

    // Protocol V2: Parse new format
    std::regex summaryRegex;
    std::regex retransmitRegex;
    
    if (role == "Server") {
        summaryRegex = std::regex(
            "=== Final Server Report ==="
            "[\\s\\S]*?"
            "Server Reception Results:"
            "[\\s\\S]*?"
            "- Received frames: (\\d+)/(\\d+)"
            "[\\s\\S]*?"
            "- Total bytes: (\\d+)"
            "[\\s\\S]*?"
            "- Errors: (\\d+)"
            "[\\s\\S]*?"
            "- Elapsed time: ([\\d.]+) seconds"
            "[\\s\\S]*?"
            "- Throughput: ([\\d.]+) MB/s"
            "[\\s\\S]*?"
            "- CPS \\(chars/sec\\): ([\\d.]+)");
        retransmitRegex = std::regex(
            "Server Transmission Results:"
            "[\\s\\S]*?"
            "- Retransmissions: (\\d+)");
    } else {
        summaryRegex = std::regex(
            "=== Final Client Report ==="
            "[\\s\\S]*?"
            "Client Reception Results:"
            "[\\s\\S]*?"
            "- Received frames: (\\d+)/(\\d+)"
            "[\\s\\S]*?"
            "- Total bytes: (\\d+)"
            "[\\s\\S]*?"
            "- Errors: (\\d+)"
            "[\\s\\S]*?"
            "- Elapsed time: ([\\d.]+) seconds"
            "[\\s\\S]*?"
            "- Throughput: ([\\d.]+) MB/s"
            "[\\s\\S]*?"
            "- CPS \\(chars/sec\\): ([\\d.]+)");
        retransmitRegex = std::regex(
            "Client Transmission Results:"
            "[\\s\\S]*?"
            "- Retransmissions: (\\d+)");
    }

    std::smatch matches;
    if (std::regex_search(output, matches, summaryRegex) && matches.size() == 8) {
        try {
            result.totalPackets = std::stoll(matches[1].str());
            // matches[2] is expected frames (ignored)
            result.totalBytes = std::stoll(matches[3].str());
            result.contentMismatches = std::stoll(matches[4].str());
            result.elapsedSeconds = std::stod(matches[5].str());
            result.throughputMBps = std::stod(matches[6].str());
            result.cps = std::stod(matches[7].str());
            
            // Parse retransmit count
            std::smatch retransmitMatches;
            if (std::regex_search(output, retransmitMatches, retransmitRegex) && retransmitMatches.size() == 2) {
                result.retransmitCount = std::stoi(retransmitMatches[1].str());
            }
            
            result.sequenceErrors = 0;
            result.checksumErrors = 0;
            result.duration = result.elapsedSeconds;
            result.throughput = result.throughputMBps * 8.0; // MB/s to Mbps
            result.success = true;
        } catch (const std::exception& e) {
            result.success = false;
            result.failureReason = "Parse error: " + std::string(e.what());
        }
    } else {
        result.success = false;
        if (output.find("Final") == std::string::npos && output.find("Report") == std::string::npos) {
            result.failureReason = "Final report not found. Process may have exited early.";
        } else {
            result.failureReason = "Unable to parse Protocol V2 final report. Expected '=== Final " + role + " Report ===' format.";
        }
    }

    return result;
}

bool ProcessManager::WaitForServerReady(ProcessHandles& handles,
                                        std::string& accumulatedOutput,
                                        int timeoutMs) {
    const std::string readyMsg = "Server waiting for a client on";
    auto startTime = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() - startTime < timeout) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            CHAR buffer[4096];
            DWORD bytesRead = 0;
            if (ReadFile(handles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                accumulatedOutput.append(buffer, bytesRead);
                if (accumulatedOutput.find(readyMsg) != std::string::npos) {
                    return true;
                }
            }
        }

        DWORD exitCode = 0;
        if (GetExitCodeProcess(handles.processInfo.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            accumulatedOutput += "\n[TestRunner2] Server process exited early (exit code "
                                + std::to_string(exitCode) + ").";
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    accumulatedOutput += "\n[TestRunner2] Timeout waiting for server ready message.";
    return false;
}

} // namespace TestRunner2

