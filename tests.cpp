/**
 * @file tests.cpp
 * @brief Focused regression tests for request processing and load balancing.
 */
#include "Config.h"
#include "IPBlocker.h"
#include "LoadBalancer.h"
#include "WebServer.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
Config baseConfig(const std::string& logPath) {
    Config config;
    config.initialServers = 1;
    config.minimumServers = 1;
    config.maximumServers = 10;
    config.initialRequestsPerServer = 0;
    config.minimumProcessingTime = 1;
    config.maximumProcessingTime = 1;
    config.arrivalProbability = 0.0;
    config.arrivalBatchSize = 1;
    config.scaleUpQueuePerServer = 80;
    config.scaleDownQueuePerServer = 50;
    config.scalingCooldownCycles = 3;
    config.randomSeed = 17;
    config.verbose = false;
    config.logFile = logPath;
    return config;
}

std::string readFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not read test log: " + path);
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void testServerFinishesExactlyOnFifthTick() {
    WebServer server(1);
    const Request request{"192.0.2.1", "198.51.100.2", 5, 'P'};
    assert(server.assignRequest(request));
    for (int cycle = 1; cycle < 5; ++cycle) {
        assert(!server.tick());
        assert(server.isBusy());
    }
    assert(server.tick());
    assert(!server.isBusy());
    assert(server.assignRequest(request));
}

void testFifoDispatchAndAccounting() {
    const std::string logPath = "fifo-test.log";
    Config config = baseConfig(logPath);
    config.maximumServers = 1;
    config.initialRequestsPerServer = 2;
    config.verbose = true;

    LoadBalancer balancer(config);
    balancer.run(2);
    const SimulationStats stats = balancer.getStats();
    assert(stats.generatedRequests == 2);
    assert(stats.completedRequests == 2);
    assert(stats.remainingQueuedRequests == 0);
    assert(stats.requestsStillProcessing == 0);

    const std::string log = readFile(logPath);
    const std::size_t first = log.find("started request #0");
    const std::size_t second = log.find("started request #1");
    assert(first != std::string::npos);
    assert(second != std::string::npos);
    assert(first < second);
    assert(log.find("Accounting check (generated = blocked + completed + queued + "
                    "processing): PASS") != std::string::npos);
    assert(log.find('\x1b') == std::string::npos);
    std::remove(logPath.c_str());
}

void testScaleUpAndCooldown() {
    const std::string logPath = "scale-up-test.log";
    Config config = baseConfig(logPath);
    config.initialRequestsPerServer = 500;
    config.minimumProcessingTime = 100;
    config.maximumProcessingTime = 100;

    LoadBalancer balancer(config);
    balancer.run(7);
    const SimulationStats stats = balancer.getStats();
    assert(stats.serversAdded == 3);
    assert(stats.finalServers == 4);
    std::remove(logPath.c_str());
}

void testBusyServersAreNotRemovedAndMinimumIsOne() {
    const std::string busyLogPath = "busy-scale-test.log";
    Config busyConfig = baseConfig(busyLogPath);
    busyConfig.initialRequestsPerServer = 81;
    busyConfig.maximumServers = 2;
    busyConfig.minimumProcessingTime = 5;
    busyConfig.maximumProcessingTime = 5;
    busyConfig.scalingCooldownCycles = 0;
    LoadBalancer busyBalancer(busyConfig);
    busyBalancer.run(6);
    const SimulationStats busyStats = busyBalancer.getStats();
    assert(busyStats.serversAdded == 1);
    assert(busyStats.serversRemoved == 1);
    assert(busyStats.finalServers == 1);
    std::remove(busyLogPath.c_str());

    const std::string minimumLogPath = "minimum-scale-test.log";
    Config minimumConfig = baseConfig(minimumLogPath);
    minimumConfig.initialServers = 2;
    minimumConfig.maximumServers = 2;
    minimumConfig.scalingCooldownCycles = 0;
    LoadBalancer minimumBalancer(minimumConfig);
    minimumBalancer.run(4);
    const SimulationStats minimumStats = minimumBalancer.getStats();
    assert(minimumStats.finalServers == 1);
    assert(minimumStats.serversRemoved == 1);
    std::remove(minimumLogPath.c_str());
}

void testFirewallBoundariesAndGeneratedAccounting() {
    IPBlocker blocker({"192.168.1.0/24", "203.0.113.7"});
    assert(blocker.isBlocked("192.168.1.0"));
    assert(blocker.isBlocked("192.168.1.255"));
    assert(!blocker.isBlocked("192.168.2.0"));
    assert(blocker.isBlocked("203.0.113.7"));
    assert(!blocker.isBlocked("203.0.113.8"));

    const std::string logPath = "firewall-test.log";
    Config config = baseConfig(logPath);
    config.initialRequestsPerServer = 100;
    config.arrivalProbability = 1.0;
    config.arrivalBatchSize = 2;
    config.minimumProcessingTime = 10;
    config.maximumProcessingTime = 10;
    config.blockedIpRanges = {"10.0.0.0/8"};

    LoadBalancer balancer(config);
    balancer.run(2);
    const SimulationStats stats = balancer.getStats();
    assert(stats.generatedRequests == 104);
    assert(stats.blockedRequests == 104);
    assert(stats.acceptedRequests == 0);
    assert(stats.generatedRequests ==
           stats.blockedRequests + stats.completedRequests +
               stats.remainingQueuedRequests + stats.requestsStillProcessing);
    std::remove(logPath.c_str());
}

void testInvalidConfigurationIsRejected() {
    Config config = baseConfig("invalid-config-test.log");
    config.arrivalBatchSize = 0;
    bool rejected = false;
    try {
        LoadBalancer balancer(config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    const std::string configPath = "invalid-config-test.txt";
    {
        std::ofstream output(configPath);
        output << "arrival_probability = 1.5\n";
    }
    rejected = false;
    try {
        (void)Config::loadFromFile(configPath);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    std::remove(configPath.c_str());
    assert(rejected);

    IPBlocker blocker;
    rejected = false;
    try {
        blocker.addRange("192.168.1.0/33");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}
}  // namespace

int main() {
    testServerFinishesExactlyOnFifthTick();
    testFifoDispatchAndAccounting();
    testScaleUpAndCooldown();
    testBusyServersAreNotRemovedAndMinimumIsOne();
    testFirewallBoundariesAndGeneratedAccounting();
    testInvalidConfigurationIsRejected();
    std::cout << "All load-balancer tests passed.\n";
}
