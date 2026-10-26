/**
 * @file Config.cpp
 * @brief Parses and validates simulation configuration.
 */
#include "Config.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int parseInt(const std::string& value, const std::string& key) {
    std::size_t consumed = 0;
    int result = 0;
    try {
        result = std::stoi(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer for setting '" + key + "'.");
    }
    if (consumed != value.size()) {
        throw std::runtime_error("Invalid integer for setting '" + key + "'.");
    }
    return result;
}

double parseDouble(const std::string& value, const std::string& key) {
    std::size_t consumed = 0;
    double result = 0.0;
    try {
        result = std::stod(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid number for setting '" + key + "'.");
    }
    if (consumed != value.size()) {
        throw std::runtime_error("Invalid number for setting '" + key + "'.");
    }
    return result;
}
}  // namespace

Config Config::loadFromFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open configuration file: " + path);
    }

    Config config;
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("Expected key=value at " + path + ":" +
                                     std::to_string(lineNumber));
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) {
            throw std::runtime_error("Empty key or value at " + path + ":" +
                                     std::to_string(lineNumber));
        }

        if (key == "simulation_cycles") {
            config.simulationCycles = parseInt(value, key);
        } else if (key == "initial_servers") {
            config.initialServers = parseInt(value, key);
        } else if (key == "min_servers") {
            config.minimumServers = parseInt(value, key);
        } else if (key == "max_servers") {
            config.maximumServers = parseInt(value, key);
        } else if (key == "initial_requests_per_server") {
            config.initialRequestsPerServer = parseInt(value, key);
        } else if (key == "min_processing_time") {
            config.minimumProcessingTime = parseInt(value, key);
        } else if (key == "max_processing_time") {
            config.maximumProcessingTime = parseInt(value, key);
        } else if (key == "arrival_probability") {
            config.arrivalProbability = parseDouble(value, key);
        } else if (key == "arrival_batch_size") {
            config.arrivalBatchSize = parseInt(value, key);
        } else if (key == "scale_up_queue_per_server") {
            config.scaleUpQueuePerServer = parseInt(value, key);
        } else if (key == "scale_down_queue_per_server") {
            config.scaleDownQueuePerServer = parseInt(value, key);
        } else if (key == "scaling_cooldown_cycles") {
            config.scalingCooldownCycles = parseInt(value, key);
        } else if (key == "verbose") {
            if (value == "true" || value == "1") {
                config.verbose = true;
            } else if (value == "false" || value == "0") {
                config.verbose = false;
            } else {
                throw std::runtime_error("verbose must be true, false, 1, or 0.");
            }
        } else if (key == "log_file") {
            config.logFile = value;
        } else if (key == "random_seed") {
            const int seed = parseInt(value, key);
            if (seed < 0) {
                throw std::runtime_error("random_seed cannot be negative.");
            }
            config.randomSeed = static_cast<std::uint32_t>(seed);
        } else if (key == "blocked_ips") {
            std::istringstream ranges(value);
            std::string range;
            while (std::getline(ranges, range, ',')) {
                range = trim(range);
                if (range.empty()) {
                    throw std::runtime_error("Empty address in blocked_ips.");
                }
                config.blockedIpRanges.push_back(range);
            }
        } else {
            throw std::runtime_error("Unknown configuration setting '" + key + "'.");
        }
    }

    if (config.simulationCycles <= 0 || config.minimumServers <= 0 ||
        config.maximumServers < config.minimumServers ||
        config.initialServers < config.minimumServers ||
        config.initialServers > config.maximumServers ||
        config.initialRequestsPerServer < 0 ||
        config.minimumProcessingTime <= 0 ||
        config.maximumProcessingTime < config.minimumProcessingTime ||
        config.arrivalBatchSize <= 0 ||
        config.scaleUpQueuePerServer < 0 || config.scaleDownQueuePerServer < 0 ||
        config.scalingCooldownCycles < 0 ||
        !std::isfinite(config.arrivalProbability) ||
        config.arrivalProbability < 0.0 || config.arrivalProbability > 1.0 ||
        config.logFile.empty()) {
        throw std::runtime_error("Configuration values are outside valid ranges.");
    }

    return config;
}