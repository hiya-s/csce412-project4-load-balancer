/**
 * @file LoadBalancer.h
 * @brief Declares the request queue, server pool, clock, and scaling policy.
 */
#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#include "Config.h"
#include "IPBlocker.h"
#include "WebServer.h"

#include <cstddef>
#include <fstream>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <vector>

/**
 * @brief Snapshot of counters after or during a simulation.
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
    int processingServers = 0;
    int streamingServers = 0;
    std::size_t processingQueueSize = 0;
    std::size_t streamingQueueSize = 0;
    std::size_t processingCompleted = 0;
    std::size_t streamingCompleted = 0;
};

/**
 * @brief Simulates request arrivals, dispatch, processing, and server scaling.
 */
class LoadBalancer {
public:
    /**
     * @brief Creates the initial server pool and configured starting queue.
     * @param config Validated run settings.
     * @throws std::invalid_argument If server-pool or traffic settings are invalid.
     */
    explicit LoadBalancer(const Config& config);

    /**
     * @brief Runs the simulation for the given number of clock cycles.
     * @param duration Positive number of requested cycles.
     * @throws std::invalid_argument If duration is not positive.
     * @throws std::logic_error If this instance has already run.
     * @throws std::runtime_error If the log cannot be opened or written.
     */
    void run(int duration);
    /** @return Number of requests waiting in the FIFO queue. */
    std::size_t getQueueSize() const;
    /** @return Current server-pool size. */
    int getServerCount() const;
    /** @return Point-in-time snapshot of simulation counters. */
    SimulationStats getStats() const;

private:
    struct JobPool {
        explicit JobPool(char type) : jobType(type) {}

        char jobType;
        std::queue<Request> requests;
        std::vector<std::unique_ptr<WebServer>> servers;
        std::size_t serversAdded = 0;
        std::size_t serversRemoved = 0;
        std::size_t completedRequests = 0;
    };

    /** @brief Generates a request with a unique ID and configured random fields. */
    Request generateRequest();
    /** @brief Applies the blocker and queues an allowed request. */
    void submitRequest(Request request);
    /** @brief Generates and submits the configured arrival batch if selected. */
    void enqueueArrivals();
    /** @brief Dispatches queued requests from oldest to idle servers. */
    void dispatchRequests();
    /** @brief Applies threshold and cooldown rules to both job-specific pools. */
    void scaleServers();
    /** @brief Logs queue and capacity counters at the configured interval. */
    void logProgressSnapshot();
    /** @brief Writes an event to verbose console and log output. */
    void logEvent(const std::string& message, const char* color = "");
    /** @brief Writes initial settings and counts to the specified stream. */
    void writeStartingSummary(std::ostream& output, int duration) const;
    /** @brief Writes final counts and validates request accounting. */
    void writeSummary(std::ostream& output);
    /** @return Number of requests currently assigned to servers. */
    std::size_t getActiveRequestCount() const;

    Config config_;
    IPBlocker blocker_;
    JobPool processingPool_{'P'};
    JobPool streamingPool_{'S'};
    std::mt19937 random_;
    int nextServerId_ = 0;
    int clock_ = 0;
    int nextScalingClock_ = 0;
    int startingServerCount_ = 0;
    int requestedCycles_ = 0;
    std::size_t initialQueueSize_ = 0;
    std::size_t peakQueueSize_ = 0;
    std::size_t generatedRequests_ = 0;
    std::size_t acceptedRequests_ = 0;
    std::size_t blockedRequests_ = 0;
    std::size_t completedRequests_ = 0;
    std::size_t serversAdded_ = 0;
    std::size_t serversRemoved_ = 0;
    std::ofstream log_;
    bool hasRun_ = false;
};

#endif
