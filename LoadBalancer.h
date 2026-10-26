/**
 * @file LoadBalancer.h
 * @brief Declares one job-type-specific load balancer.
 */
#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#include "Config.h"
#include "Request.h"
#include "WebServer.h"

#include <cstddef>
#include <memory>
#include <queue>
#include <string>
#include <vector>

/**
 * @brief Per-balancer counters for a single request type.
 */
struct LoadBalancerStats {
    char jobType = 'P';
    int initialServers = 0;
    int finalServers = 0;
    std::size_t initialQueueSize = 0;
    std::size_t queuedRequests = 0;
    std::size_t activeRequests = 0;
    std::size_t generatedRequests = 0;
    std::size_t acceptedRequests = 0;
    std::size_t blockedRequests = 0;
    std::size_t completedRequests = 0;
    std::size_t serversAdded = 0;
    std::size_t serversRemoved = 0;
};

/**
 * @brief Owns a FIFO request queue and dynamically scaled servers for one job type.
 *
 * The Switch submits only matching requests and calls advanceCycle once per
 * simulation cycle. This keeps processing and streaming capacity independent
 * while sharing the simulation clock.
 */
class LoadBalancer {
public:
    /**
     * @brief Creates a job-type-specific balancer and its initial servers.
     * @param config Shared simulation and scaling settings.
     * @param jobType Request type handled by this instance ('P' or 'S').
     * @param initialServers Initial capacity assigned to this instance.
     * @param minimumServers Minimum capacity reserved for this instance.
     * @param maximumServers Maximum capacity available to this instance.
     * @param firstServerId First unique server identifier to allocate.
     */
    LoadBalancer(const Config& config, char jobType, int initialServers,
                 int minimumServers, int maximumServers, int firstServerId);

    /**
     * @brief Places a matching request at the end of this balancer's FIFO.
     * @param request Request to enqueue.
     * @throws std::invalid_argument If request type differs or service time is invalid.
     */
    void submitRequest(const Request& request);

    /**
     * @brief Records a generated request rejected by the switch firewall.
     */
    void recordBlockedRequest();
    /**
     * @brief Records a generated request routed to this job type.
     */
    void recordGeneratedRequest();
    /**
     * @brief Captures accepted preload queue size before the first cycle.
     */
    void recordInitialQueueSize();

    /**
     * @brief Scales, ticks active servers, and dispatches queued work for one cycle.
     * @param clock Current shared simulation cycle, zero-based.
     * @return Human-readable completion and scaling events.
     */
    std::vector<std::string> advanceCycle(int clock);

    /** @return Snapshot of this job type's queue, capacity, and counters. */
    LoadBalancerStats getStats() const;
    /** @return Number of requests currently queued. */
    std::size_t getQueueSize() const;
    /** @return Number of currently active server requests. */
    std::size_t getActiveRequestCount() const;

private:
    /** @brief Applies this pool's queue thresholds and cooldown policy. */
    void scaleServers(int clock, std::vector<std::string>& events);
    /** @brief Assigns oldest waiting requests to idle servers. */
    void dispatchRequests();

    Config config_;
    char jobType_;
    std::queue<Request> requests_;
    std::vector<std::unique_ptr<WebServer>> servers_;
    int minimumServers_;
    int maximumServers_;
    int nextServerId_;
    int initialServers_;
    int nextScalingClock_ = 0;
    std::size_t initialQueueSize_ = 0;
    std::size_t generatedRequests_ = 0;
    std::size_t acceptedRequests_ = 0;
    std::size_t blockedRequests_ = 0;
    std::size_t completedRequests_ = 0;
    std::size_t serversAdded_ = 0;
    std::size_t serversRemoved_ = 0;
};

#endif
