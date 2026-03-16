#ifndef __ISUBSCRIBER_H__
#define __ISUBSCRIBER_H__

#pragma once

#include <functional>
#include <string>
#include <cstdint>

namespace server
{

/// Callback, called on new message receive
using MessageCallback = std::function<void(const std::string& payload)>;

class ISubscriber {
public:
    virtual ~ISubscriber() = default;

    // connect to brker (mqtt), or listen port (WebSocket)
    // host ignored for ws subscriber (listen 0.0.0.0:port)
    virtual bool connect(const std::string& host, uint16_t port) = 0;

    // income message callback
    virtual void set_callback(MessageCallback cb) = 0;

    // run blockeing loop of message receive
    virtual void run() = 0;

    // stop loop from another thread
    virtual void stop() = 0;

    // thansport name for logging
    virtual const char* name() const = 0;
};

}
#endif
