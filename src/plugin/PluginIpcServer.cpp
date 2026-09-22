#include "PluginIpcServer.hpp"

#include <chrono>
#include <iostream>

PluginIpcServer::~PluginIpcServer() { stop(); }

bool PluginIpcServer::start(std::uint16_t port) {
    if (running_.exchange(true)) return true;
    thread_ = std::thread([this, port] { run(port); });
    return true;
}

void PluginIpcServer::stop() {
    if (!running_.exchange(false)) return;
    if (listener_) listener_->close();
    if (thread_.joinable()) thread_.join();
    connected_.store(false);
    outgoing_.clear();
    incomingActions_.clear();
}

bool PluginIpcServer::publish(const copilot::json::Value& message) {
    if (!connected_.load()) return false;
    return outgoing_.push(message.dump());
}

std::optional<copilot::AircraftAction> PluginIpcServer::tryPopAction() {
    return incomingActions_.tryPop();
}

void PluginIpcServer::run(std::uint16_t port) {
    listener_ = std::make_unique<copilot::net::TcpListener>();
    if (!listener_->bindLoopback(port)) {
        std::cerr << "[CopilotAI] IPC bind failed on 127.0.0.1:" << port << "\n";
        running_.store(false);
        return;
    }
    while (running_.load()) {
        auto client = listener_->acceptOne();
        if (!client) {
            if (running_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        connected_.store(true);
        outgoing_.clear();
        incomingActions_.clear();
        clientSession(std::move(*client));
        connected_.store(false);
        outgoing_.clear();
        incomingActions_.clear();
    }
}

void PluginIpcServer::clientSession(copilot::net::TcpStream stream) {
    while (running_.load() && stream.valid()) {
        while (auto outgoing = outgoing_.tryPop()) {
            if (!stream.sendLine(*outgoing)) {
                stream.close();
                return;
            }
        }
        auto received = stream.receiveLine(20);
        if (received.status == copilot::net::ReceiveStatus::Timeout) continue;
        if (received.status != copilot::net::ReceiveStatus::Line) return;
        auto message = copilot::json::Value::parse(received.line);
        if (!message) continue;
        const std::string type = copilot::messageType(*message);
        if (type == "action") {
            auto action = copilot::parseAction(*message);
            if (action) incomingActions_.push(std::move(*action));
        }
    }
}
