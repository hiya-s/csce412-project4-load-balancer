/**
 * @file LoadBalancer.cpp
 * @brief Implements a single job-type-specific queue and server pool.
 */
#include "LoadBalancer.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

LoadBalancer::LoadBalancer(const Config& config, char jobType, int initialServers,
                           int minimumServers, int maximumServers,
                           int firstServerId)
    : config_(config), jobType_(jobType), minimumServers_(minimumServers),
      maximumServers_(maximumServers), nextServerId_(firstServerId),
      initialServers_(initialServers) {
    if ((jobType_ != 'P' && jobType_ != 'S') || initialServers < minimumServers_ ||
        initialServers > maximumServers_ || minimumServers_ < 1 ||
        maximumServers_ < minimumServers_ || nextServerId_ < 0) {
        throw std::invalid_argument("Invalid job-specific load-balancer settings.");
    }

    servers_.reserve(static_cast<std::size_t>(maximumServers_));
    for (int i = 0; i < initialServers; ++i) {
        servers_.push_back(std::make_unique<WebServer>(nextServerId_++));
    }
}

void LoadBalancer::submitRequest(const Request& request) {
    if (request.jobType != jobType_) {
        throw std::invalid_argument("Request was routed to the wrong load balancer.");
    }
    if (request.processingTime <= 0) {
        throw std::invalid_argument("Request processing time must be positive.");
    }
    requests_.push(request);
    ++acceptedRequests_;
}

void LoadBalancer::recordBlockedRequest() {
    ++blockedRequests_;
}

void LoadBalancer::recordGeneratedRequest() {
    ++generatedRequests_;
}

void LoadBalancer::recordInitialQueueSize() {
    initialQueueSize_ = requests_.size();
}

std::vector<std::string> LoadBalancer::advanceCycle(int clock) {
    std::vector<std::string> events;
    scaleServers(clock, events);

    for (const std::unique_ptr<WebServer>& server : servers_) {
        const std::optional<std::uint64_t> requestId =
            server->getCurrentRequestId();
        if (server->tick()) {
            ++completedRequests_;
            events.push_back("Clock " + std::to_string(clock) + ": " + jobType_ +
                             " request #" + std::to_string(*requestId) +
                             " completed on server " +
                             std::to_string(server->getId()) + ".");
        }
    }

    dispatchRequests();
    return events;
}

LoadBalancerStats LoadBalancer::getStats() const {
    const std::size_t activeCount = getActiveRequestCount();
    return {jobType_,
            initialServers_,
            static_cast<int>(servers_.size()),
            initialQueueSize_,
            requests_.size(),
            activeCount,
            generatedRequests_,
            acceptedRequests_,
            blockedRequests_,
            completedRequests_,
            serversAdded_,
            serversRemoved_};
}

std::size_t LoadBalancer::getQueueSize() const {
    return requests_.size();
}

std::size_t LoadBalancer::getActiveRequestCount() const {
    return static_cast<std::size_t>(std::count_if(
        servers_.begin(), servers_.end(),
        [](const std::unique_ptr<WebServer>& server) { return server->isBusy(); }));
}

void LoadBalancer::scaleServers(int clock, std::vector<std::string>& events) {
    if (clock < nextScalingClock_) {
        return;
    }

    if (requests_.size() >
            static_cast<std::size_t>(config_.scaleUpQueuePerServer) *
                servers_.size() &&
        static_cast<int>(servers_.size()) < maximumServers_) {
        servers_.push_back(std::make_unique<WebServer>(nextServerId_++));
        ++serversAdded_;
        nextScalingClock_ = clock + config_.scalingCooldownCycles;
        events.push_back("Clock " + std::to_string(clock) + ": added a " +
                         (jobType_ == 'P' ? "processing" : "streaming") +
                         " load-balancer server; pool size is now " +
                         std::to_string(servers_.size()) + ".");
        return;
    }

    if (requests_.size() >=
            static_cast<std::size_t>(config_.scaleDownQueuePerServer) *
                servers_.size() ||
        static_cast<int>(servers_.size()) <= minimumServers_) {
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
    nextScalingClock_ = clock + config_.scalingCooldownCycles;
    events.push_back("Clock " + std::to_string(clock) + ": removed idle " +
                     (jobType_ == 'P' ? "processing" : "streaming") +
                     " server " + std::to_string(removedId) +
                     "; pool size is now " + std::to_string(servers_.size()) + ".");
}

void LoadBalancer::dispatchRequests() {
    for (const std::unique_ptr<WebServer>& server : servers_) {
        if (requests_.empty()) {
            return;
        }
        if (!server->isBusy() && server->assignRequest(requests_.front())) {
            requests_.pop();
        }
    }
}
