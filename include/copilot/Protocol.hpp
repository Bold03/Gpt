#pragma once

#include "copilot/SimpleJson.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace copilot {

inline constexpr int kProtocolVersion = 1;
inline constexpr std::uint16_t kDefaultPort = 49075;

struct FlightState {
    std::uint64_t sequence{};
    double altitudeFt{};
    double indicatedAirspeedKt{};
    double verticalSpeedFpm{};
    double mcpHeadingDeg{};
    bool onGround{};
};

struct AircraftAction {
    std::uint64_t requestId{};
    std::string action;
    double value{};
};

inline json::Value makeHello(const std::string& clientName) {
    return json::object({
        {"protocol", kProtocolVersion},
        {"type", "hello"},
        {"payload", json::object({{"client", clientName}})}
    });
}

inline json::Value makeHeartbeat() {
    return json::object({
        {"protocol", kProtocolVersion},
        {"type", "heartbeat"}
    });
}

inline json::Value makeState(const FlightState& state) {
    return json::object({
        {"protocol", kProtocolVersion},
        {"type", "state"},
        {"sequence", static_cast<unsigned long long>(state.sequence)},
        {"payload", json::object({
            {"altitude_ft", state.altitudeFt},
            {"ias_kt", state.indicatedAirspeedKt},
            {"vertical_speed_fpm", state.verticalSpeedFpm},
            {"mcp_heading_deg", state.mcpHeadingDeg},
            {"on_ground", state.onGround}
        })}
    });
}

inline json::Value makeAction(std::uint64_t requestId,
                              const std::string& action,
                              double value) {
    return json::object({
        {"protocol", kProtocolVersion},
        {"type", "action"},
        {"request_id", static_cast<unsigned long long>(requestId)},
        {"payload", json::object({{"action", action}, {"value", value}})}
    });
}

inline json::Value makeActionResult(std::uint64_t requestId,
                                    bool accepted,
                                    bool executed,
                                    const std::string& reason = {}) {
    return json::object({
        {"protocol", kProtocolVersion},
        {"type", "action_result"},
        {"request_id", static_cast<unsigned long long>(requestId)},
        {"payload", json::object({
            {"accepted", accepted},
            {"executed", executed},
            {"reason", reason}
        })}
    });
}

inline bool validProtocol(const json::Value& message) {
    const auto* protocol = message.find("protocol");
    return protocol && static_cast<int>(protocol->numberOr()) == kProtocolVersion;
}

inline std::string messageType(const json::Value& message) {
    const auto* type = message.find("type");
    return type ? type->stringOr() : std::string{};
}

inline std::optional<AircraftAction> parseAction(const json::Value& message) {
    if (!validProtocol(message) || messageType(message) != "action") return std::nullopt;
    const auto* requestId = message.find("request_id");
    const auto* payload = message.find("payload");
    if (!requestId || !payload || !payload->isObject()) return std::nullopt;
    const auto* actionName = payload->find("action");
    const auto* value = payload->find("value");
    if (!actionName || !actionName->isString() || !value || !value->isNumber()) return std::nullopt;
    const double idValue = requestId->numberOr(-1.0);
    if (!std::isfinite(idValue) || idValue < 0.0) return std::nullopt;
    return AircraftAction{static_cast<std::uint64_t>(idValue), actionName->stringOr(), value->numberOr()};
}

inline std::optional<FlightState> parseState(const json::Value& message) {
    if (!validProtocol(message) || messageType(message) != "state") return std::nullopt;
    const auto* sequence = message.find("sequence");
    const auto* payload = message.find("payload");
    if (!sequence || !payload || !payload->isObject()) return std::nullopt;
    FlightState state;
    state.sequence = static_cast<std::uint64_t>(sequence->numberOr());
    if (const auto* v = payload->find("altitude_ft")) state.altitudeFt = v->numberOr();
    if (const auto* v = payload->find("ias_kt")) state.indicatedAirspeedKt = v->numberOr();
    if (const auto* v = payload->find("vertical_speed_fpm")) state.verticalSpeedFpm = v->numberOr();
    if (const auto* v = payload->find("mcp_heading_deg")) state.mcpHeadingDeg = v->numberOr();
    if (const auto* v = payload->find("on_ground")) state.onGround = v->boolOr();
    return state;
}

} // namespace copilot
