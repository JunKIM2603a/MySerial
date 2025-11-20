#include "ControlServer.h"
#include "ControlClient.h"

#include <iostream>
#include <map>
#include <string>

using namespace TestRunner2;

namespace {

void PrintUsage(const char* programName) {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "TestRunner2 - SerialCommunicator Remote Controller" << std::endl;
    std::cout << "==================================================\n" << std::endl;
    std::cout << "Server mode:" << std::endl;
    std::cout << "  " << programName << " --mode server [--control-port <port>] [--serial-exe <path>]\n" << std::endl;
    std::cout << "Client mode:" << std::endl;
    std::cout << "  " << programName << " --mode client --server <ip> --comports <list> [options]\n" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --repetitions <n>     Number of iterations (default 1)" << std::endl;
    std::cout << "  --datasize <bytes>    Payload size per packet (default 1024)" << std::endl;
    std::cout << "  --num-packets <n>     Packets per iteration (default 100)" << std::endl;
    std::cout << "  --baudrate <bps>      Serial baudrate (default 115200)" << std::endl;
    std::cout << "  --save-logs <true|false> Toggle SerialCommunicator logs" << std::endl;
    std::cout << "  --serial-exe <path>   Path to SerialCommunicator.exe" << std::endl;
    std::cout << std::endl;
}

std::map<std::string, std::string> ParseArguments(int argc, char* argv[]) {
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) == 0 && i + 1 < argc) {
            args[arg.substr(2)] = argv[++i];
        }
    }
    return args;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    auto args = ParseArguments(argc, argv);
    if (!args.count("mode")) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string mode = args["mode"];
    if (mode == "server") {
        int controlPort = Protocol::DEFAULT_CONTROL_PORT;
        if (args.count("control-port")) {
            controlPort = std::stoi(args["control-port"]);
        }
        std::string serialExe = "..\\sSerialCommunicator.exe";
        if (args.count("serial-exe")) {
            serialExe = args["serial-exe"];
        }

        ControlServer server(controlPort, serialExe);
        if (!server.Start()) {
            std::cerr << "Failed to start server." << std::endl;
            return 1;
        }
        return 0;
    } else if (mode == "client") {
        if (!args.count("server")) {
            std::cerr << "Client mode requires --server <ip>" << std::endl;
            return 1;
        }
        if (!args.count("comports")) {
            std::cerr << "Client mode requires --comports <comma-separated list>" << std::endl;
            return 1;
        }

        SerialTestConfig config;
        config.comportList = args["comports"];
        if (args.count("repetitions")) config.repetitions = std::stoi(args["repetitions"]);
        if (args.count("datasize")) config.dataSize = std::stoll(args["datasize"]);
        if (args.count("num-packets")) config.numPackets = std::stoll(args["num-packets"]);
        if (args.count("baudrate")) config.baudrate = std::stoi(args["baudrate"]);
        if (args.count("save-logs")) {
            std::string value = args["save-logs"];
            config.saveLogs = (value == "true" || value == "1");
        }
        if (args.count("serial-exe")) config.serialExecutable = args["serial-exe"];

        std::string serverIp = args["server"];
        int controlPort = Protocol::DEFAULT_CONTROL_PORT;
        if (args.count("control-port")) {
            controlPort = std::stoi(args["control-port"]);
        }

        ControlClient client(serverIp, controlPort);
        bool success = client.Execute(config);
        return success ? 0 : 1;
    }

    std::cerr << "Unknown mode: " << mode << std::endl;
    PrintUsage(argv[0]);
    return 1;
}

