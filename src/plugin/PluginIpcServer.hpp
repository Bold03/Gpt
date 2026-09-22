#pragma once

#include "copilot/Protocol.hpp"
#include "copilot/ThreadSafeQueue.hpp"
#include "copilot/net/TcpSocket.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

class PluginIpcServer {
public:
    PluginIpcServer() = default;
    ~PluginIpcServer();
    PluginIpcServer(const PluginIpcServer&) = delete;
    PluginIpcServer& operator=(const PluginIpcServer&) = delete;
    bool start(std::uint16_t port = copilot::kDefaultPort);
    void stop();
    bool publish(const copilot::json::Value& message);
    std::optional<copilot::AircraftAction> tryPopAction();
    bool connected() const { return connected_.load(); }
private:
    void run(std::uint16_t port);
    void clientSession(copilot::net::TcpStream stream);
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread thread_;
    copilot::ThreadSafeQueue<std::string> outgoing_{256};
    copilot::ThreadSafeQueue<copilot::AircraftAction> incomingActions_{64};
    std::unique_ptr<copilot::net::TcpListener> listener_;
};
