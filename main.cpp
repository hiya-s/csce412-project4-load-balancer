/**
 * @file main.cpp
 * @brief Parses command-line options and launches the load-balancer simulation.
 */
#include "Config.h"
#include "LoadBalancer.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int parsePositiveInteger(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    int value = 0;
    try {
        value = std::stoi(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error(option + " requires an integer.");
    }
    if (consumed != text.size() || value <= 0) {
        throw std::runtime_error(option + " requires a positive integer.");
    }
    return value;
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program
              << " [--config FILE] [--servers N] [--cycles N]"
                 " [--verbose|--quiet] [--log FILE]\n";
}
}  // namespace

/**
 * @brief Loads settings and starts an interactive or command-line simulation.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument values.
 * @return Zero on success, nonzero when input or simulation setup fails.
 */
int main(int argc, char* argv[]) {
    try {
        std::string configPath = "config.txt";
        int serverCount = 0;
        int duration = 0;
        bool serverCountProvided = false;
        bool durationProvided = false;
        bool verbosityProvided = false;
        bool verbose = true;
        std::string logPath;

        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--help" || argument == "-h") {
                printUsage(argv[0]);
                return 0;
            }
            if (argument == "--verbose" || argument == "--quiet") {
                verbose = argument == "--verbose";
                verbosityProvided = true;
                continue;
            }
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for option " + argument + ".");
            }
            const std::string value = argv[++i];
            if (argument == "--config") {
                configPath = value;
            } else if (argument == "--servers") {
                serverCount = parsePositiveInteger(value, argument);
                serverCountProvided = true;
            } else if (argument == "--cycles") {
                duration = parsePositiveInteger(value, argument);
                durationProvided = true;
            } else if (argument == "--log") {
                logPath = value;
            } else {
                throw std::runtime_error("Unknown option: " + argument);
            }
        }

        const Config config = Config::loadFromFile(configPath);
        Config runConfig = config;

        if (serverCountProvided) {
            runConfig.initialServers = serverCount;
        } else {
            std::cout << "How many web servers should start? ["
                      << runConfig.initialServers << "]: ";
            std::string input;
            if (!std::getline(std::cin, input)) {
                throw std::runtime_error("Could not read the server count.");
            }
            if (!input.empty()) {
                runConfig.initialServers = parsePositiveInteger(input, "Server count");
            }
        }
        runConfig.maximumServers =
            std::max(runConfig.maximumServers, runConfig.initialServers);

        if (!durationProvided) {
            std::cout << "How many clock cycles should the simulation run? ["
                      << runConfig.simulationCycles << "]: ";
            std::string input;
            if (!std::getline(std::cin, input)) {
                throw std::runtime_error("Could not read simulation duration.");
            }
            duration = input.empty()
                           ? runConfig.simulationCycles
                           : parsePositiveInteger(input, "Simulation duration");
        }

        if (verbosityProvided) {
            runConfig.verbose = verbose;
        }
        if (!logPath.empty()) {
            runConfig.logFile = logPath;
        }

        LoadBalancer loadBalancer(runConfig);
        loadBalancer.run(duration);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}