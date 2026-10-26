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
    if (config_.initialServers < 1 ||
        config_.initialServers < config_.minimumServers ||
        config_.initialServers > config_.maximumServers ||
        config_.minimumServers < 1 ||
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

    servers_.reserve(static_cast<std::size_t>(config_.maximumServers));
    startingServerCount_ = config_.initialServers;
    for (int i = 0; i < config_.initialServers; ++i) {
        servers_.push_back(std::make_unique<WebServer>(nextServerId_++));
    }

    const std::size_t initialRequestCount =
        static_cast<std::size_t>(config_.initialServers) *
        static_cast<std::size_t>(config_.initialRequestsPerServer);
    for (std::size_t i = 0; i < initialRequestCount; ++i) {
        submitRequest(generateRequest());
    }
    initialQueueSize_ = requestQueue_.size();
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
        peakQueueSize_ = std::max(peakQueueSize_, requestQueue_.size());
        scaleServers();
        dispatchRequests();

        for (const std::unique_ptr<WebServer>& server : servers_) {
            if (server->tick()) {
                ++completedRequests_;
                logEvent("Clock " + std::to_string(clock_) +
                             ": request completed.",
                         "\x1b[32m");
            }
        }
        ++clock_;
    }

    writeSummary(std::cout);
    writeSummary(log_);
    log_.close();
    if (!log_) {
        throw std::runtime_error("Failed while writing log file: " + config_.logFile);
    }
}

std::size_t LoadBalancer::getQueueSize() const {
    return requestQueue_.size();
}

int LoadBalancer::getServerCount() const {
    return static_cast<int>(servers_.size());
}

SimulationStats LoadBalancer::getStats() const {
    return {startingServerCount_,
            getServerCount(),
            requestedCycles_,
            clock_,
            initialQueueSize_,
            requestQueue_.size(),
            getActiveRequestCount(),
            generatedRequests_,
            acceptedRequests_,
            blockedRequests_,
            completedRequests_,
            serversAdded_,
            serversRemoved_};
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

    requestQueue_.push(std::move(request));
    ++acceptedRequests_;
    const Request& queuedRequest = requestQueue_.back();
    logEvent("Clock " + std::to_string(clock_) + ": accepted request #" +
                 std::to_string(queuedRequest.requestId) + " (" +
                 queuedRequest.inputIp + " -> " + queuedRequest.outputIp + ", " +
                 std::to_string(queuedRequest.processingTime) + " cycles, " +
                 queuedRequest.jobType + ").",
             "\x1b[36m");
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
    for (const std::unique_ptr<WebServer>& server : servers_) {
        if (requestQueue_.empty()) {
            break;
        }
        if (!server->isBusy() && server->assignRequest(requestQueue_.front())) {
            const Request& request = requestQueue_.front();
            logEvent("Clock " + std::to_string(clock_) + ": server " +
                         std::to_string(server->getId()) + " started request #" +
                         std::to_string(request.requestId) + ".",
                     "\x1b[34m");
            requestQueue_.pop();
        }
    }
}

void LoadBalancer::scaleServers() {
    if (clock_ < nextScalingClock_) {
        return;
    }

    const std::size_t serverCount = servers_.size();
    if (requestQueue_.size() >
            static_cast<std::size_t>(config_.scaleUpQueuePerServer) * serverCount &&
        getServerCount() < config_.maximumServers) {
        servers_.push_back(std::make_unique<WebServer>(nextServerId_++));
        ++serversAdded_;
        nextScalingClock_ = clock_ + config_.scalingCooldownCycles;
        logEvent("Clock " + std::to_string(clock_) + ": added a server; pool is now " +
                     std::to_string(getServerCount()) + ".",
                 "\x1b[33m");
        return;
    }

    if (requestQueue_.size() >=
            static_cast<std::size_t>(config_.scaleDownQueuePerServer) * serverCount ||
        getServerCount() <= config_.minimumServers) {
        return;
    }

    const auto idleServer =
        std::find_if(servers_.rbegin(), servers_.rend(),
                     [](const std::unique_ptr<WebServer>& server) {
                         return !server->isBusy();
                     });
    if (idleServer == servers_.rend()) {
        return;
    }

    const int removedId = (*idleServer)->getId();
    servers_.erase(std::next(idleServer).base());
    ++serversRemoved_;
    nextScalingClock_ = clock_ + config_.scalingCooldownCycles;
    logEvent("Clock " + std::to_string(clock_) + ": removed idle server " +
                 std::to_string(removedId) + "; pool is now " +
                 std::to_string(getServerCount()) + ".",
             "\x1b[33m");
}

void LoadBalancer::logEvent(const std::string& message, const char* color) {
    if (!hasRun_ || !config_.verbose) {
        return;
    }

    std::cout << color << message << "\x1b[0m\n";
    log_ << message << '\n';
}

std::size_t LoadBalancer::getActiveRequestCount() const {
    return static_cast<std::size_t>(std::count_if(
        servers_.begin(), servers_.end(),
        [](const std::unique_ptr<WebServer>& server) { return server->isBusy(); }));
}

void LoadBalancer::writeStartingSummary(std::ostream& output, int duration) const {
    output << "Starting simulation\n"
           << "Initial servers: " << startingServerCount_ << '\n'
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

void LoadBalancer::writeSummary(std::ostream& output) const {
    const std::size_t activeRequests = getActiveRequestCount();
    const bool accountingBalanced =
        generatedRequests_ ==
        blockedRequests_ + completedRequests_ + requestQueue_.size() + activeRequests;
    output << "Ending summary\n"
           << "Initial servers: " << startingServerCount_ << '\n'
           << "Final servers: " << getServerCount() << '\n'
           << "Requested cycles: " << requestedCycles_ << '\n'
           << "Cycles completed: " << clock_ << '\n'
           << "Initial queue size: " << initialQueueSize_ << '\n'
           << "Remaining queued requests: " << requestQueue_.size() << '\n'
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
