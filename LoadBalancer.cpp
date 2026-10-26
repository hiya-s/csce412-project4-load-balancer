/**
 * @file LoadBalancer.cpp
 * @brief Implements FIFO processing, arrivals, logging, and server scaling.
 */
#include "LoadBalancer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>

LoadBalancer::LoadBalancer(const Config& config)
    : config_(config), blocker_(config.blockedIpRanges),
      random_(config.randomSeed == 0 ? std::random_device{}() : config.randomSeed) {
    if (config_.initialServers < 2 ||
        config_.initialServers < config_.minimumServers ||
        config_.initialServers > config_.maximumServers ||
        config_.minimumServers < 2 ||
        config_.minimumServers > config_.maximumServers ||
        config_.initialRequestsPerServer < 0 ||
        config_.minimumProcessingTime <= 0 ||
        config_.maximumProcessingTime < config_.minimumProcessingTime ||
        config_.arrivalBatchSize <= 0 ||
        !std::isfinite(config_.arrivalProbability) ||
        config_.arrivalProbability < 0.0 ||
        config_.arrivalProbability > 1.0 ||
        config_.scaleUpQueuePerServer < 0 ||
        config_.scaleDownQueuePerServer < 0 ||
        config_.scalingCooldownCycles < 0) {
        throw std::invalid_argument("Invalid load-balancer configuration.");
    }

    processingPool_.servers.reserve(
        static_cast<std::size_t>((config_.maximumServers + 1) / 2));
    streamingPool_.servers.reserve(
        static_cast<std::size_t>(config_.maximumServers / 2));
    startingServerCount_ = config_.initialServers;
    const int processingServerCount = (config_.initialServers + 1) / 2;
    const int streamingServerCount = config_.initialServers / 2;
    for (int i = 0; i < processingServerCount; ++i) {
        processingPool_.servers.push_back(
            std::make_unique<WebServer>(nextServerId_++));
    }
    for (int i = 0; i < streamingServerCount; ++i) {
        streamingPool_.servers.push_back(
            std::make_unique<WebServer>(nextServerId_++));
    }

    const std::size_t initialRequestCount =
        static_cast<std::size_t>(config_.initialServers) *
        static_cast<std::size_t>(config_.initialRequestsPerServer);
    for (std::size_t i = 0; i < initialRequestCount; ++i) {
        submitRequest(generateRequest());
    }
    initialQueueSize_ = getQueueSize();
    peakQueueSize_ = initialQueueSize_;
}

