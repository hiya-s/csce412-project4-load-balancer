/**
 * @file tests.cpp
 * @brief Focused regression tests for request processing and load balancing.
 */
#include "Config.h"
#include "IPBlocker.h"
#include "Switch.h"
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
    config.initialServers = 2;
    config.minimumServers = 2;
    config.maximumServers = 10;
    config.initialRequestsPerServer = 0;
    config.minimumProcessingTime = 1;
    config.maximumProcessingTime = 1;
    config.arrivalProbability = 0.0;
    config.arrivalBatchSize = 1;
    config.scaleUpQueuePerServer = 80;
    config.scaleDownQueuePerServer = 50;
    config.scalingCooldownCycles = 3;
    config.progressSnapshotInterval = 1;
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
    config.maximumServers = 2;
    config.initialRequestsPerServer = 2;
    config.verbose = true;

    Switch balancer(config);
    balancer.run(6);
    const SimulationStats stats = balancer.getStats();
    assert(stats.generatedRequests == 4);
    assert(stats.completedRequests == 4);
    assert(stats.remainingQueuedRequests == 0);
    assert(stats.requestsStillProcessing == 0);
    assert(stats.processing.generatedRequests +
               stats.streaming.generatedRequests ==
           stats.generatedRequests);

    const std::string log = readFile(logPath);
    const std::size_t firstStreaming = log.find("S request #0 completed");
    const std::size_t secondStreaming = log.find("S request #1 completed");
    assert(firstStreaming != std::string::npos);
    assert(secondStreaming != std::string::npos);
    assert(firstStreaming < secondStreaming);
    assert(stats.processing.completedRequests > 0);
    assert(stats.streaming.completedRequests > 0);
    assert(log.find("Progress at cycle 1/6") != std::string::npos);
    assert(log.find("accepted request") == std::string::npos);
    assert(log.find("started request") == std::string::npos);
    assert(log.find("Accounting check (generated = blocked + completed + queued + "
                    "processing): PASS") != std::string::npos);
    assert(log.find('\x1b') == std::string::npos);
    std::remove(logPath.c_str());
}

void testAssignedRequestsStartOnNextCycle() {
    Config config = baseConfig("next-cycle-five.log");
    config.initialRequestsPerServer = 1;
    config.minimumProcessingTime = 5;
    config.maximumProcessingTime = 5;

    Switch fiveCycleRun(config);
    fiveCycleRun.run(5);
    const SimulationStats fiveCycleStats = fiveCycleRun.getStats();
    assert(fiveCycleStats.completedRequests == 0);
    assert(fiveCycleStats.remainingQueuedRequests +
               fiveCycleStats.requestsStillProcessing ==
           2);
    std::remove(config.logFile.c_str());

    config.logFile = "next-cycle-six.log";
    Switch sixCycleRun(config);
    sixCycleRun.run(6);
    const SimulationStats sixCycleStats = sixCycleRun.getStats();
    assert(sixCycleStats.completedRequests >= 1);
    assert(sixCycleStats.generatedRequests ==
           sixCycleStats.completedRequests +
               sixCycleStats.remainingQueuedRequests +
               sixCycleStats.requestsStillProcessing);
    std::remove(config.logFile.c_str());
}

void testScaleUpAndCooldown() {
    const std::string logPath = "scale-up-test.log";
    Config config = baseConfig(logPath);
    config.initialRequestsPerServer = 500;
    config.minimumProcessingTime = 100;
    config.maximumProcessingTime = 100;

    Switch balancer(config);
    balancer.run(7);
    const SimulationStats stats = balancer.getStats();
    assert(stats.serversAdded == 6);
    assert(stats.processing.serversAdded == 3);
    assert(stats.streaming.serversAdded == 3);
    assert(stats.finalServers == 8);
    std::remove(logPath.c_str());
}

void testBusyServersAreNotRemovedAndMinimumIsOne() {
    const std::string busyLogPath = "busy-scale-test.log";
    Config busyConfig = baseConfig(busyLogPath);
    busyConfig.initialRequestsPerServer = 100;
    busyConfig.maximumServers = 3;
    busyConfig.scaleUpQueuePerServer = 0;
    busyConfig.minimumProcessingTime = 5;
    busyConfig.maximumProcessingTime = 5;
    busyConfig.scalingCooldownCycles = 0;
    Switch busyBalancer(busyConfig);
    busyBalancer.run(6);
    const SimulationStats busyStats = busyBalancer.getStats();
    assert(busyStats.serversAdded == 1);
    assert(busyStats.serversRemoved == 0);
    assert(busyStats.finalServers == 3);
    std::remove(busyLogPath.c_str());

    const std::string idleLogPath = "idle-scale-test.log";
    Config idleConfig = busyConfig;
    idleConfig.logFile = idleLogPath;
    Switch idleBalancer(idleConfig);
    idleBalancer.run(600);
    const SimulationStats idleStats = idleBalancer.getStats();
    assert(idleStats.serversAdded >= 1);
    assert(idleStats.serversRemoved >= 1);
    assert(idleStats.finalServers == 2);
    std::remove(idleLogPath.c_str());

    const std::string minimumLogPath = "minimum-scale-test.log";
    Config minimumConfig = baseConfig(minimumLogPath);
    minimumConfig.initialServers = 2;
    minimumConfig.minimumServers = 2;
    minimumConfig.maximumServers = 2;
    minimumConfig.scalingCooldownCycles = 0;
    Switch minimumBalancer(minimumConfig);
    minimumBalancer.run(4);
    const SimulationStats minimumStats = minimumBalancer.getStats();
    assert(minimumStats.finalServers == 2);
    assert(minimumStats.serversRemoved == 0);
    assert(minimumStats.processing.finalServers == 1);
    assert(minimumStats.streaming.finalServers == 1);
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

    Switch balancer(config);
    balancer.run(2);
    const SimulationStats stats = balancer.getStats();
    assert(stats.generatedRequests == 204);
    assert(stats.blockedRequests == 204);
    assert(stats.acceptedRequests == 0);
    assert(stats.processing.blockedRequests +
               stats.streaming.blockedRequests ==
           stats.blockedRequests);
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
        Switch balancer(config);
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
    testAssignedRequestsStartOnNextCycle();
    testScaleUpAndCooldown();
    testBusyServersAreNotRemovedAndMinimumIsOne();
    testFirewallBoundariesAndGeneratedAccounting();
    testInvalidConfigurationIsRejected();
    std::cout << "All load-balancer tests passed.\n";
}
