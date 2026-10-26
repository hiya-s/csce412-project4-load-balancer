/**
 * @file Switch.cpp
 * @brief Implements shared-clock routing and reporting for both balancers.
 */
#include "Switch.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
int upperHalf(int value) {
    return value / 2 + value % 2;
}

Config validateConfig(const Config& config) {
    if (config.initialServers < 2 ||
        config.initialServers < config.minimumServers ||
        config.initialServers > config.maximumServers ||
        config.minimumServers < 2 ||
        config.minimumServers > config.maximumServers ||
        config.initialRequestsPerServer < 0 ||
        config.minimumProcessingTime <= 0 ||
        config.maximumProcessingTime < config.minimumProcessingTime ||
        config.arrivalBatchSize <= 0 ||
        config.progressSnapshotInterval < 0 ||
        !std::isfinite(config.arrivalProbability) ||
        config.arrivalProbability < 0.0 ||
        config.arrivalProbability > 1.0 ||
        config.scaleUpQueuePerServer < 0 ||
        config.scaleDownQueuePerServer < 0 ||
        config.scalingCooldownCycles < 0 || config.logFile.empty()) {
        throw std::invalid_argument("Invalid switch or simulation configuration.");
    }
    return config;
}
}  // namespace

Switch::Switch(const Config& config)
    : config_(validateConfig(config)), blocker_(config_.blockedIpRanges),
      processingBalancer_(config_, 'P', upperHalf(config_.initialServers),
                          upperHalf(config_.minimumServers),
                          upperHalf(config_.maximumServers), 0),
      streamingBalancer_(config_, 'S', config_.initialServers / 2,
                         config_.minimumServers / 2,
                         config_.maximumServers / 2,
                         upperHalf(config_.maximumServers)),
      random_(config_.randomSeed == 0 ? std::random_device{}()
                                     : config_.randomSeed) {}

void Switch::run(int duration) {
    if (duration <= 0) {
        throw std::invalid_argument("Simulation duration must be positive.");
    }
    if (hasRun_) {
        throw std::logic_error("A Switch instance can only run once.");
    }

    log_.open(config_.logFile, std::ios::out | std::ios::trunc);
    if (!log_) {
        throw std::runtime_error("Could not open log file: " + config_.logFile);
    }
    hasRun_ = true;
    requestedCycles_ = duration;

    const std::size_t initialRequestCount =
        static_cast<std::size_t>(config_.initialServers) *
        static_cast<std::size_t>(config_.initialRequestsPerServer);
    for (std::size_t i = 0; i < initialRequestCount; ++i) {
        submitRequest(generateRequest());
    }
    processingBalancer_.recordInitialQueueSize();
    streamingBalancer_.recordInitialQueueSize();
    initialQueueSize_ = getQueueSize();

    writeStartingSummary(std::cout);
    writeStartingSummary(log_);

    while (clock_ < duration) {
        enqueueArrivals();
        for (const std::string& event : processingBalancer_.advanceCycle(clock_)) {
            logEvent(event, event.find("completed") == std::string::npos
                                ? "\x1b[33m"
                                : "\x1b[32m");
        }
        for (const std::string& event : streamingBalancer_.advanceCycle(clock_)) {
            logEvent(event, event.find("completed") == std::string::npos
                                ? "\x1b[33m"
                                : "\x1b[32m");
        }

        ++clock_;
        if (config_.progressSnapshotInterval > 0 &&
            clock_ % config_.progressSnapshotInterval == 0) {
            logProgressSnapshot();
        }
    }

    writeSummary(std::cout);
    writeSummary(log_);
    log_.close();
    if (!log_) {
        throw std::runtime_error("Failed while writing log file: " + config_.logFile);
    }
}

SimulationStats Switch::getStats() const {
    const LoadBalancerStats processing = processingBalancer_.getStats();
    const LoadBalancerStats streaming = streamingBalancer_.getStats();
    return {config_.initialServers,
            getServerCount(),
            requestedCycles_,
            clock_,
            initialQueueSize_,
            getQueueSize(),
            getActiveRequestCount(),
            generatedRequests_,
            processing.acceptedRequests + streaming.acceptedRequests,
            blockedRequests_,
            processing.completedRequests + streaming.completedRequests,
            processing.serversAdded + streaming.serversAdded,
            processing.serversRemoved + streaming.serversRemoved,
            processing,
            streaming};
}