void LoadBalancer::run(int duration) {
    if (duration <= 0) {
        throw std::invalid_argument("Simulation duration must be positive.");
    }
    if (hasRun_) {
        throw std::logic_error("A LoadBalancer instance can only run once.");
    }

    log_.open(config_.logFile, std::ios::out | std::ios::trunc);
    if (!log_) {
        throw std::runtime_error("Could not open log file: " + config_.logFile);
    }
    hasRun_ = true;
    requestedCycles_ = duration;
    writeStartingSummary(std::cout, duration);
    writeStartingSummary(log_, duration);

    while (clock_ < duration) {
        enqueueArrivals();
        peakQueueSize_ = std::max(peakQueueSize_, getQueueSize());
        scaleServers();

        for (JobPool* pool : {&processingPool_, &streamingPool_}) {
            for (const std::unique_ptr<WebServer>& server : pool->servers) {
                const std::optional<std::uint64_t> requestId =
                    server->getCurrentRequestId();
                if (server->tick()) {
                    ++completedRequests_;
                    ++pool->completedRequests;
                    logEvent("Clock " + std::to_string(clock_) + ": " +
                                 pool->jobType + " request #" +
                                 std::to_string(*requestId) + " completed on server " +
                                 std::to_string(server->getId()) + ".",
                             "\x1b[32m");
                }
            }
        }
        dispatchRequests();
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

std::size_t LoadBalancer::getQueueSize() const {
    return processingPool_.requests.size() + streamingPool_.requests.size();
}

int LoadBalancer::getServerCount() const {
    return static_cast<int>(processingPool_.servers.size() +
                            streamingPool_.servers.size());
}

SimulationStats LoadBalancer::getStats() const {
    return {startingServerCount_,
            getServerCount(),
            requestedCycles_,
            clock_,
            initialQueueSize_,
            getQueueSize(),
            getActiveRequestCount(),
            generatedRequests_,
            acceptedRequests_,
            blockedRequests_,
            completedRequests_,
            serversAdded_,
            serversRemoved_,
            static_cast<int>(processingPool_.servers.size()),
            static_cast<int>(streamingPool_.servers.size()),
            processingPool_.requests.size(),
            streamingPool_.requests.size(),
            processingPool_.completedRequests,
            streamingPool_.completedRequests};
}

Request LoadBalancer::generateRequest() {
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
    request.requestId = generatedRequests_++;
    return request;
}

void LoadBalancer::submitRequest(Request request) {
    if (blocker_.isBlocked(request.inputIp)) {
        ++blockedRequests_;
        logEvent("Clock " + std::to_string(clock_) + ": blocked request #" +
                     std::to_string(request.requestId) + " from " + request.inputIp +
                     ".",
                 "\x1b[31m");
        return;
    }

    JobPool& pool = request.jobType == 'P' ? processingPool_ : streamingPool_;
    pool.requests.push(std::move(request));
    ++acceptedRequests_;
}

void LoadBalancer::enqueueArrivals() {
    std::bernoulli_distribution arrival(config_.arrivalProbability);
    if (!arrival(random_)) {
        return;
    }

    for (int i = 0; i < config_.arrivalBatchSize; ++i) {
        submitRequest(generateRequest());
    }
}

void LoadBalancer::dispatchRequests() {
    for (JobPool* pool : {&processingPool_, &streamingPool_}) {
        for (const std::unique_ptr<WebServer>& server : pool->servers) {
            if (pool->requests.empty()) {
                break;
            }
            if (!server->isBusy() && server->assignRequest(pool->requests.front())) {
                pool->requests.pop();
            }
        }
    }
}

void LoadBalancer::scaleServers() {
    if (clock_ < nextScalingClock_) {
        return;
    }

    for (JobPool* pool : {&processingPool_, &streamingPool_}) {
        if (pool->requests.size() >
                static_cast<std::size_t>(config_.scaleUpQueuePerServer) *
                    pool->servers.size() &&
            getServerCount() < config_.maximumServers) {
            pool->servers.push_back(std::make_unique<WebServer>(nextServerId_++));
            ++pool->serversAdded;
            ++serversAdded_;
            nextScalingClock_ = clock_ + config_.scalingCooldownCycles;
            logEvent("Clock " + std::to_string(clock_) + ": added a " +
                         (pool->jobType == 'P' ? "processing" : "streaming") +
                         " server; total pool is now " +
                         std::to_string(getServerCount()) + ".",
                     "\x1b[33m");
            return;
        }
    }

    const int minimumTotalServers = std::max(2, config_.minimumServers);
    for (JobPool* pool : {&processingPool_, &streamingPool_}) {
        if (pool->requests.size() >=
                static_cast<std::size_t>(config_.scaleDownQueuePerServer) *
                    pool->servers.size() ||
            getServerCount() <= minimumTotalServers || pool->servers.size() <= 1) {
            continue;
        }

        const auto idleServer =
            std::find_if(pool->servers.rbegin(), pool->servers.rend(),
                         [](const std::unique_ptr<WebServer>& server) {
                             return !server->isBusy();
                         });
        if (idleServer == pool->servers.rend()) {
            continue;
        }

        const int removedId = (*idleServer)->getId();
        pool->servers.erase(std::next(idleServer).base());
        ++pool->serversRemoved;
        ++serversRemoved_;
        nextScalingClock_ = clock_ + config_.scalingCooldownCycles;
        logEvent("Clock " + std::to_string(clock_) + ": removed idle " +
                     (pool->jobType == 'P' ? "processing" : "streaming") +
                     " server " + std::to_string(removedId) +
                     "; total pool is now " + std::to_string(getServerCount()) + ".",
                 "\x1b[33m");
        return;
    }
}

void LoadBalancer::logEvent(const std::string& message, const char* color) {
    if (!hasRun_ || !config_.verbose) {
        return;
    }

    std::cout << color << message << "\x1b[0m\n";
    log_ << message << '\n';
}

void LoadBalancer::logProgressSnapshot() {
    if (!config_.verbose) {
        return;
    }

    const std::string snapshot =
        "Progress at cycle " + std::to_string(clock_) + "/" +
        std::to_string(requestedCycles_) + ": P-queue=" +
        std::to_string(processingPool_.requests.size()) + ", S-queue=" +
        std::to_string(streamingPool_.requests.size()) + ", active=" +
        std::to_string(getActiveRequestCount()) + ", servers=" +
        std::to_string(getServerCount()) + ", generated=" +
        std::to_string(generatedRequests_) + ", completed=" +
        std::to_string(completedRequests_) + ", blocked=" +
        std::to_string(blockedRequests_) + ".";
    std::cout << "\x1b[35m" << snapshot << "\x1b[0m\n";
    log_ << snapshot << '\n';
}

std::size_t LoadBalancer::getActiveRequestCount() const {
    std::size_t activeCount = 0;
    for (const JobPool* pool : {&processingPool_, &streamingPool_}) {
        activeCount += static_cast<std::size_t>(std::count_if(
            pool->servers.begin(), pool->servers.end(),
            [](const std::unique_ptr<WebServer>& server) {
                return server->isBusy();
            }));
    }
    return activeCount;
}

void LoadBalancer::writeStartingSummary(std::ostream& output, int duration) const {
    output << "Starting simulation\n"
           << "Initial servers: " << startingServerCount_ << '\n'
           << "Initial processing servers: " << ((startingServerCount_ + 1) / 2)
           << '\n'
           << "Initial streaming servers: " << (startingServerCount_ / 2) << '\n'
           << "Requested cycles: " << duration << '\n'
           << "Initial queue size: " << initialQueueSize_ << '\n'
           << "Logging mode: " << (config_.verbose ? "verbose" : "quiet") << '\n'
           << "Log file: " << config_.logFile << '\n'
           << "Server limits: " << config_.minimumServers << " to "
           << config_.maximumServers << '\n'
           << "Initial requests per server: " << config_.initialRequestsPerServer
           << '\n'
           << "Arrival probability: " << config_.arrivalProbability << '\n'
           << "Arrival batch size: " << config_.arrivalBatchSize << '\n'
           << "Progress snapshot interval: "
           << config_.progressSnapshotInterval << " cycles\n"
           << "Processing time bounds: " << config_.minimumProcessingTime << " to "
           << config_.maximumProcessingTime << " cycles\n"
           << "Scale-up threshold: above " << config_.scaleUpQueuePerServer
           << " queued requests per server\n"
           << "Scale-down threshold: below " << config_.scaleDownQueuePerServer
           << " queued requests per server\n"
           << "Scaling cooldown: " << config_.scalingCooldownCycles << " cycles\n"
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

void LoadBalancer::writeSummary(std::ostream& output) {
    const std::size_t activeRequests = getActiveRequestCount();
    const std::size_t remainingQueuedRequests = getQueueSize();
    const bool accountingBalanced =
        generatedRequests_ ==
        blockedRequests_ + completedRequests_ + remainingQueuedRequests + activeRequests;
    output << "Ending summary\n"
           << "Initial servers: " << startingServerCount_ << '\n'
           << "Final servers: " << getServerCount() << '\n'
           << "Processing servers: " << processingPool_.servers.size() << '\n'
           << "Streaming servers: " << streamingPool_.servers.size() << '\n'
           << "Processing requests completed: "
           << processingPool_.completedRequests << '\n'
           << "Streaming requests completed: "
           << streamingPool_.completedRequests << '\n'
           << "Requested cycles: " << requestedCycles_ << '\n'
           << "Cycles completed: " << clock_ << '\n'
           << "Initial queue size: " << initialQueueSize_ << '\n'
           << "Remaining queued requests: " << remainingQueuedRequests << '\n'
           << "Processing queue: " << processingPool_.requests.size() << '\n'
           << "Streaming queue: " << streamingPool_.requests.size() << '\n'
           << "Requests still processing: " << activeRequests << '\n'
           << "Generated requests: " << generatedRequests_ << '\n'
           << "Accepted requests: " << acceptedRequests_ << '\n'
           << "Completed requests: " << completedRequests_ << '\n'
           << "Blocked requests: " << blockedRequests_ << '\n'
           << "Servers added: " << serversAdded_ << '\n'
           << "Servers removed: " << serversRemoved_ << '\n'
           << "Accounting check (generated = blocked + completed + queued + "
              "processing): "
           << (accountingBalanced ? "PASS" : "FAIL") << '\n';
    if (!accountingBalanced) {
        throw std::logic_error("Generated request accounting does not balance.");
    }
}
