/**
 * @file IPBlocker.cpp
 * @brief Implements strict IPv4 and CIDR matching.
 */
#include "IPBlocker.h"

#include <stdexcept>

namespace {
std::uint32_t parseIpv4(const std::string& ip) {
    std::uint32_t value = 0;
    std::size_t start = 0;

    for (int part = 0; part < 4; ++part) {
        const std::size_t end = ip.find('.', start);
        const std::size_t length =
            (end == std::string::npos ? ip.size() : end) - start;
        if (length == 0 || (part < 3 && end == std::string::npos) ||
            (part == 3 && end != std::string::npos)) {
            throw std::invalid_argument("Invalid IPv4 address: " + ip);
        }

        unsigned int octet = 0;
        for (std::size_t i = start; i < start + length; ++i) {
            if (ip[i] < '0' || ip[i] > '9') {
                throw std::invalid_argument("Invalid IPv4 address: " + ip);
            }
            const unsigned int digit = static_cast<unsigned int>(ip[i] - '0');
            if (octet > 25 || (octet == 25 && digit > 5)) {
                throw std::invalid_argument("Invalid IPv4 address: " + ip);
            }
            octet = octet * 10 + digit;
        }
        value = (value << 8) | octet;
        start = end == std::string::npos ? ip.size() : end + 1;
    }
    return value;
}
}  // namespace

IPBlocker::IPBlocker(const std::vector<std::string>& ranges) {
    for (const std::string& range : ranges) {
        addRange(range);
    }
}

void IPBlocker::addRange(const std::string& range) {
    const std::size_t slash = range.find('/');
    const std::string address = range.substr(0, slash);
    unsigned int prefixLength = 32;

    if (slash != std::string::npos) {
        const std::string prefix = range.substr(slash + 1);
        if (prefix.empty()) {
            throw std::invalid_argument("Invalid CIDR range: " + range);
        }
        prefixLength = 0;
        for (char digit : prefix) {
            if (digit < '0' || digit > '9') {
                throw std::invalid_argument("Invalid CIDR range: " + range);
            }
            prefixLength = prefixLength * 10 +
                           static_cast<unsigned int>(digit - '0');
            if (prefixLength > 32) {
                throw std::invalid_argument("Invalid CIDR range: " + range);
            }
        }
    }

    const std::uint32_t mask =
        prefixLength == 0 ? 0 : (0xFFFFFFFFu << (32 - prefixLength));
    ranges_.push_back({parseIpv4(address), mask});
}

bool IPBlocker::isBlocked(const std::string& ip) const {
    const std::uint32_t address = parseIpv4(ip);
    for (const Range& range : ranges_) {
        if ((address & range.mask) == (range.network & range.mask)) {
            return true;
        }
    }
    return false;
}