Request Switch::generateRequest() {
    std::uniform_int_distribution<int> octet(1, 254);
    std::uniform_int_distribution<int> serviceTime(config_.minimumProcessingTime,
                                                   config_.maximumProcessingTime);
    std::uniform_int_distribution<int> job(0, 1);
    const auto makeIp = [&]() {
        return "10." + std::to_string(octet(random_)) + "." +
               std::to_string(octet(random_)) + "." +
               std::to_string(octet(random_));
    };

    Request request{makeIp(), makeIp(), serviceTime(random_),
                    job(random_) == 0 ? 'P' : 'S'};
    request.requestId = nextRequestId_++;
    ++generatedRequests_;
    if (request.jobType == 'P') {
        processingBalancer_.recordGeneratedRequest();
    } else {
        streamingBalancer_.recordGeneratedRequest();
    }
    return request;
}

void Switch::submitRequest(Request request) {
    if (blocker_.isBlocked(request.inputIp)) {
        ++blockedRequests_;
        if (request.jobType == 'P') {
            processingBalancer_.recordBlockedRequest();
        } else {
            streamingBalancer_.recordBlockedRequest();
        }
        logEvent("Clock " + std::to_string(clock_) + ": blocked " +
                     request.jobType + " request #" +
                     std::to_string(request.requestId) + " from " +
                     request.inputIp + ".",
                 "\x1b[31m");
        return;
    }

    if (request.jobType == 'P') {
        processingBalancer_.submitRequest(request);
    } else {
        streamingBalancer_.submitRequest(request);
    }
}

void Switch::enqueueArrivals() {
    std::bernoulli_distribution arrival(config_.arrivalProbability);
    if (!arrival(random_)) {
        return;
    }
    for (int i = 0; i < config_.arrivalBatchSize; ++i) {
        submitRequest(generateRequest());
    }
}

void Switch::logEvent(const std::string& message, const char* color) {
    if (!config_.verbose) {
        return;
    }
    std::cout << color << message << "\x1b[0m\n";
    log_ << message << '\n';
}

void Switch::logProgressSnapshot() {
    if (!config_.verbose) {
        return;
    }
    const SimulationStats stats = getStats();
    const std::string snapshot =
        "Progress at cycle " + std::to_string(clock_) + "/" +
        std::to_string(requestedCycles_) + ": P-queue=" +
        std::to_string(stats.processing.queuedRequests) + ", S-queue=" +
        std::to_string(stats.streaming.queuedRequests) + ", active=" +
        std::to_string(stats.requestsStillProcessing) + ", servers P/S=" +
        std::to_string(stats.processing.finalServers) + "/" +
        std::to_string(stats.streaming.finalServers) + ", generated=" +
        std::to_string(stats.generatedRequests) + ", completed=" +
        std::to_string(stats.completedRequests) + ", blocked=" +
        std::to_string(stats.blockedRequests) + ".";
    std::cout << "\x1b[35m" << snapshot << "\x1b[0m\n";
    log_ << snapshot << '\n';
}

void Switch::writeStartingSummary(std::ostream& output) const {
    const SimulationStats stats = getStats();
    output << "Starting simulation\n"
           << "Initial servers total (P/S): " << stats.initialServers << " ("
           << stats.processing.initialServers << "/"
           << stats.streaming.initialServers << ")\n"
           << "Requested cycles: " << requestedCycles_ << '\n'
           << "Initial accepted queue size (P/S): " << initialQueueSize_ << " ("
           << stats.processing.initialQueueSize << "/"
           << stats.streaming.initialQueueSize << ")\n"
           << "Logging mode: " << (config_.verbose ? "verbose" : "quiet") << '\n'
           << "Log file: " << config_.logFile << '\n'
           << "Server limits total: " << config_.minimumServers << " to "
           << config_.maximumServers << '\n'
           << "Initial requests per starting server: "
           << config_.initialRequestsPerServer << '\n'
           << "Arrival probability per cycle: " << config_.arrivalProbability << '\n'
           << "Arrival batch size: " << config_.arrivalBatchSize << '\n'
           << "Progress snapshot interval: "
           << config_.progressSnapshotInterval << " cycles\n"
           << "Processing time bounds: " << config_.minimumProcessingTime << " to "
           << config_.maximumProcessingTime << " cycles\n"
           << "Scale-up threshold: above " << config_.scaleUpQueuePerServer
           << " queued requests per server\n"
           << "Scale-down threshold: below " << config_.scaleDownQueuePerServer
           << " queued requests per server\n"
           << "Scaling cooldown per balancer: "
           << config_.scalingCooldownCycles << " cycles\n"
           << "Random seed: "
           << (config_.randomSeed == 0 ? "non-deterministic"
                                       : std::to_string(config_.randomSeed))
           << '\n'
           << "Blocked IP ranges:";
    if (config_.blockedIpRanges.empty()) {
        output << " none";
    } else {
        for (const std::string& range : config_.blockedIpRanges) {
            output << ' ' << range;
        }
    }
    output << "\nGenerated requests so far: " << generatedRequests_
           << "\nBlocked requests so far: " << blockedRequests_ << "\n\n";
}

