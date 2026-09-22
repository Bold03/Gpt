#include "copilot/Protocol.hpp"
#include "copilot/net/TcpSocket.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace {
struct PendingHeading { double value{}; };
void printHelp() {
    std::cout << "Commands:\n"
              << "  heading <1..360>   send SET_HEADING\n"
              << "  help               show commands\n"
              << "  quit               exit\n";
}
} // namespace

int main() {
    using namespace std::chrono_literals;
    std::cout << "CopilotCore MVP\n";
    std::cout << "Connecting to X-Plane plugin at 127.0.0.1:" << copilot::kDefaultPort << " ...\n";
    auto connection = copilot::net::TcpStream::connectLoopback(copilot::kDefaultPort);
    if (!connection) {
        std::cerr << "Connection failed. Start X-Plane with CopilotAI.xpl loaded first.\n";
        return 2;
    }
    auto stream = std::move(*connection);
    if (!stream.sendLine(copilot::makeHello("CopilotCore").dump())) {
        std::cerr << "Could not send handshake.\n";
        return 3;
    }

    std::atomic<bool> running{true};
    std::mutex pendingMutex;
    std::unordered_map<std::uint64_t, PendingHeading> pending;
    std::uint64_t requestCounter = 0;
    std::uint64_t lastSequence = 0;

    std::thread receiver([&] {
        auto lastHeartbeat = std::chrono::steady_clock::now();
        while (running.load()) {
            auto result = stream.receiveLine(250);
            if (result.status == copilot::net::ReceiveStatus::Timeout) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastHeartbeat >= 1s) {
                    if (!stream.sendLine(copilot::makeHeartbeat().dump())) {
                        running.store(false);
                        break;
                    }
                    lastHeartbeat = now;
                }
                continue;
            }
            if (result.status != copilot::net::ReceiveStatus::Line) {
                std::cerr << "\nSimulator connection lost.\n";
                running.store(false);
                break;
            }
            auto message = copilot::json::Value::parse(result.line);
            if (!message) {
                std::cerr << "\nBad IPC message.\n> " << std::flush;
                continue;
            }
            const std::string type = copilot::messageType(*message);
            if (type == "state") {
                auto state = copilot::parseState(*message);
                if (!state || state->sequence <= lastSequence) continue;
                lastSequence = state->sequence;
                std::cout << "\rALT " << static_cast<int>(state->altitudeFt)
                          << "  IAS " << static_cast<int>(state->indicatedAirspeedKt)
                          << "  VS " << static_cast<int>(state->verticalSpeedFpm)
                          << "  MCP HDG " << static_cast<int>(std::lround(state->mcpHeadingDeg))
                          << "  " << (state->onGround ? "GROUND" : "AIR") << "          " << std::flush;
                std::lock_guard<std::mutex> lock(pendingMutex);
                for (auto it = pending.begin(); it != pending.end();) {
                    if (std::abs(state->mcpHeadingDeg - it->second.value) <= 0.5) {
                        std::cout << "\n[VERIFIED] request #" << it->first
                                  << " heading " << it->second.value << " set.\n> " << std::flush;
                        it = pending.erase(it);
                    } else {
                        ++it;
                    }
                }
            } else if (type == "action_result") {
                const auto* requestNode = message->find("request_id");
                const auto* payload = message->find("payload");
                if (!requestNode || !payload || !payload->isObject()) continue;
                const auto requestId = static_cast<std::uint64_t>(requestNode->numberOr());
                const auto* acceptedNode = payload->find("accepted");
                const auto* executedNode = payload->find("executed");
                const auto* reasonNode = payload->find("reason");
                const bool accepted = acceptedNode && acceptedNode->boolOr();
                const bool executed = executedNode && executedNode->boolOr();
                const std::string reason = reasonNode ? reasonNode->stringOr() : std::string{};
                std::cout << "\n[ACTION] #" << requestId << " accepted=" << accepted << " executed=" << executed;
                if (!reason.empty()) std::cout << " reason=" << reason;
                std::cout << "\n> " << std::flush;
                if (!accepted || !executed) {
                    std::lock_guard<std::mutex> lock(pendingMutex);
                    pending.erase(requestId);
                }
            }
        }
    });

    printHelp();
    std::string line;
    while (running.load()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == "quit" || line == "exit") break;
        if (line == "help") { printHelp(); continue; }
        std::istringstream input(line);
        std::string command;
        input >> command;
        if (command == "heading") {
            double heading = 0.0;
            if (!(input >> heading) || !std::isfinite(heading) || heading < 1.0 || heading > 360.0) {
                std::cout << "Invalid heading. Use 1..360.\n";
                continue;
            }
            const auto requestId = ++requestCounter;
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                pending.emplace(requestId, PendingHeading{heading});
            }
            if (!stream.sendLine(copilot::makeAction(requestId, "SET_HEADING", heading).dump())) {
                std::cerr << "Send failed.\n";
                running.store(false);
                break;
            }
            continue;
        }
        std::cout << "Unknown command. Type 'help'.\n";
    }

    running.store(false);
    stream.close();
    if (receiver.joinable()) receiver.join();
    std::cout << "\nCopilotCore stopped.\n";
    return 0;
}
