/**
 * @file WebServer.cpp
 * @brief Implements server request assignment and processing.
 */
#include "WebServer.h"

#include <stdexcept>

WebServer::WebServer(int id) : id_(id) {
    if (id < 0) {
        throw std::invalid_argument("Server ID cannot be negative.");
    }
}

int WebServer::getId() const {
    return id_;
}

bool WebServer::isBusy() const {
    return currentRequest_.has_value();
}

bool WebServer::assignRequest(const Request& request) {
    if (isBusy()) {
        return false;
    }
    if (request.processingTime <= 0) {
        throw std::invalid_argument("Request processing time must be positive.");
    }

    currentRequest_ = request;
    remainingTime_ = request.processingTime;
    return true;
}

bool WebServer::tick() {
    if (!isBusy()) {
        return false;
    }

    --remainingTime_;
    if (remainingTime_ == 0) {
        currentRequest_.reset();
        return true;
    }
    return false;
}