void Switch::writeSummary(std::ostream& output) const {
    const SimulationStats stats = getStats();
    const bool accountingBalanced =
        stats.generatedRequests ==
        stats.blockedRequests + stats.completedRequests +
            stats.remainingQueuedRequests + stats.requestsStillProcessing;
    output << "Ending summary\n"
           << "Initial servers: " << stats.initialServers << '\n'
           << "Final servers: " << stats.finalServers << '\n'
           << "Processing balancer servers: " << stats.processing.finalServers
           << " (added " << stats.processing.serversAdded << ", removed "
           << stats.processing.serversRemoved << ")\n"
           << "Streaming balancer servers: " << stats.streaming.finalServers
           << " (added " << stats.streaming.serversAdded << ", removed "
           << stats.streaming.serversRemoved << ")\n"
           << "Processing balancer requests: generated "
           << stats.processing.generatedRequests << ", accepted "
           << stats.processing.acceptedRequests << ", blocked "
           << stats.processing.blockedRequests << '\n'
           << "Streaming balancer requests: generated "
           << stats.streaming.generatedRequests << ", accepted "
           << stats.streaming.acceptedRequests << ", blocked "
           << stats.streaming.blockedRequests << '\n'
           << "Processing requests completed: "
           << stats.processing.completedRequests << '\n'
           << "Streaming requests completed: "
           << stats.streaming.completedRequests << '\n'
           << "Requested cycles: " << stats.requestedCycles << '\n'
           << "Cycles completed: " << stats.cyclesCompleted << '\n'
           << "Initial queue size: " << stats.initialQueueSize << '\n'
           << "Remaining queued requests: " << stats.remainingQueuedRequests
           << '\n'
           << "Processing queue: " << stats.processing.queuedRequests << '\n'
           << "Streaming queue: " << stats.streaming.queuedRequests << '\n'
           << "Processing requests still active: "
           << stats.processing.activeRequests << '\n'
           << "Streaming requests still active: "
           << stats.streaming.activeRequests << '\n'
           << "Requests still processing: " << stats.requestsStillProcessing
           << '\n'
           << "Generated requests: " << stats.generatedRequests << '\n'
           << "Accepted requests: " << stats.acceptedRequests << '\n'
           << "Completed requests: " << stats.completedRequests << '\n'
           << "Blocked requests: " << stats.blockedRequests << '\n'
           << "Servers added: " << stats.serversAdded << '\n'
           << "Servers removed: " << stats.serversRemoved << '\n'
           << "Accounting check (generated = blocked + completed + queued + "
              "processing): "
           << (accountingBalanced ? "PASS" : "FAIL") << '\n';
    if (!accountingBalanced) {
        throw std::logic_error("Generated request accounting does not balance.");
    }
}

std::size_t Switch::getQueueSize() const {
    return processingBalancer_.getQueueSize() + streamingBalancer_.getQueueSize();
}

std::size_t Switch::getActiveRequestCount() const {
    return processingBalancer_.getActiveRequestCount() +
           streamingBalancer_.getActiveRequestCount();
}

int Switch::getServerCount() const {
    return processingBalancer_.getStats().finalServers +
           streamingBalancer_.getStats().finalServers;
}
