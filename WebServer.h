/**
 * @file WebServer.h
 * @brief Declares a server that processes one request at a time.
 */
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "Request.h"

#include <cstdint>
#include <optional>

class WebServer {
public:
    /**
     * @brief Creates an idle server with the specified non-negative ID.
     * @param id Stable server identifier.
     * @throws std::invalid_argument If id is negative.
     */
    explicit WebServer(int id);

    /** @return This server's stable identifier. */
    int getId() const;
    /** @return True if a request is currently being processed. */
    bool isBusy() const;
    /**
     * @return Active request ID, or an empty optional if the server is idle.
     */
    std::optional<std::uint64_t> getCurrentRequestId() const;
    /**
     * @brief Assigns work if idle; returns false if already busy.
     * @param request Request to begin processing.
     * @return True if assigned.
     * @throws std::invalid_argument If the processing time is not positive.
     */
    bool assignRequest(const Request& request);
    /**
     * @brief Advances work by one cycle.
     * @return True exactly when this tick completes a request.
     */
    bool tick();

private:
    int id_;
    std::optional<Request> currentRequest_;
    int remainingTime_ = 0;
};

#endif
