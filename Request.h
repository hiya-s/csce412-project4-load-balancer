/**
 * @file Request.h
 * @brief Defines a single request processed by a web server.
 */
#ifndef REQUEST_H
#define REQUEST_H

#include <cstdint>
#include <string>

/**
 * @brief Work item routed through the load balancer.
 */
struct Request {
    /** Requester's IPv4 address. */
    std::string inputIp;
    /** Destination IPv4 address. */
    std::string outputIp;
    /** Number of clock cycles required to process the request. */
    int processingTime;
    /** Request category: P (processing) or S (streaming). */
    char jobType;
    /** Unique zero-based sequence number assigned when the request is generated. */
    std::uint64_t requestId = 0;
};

#endif
