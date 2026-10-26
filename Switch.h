/**
 * @file Switch.h
 * @brief Declares the request-type router and shared simulation clock.
 */
#ifndef SWITCH_H
#define SWITCH_H

#include "Config.h"
#include "IPBlocker.h"
#include "LoadBalancer.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>

/**
 * @brief Aggregate run statistics, including per-load-balancer details.
 */
struct SimulationStats {
    int initialServers = 0;
    int finalServers = 0;
    int requestedCycles = 0;
    int cyclesCompleted = 0;
    std::size_t initialQueueSize = 0;
    std::size_t remainingQueuedRequests = 0;
    std::size_t requestsStillProcessing = 0;
    std::size_t generatedRequests = 0;
    std::size_t acceptedRequests = 0;
    std::size_t blockedRequests = 0;
    std::size_t completedRequests = 0;
    std::size_t serversAdded = 0;
    std::size_t serversRemoved = 0;
    LoadBalancerStats processing;
    LoadBalancerStats streaming;
};

/**
 * @brief Routes requests into independent P/S balancers that share one clock.
 *
 * The switch creates all requests and applies the firewall before routing.
 * Each cycle advances both load balancers once, retaining separate queues,
 * scaling cooldowns, server pools, and statistics.
 */
class Switch {
public:
    /**
     * @brief Creates processing and streaming balancers from total capacities.
     * @param config Shared simulation configuration.
     * @throws std::invalid_argument If the configuration is invalid.
     */
    explicit Switch(const Config& config);

    /**
     * @brief Runs both balancers together for the requested cycles.
     * @param duration Positive number of simulation cycles.
     * @throws std::invalid_argument If duration is not positive.
     * @throws std::logic_error If this switch has already run.
     * @throws std::runtime_error If the log cannot be opened or written.
     */
    void run(int duration);

    /** @return Aggregate and per-balancer statistics. */
    SimulationStats getStats() const;

private:
    /** @brief Generates one uniquely identified request with random fields. */
    Request generateRequest();
    /** @brief Firewall-checks a request and routes it to the matching balancer. */
    void submitRequest(Request request);
    /** @brief Generates and routes one configured arrival batch when selected. */
    void enqueueArrivals();
    /** @brief Writes a meaningful verbose event to console and log. */
    void logEvent(const std::string& message, const char* color = "");
    /** @brief Writes a configured periodic aggregate/per-balancer snapshot. */
    void logProgressSnapshot();
    /** @brief Writes startup settings and preload statistics. */
    void writeStartingSummary(std::ostream& output) const;
    /** @brief Writes final per-balancer and aggregate counters. */
    void writeSummary(std::ostream& output) const;
    /** @return Sum of the two job-specific waiting queues. */
    std::size_t getQueueSize() const;
    /** @return Sum of requests currently active on both server pools. */
    std::size_t getActiveRequestCount() const;
    /** @return Sum of server counts in both balancers. */
    int getServerCount() const;

    Config config_;
    IPBlocker blocker_;
    LoadBalancer processingBalancer_;
    LoadBalancer streamingBalancer_;
    std::mt19937 random_;
    std::ofstream log_;
    int clock_ = 0;
    int requestedCycles_ = 0;
    std::uint64_t nextRequestId_ = 0;
    std::size_t generatedRequests_ = 0;
    std::size_t blockedRequests_ = 0;
    std::size_t initialQueueSize_ = 0;
    bool hasRun_ = false;
};

#endif
