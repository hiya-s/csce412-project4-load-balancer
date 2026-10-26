/**
 * @file IPBlocker.h
 * @brief Declares an IPv4 address and CIDR range blocker.
 */
#ifndef IPBLOCKER_H
#define IPBLOCKER_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Matches IPv4 addresses against exact-address and CIDR block rules.
 */
class IPBlocker {
public:
    IPBlocker() = default;
    /**
     * @brief Initializes the blocker with IPv4 addresses or CIDR ranges.
     * @param ranges IPv4 address or CIDR strings.
     * @throws std::invalid_argument If a range is malformed.
     */
    explicit IPBlocker(const std::vector<std::string>& ranges);

    /**
     * @brief Adds one IPv4 address or CIDR range.
     * @param range IPv4 address or CIDR string.
     * @throws std::invalid_argument If the range is malformed.
     */
    void addRange(const std::string& range);
    /**
     * @brief Returns true if the given IPv4 address matches a blocked range.
     * @param ip IPv4 address to check.
     * @return True if blocked; false if allowed.
     * @throws std::invalid_argument If the address is malformed.
     */
    bool isBlocked(const std::string& ip) const;

private:
    struct Range {
        std::uint32_t network;
        std::uint32_t mask;
    };

    std::vector<Range> ranges_;
};

#endif