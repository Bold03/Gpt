#include "PluginIpcServer.hpp"
#include "copilot/Protocol.hpp"
#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {
struct DataRefs {
    XPLMDataRef altitude{};
    XPLMDataRef airspeed{};
    XPLMDataRef verticalSpeed{};
    XPLMDataRef onGround{};
    XPLMDataRef mcpHeading{};
    bool initialized{};
};
DataRefs gRefs;
std::unique_ptr<PluginIpcServer> gIpc;
std::uint64_t gSequence = 0;

void logLine(const std::string& line) {
    const std::string output = "[CopilotAI] " + line + "\n";
    XPLMDebugString(output.c_str());
}

void bindDataRefs() {
    gRefs.altitude = XPLMFindDataRef("sim/cockpit2/gauges/indicators/altitude_ft_pilot");
    gRefs.airspeed = XPLMFindDataRef("sim/cockpit2/gauges/indicators/airspeed_kts_pilot");
    gRefs.verticalSpeed = XPLMFindDataRef("sim/cockpit2/gauges/indicators/vvi_fpm_pilot");
    gRefs.onGround = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    gRefs.mcpHeading = XPLMFindDataRef("sim/cockpit2/autopilot/heading_dial_deg_mag_pilot");
    gRefs.initialized = true;
    const bool ok = gRefs.altitude && gRefs.airspeed && gRefs.verticalSpeed && gRefs.onGround && gRefs.mcpHeading;
    logLine(ok ? "generic datarefs bound" : "one or more generic datarefs are missing");
}

float readFloat(XPLMDataRef ref) { return ref ? XPLMGetDataf(ref) : 0.0f; }
int readInt(XPLMDataRef ref) { return ref ? XPLMGetDatai(ref) : 0; }

copilot::FlightState readState() {
    copilot::FlightState state;
    state.sequence = ++gSequence;
    state.altitudeFt = readFloat(gRefs.altitude);
    state.indicatedAirspeedKt = readFloat(gRefs.airspeed);
    state.verticalSpeedFpm = readFloat(gRefs.verticalSpeed);
    state.mcpHeadingDeg = readFloat(gRefs.mcpHeading);
    state.onGround = readInt(gRefs.onGround) != 0;
    return state;
}

void executeAction(const copilot::AircraftAction& action) {
    if (!gIpc) return;
    if (action.action != "SET_HEADING") {
        gIpc->publish(copilot::makeActionResult(action.requestId, false, false, "unsupported_action"));
        return;
    }
    if (!std::isfinite(action.value) || action.value < 1.0 || action.value > 360.0) {
        gIpc->publish(copilot::makeActionResult(action.requestId, false, false, "invalid_heading"));
        return;
    }
    if (!gRefs.mcpHeading || !XPLMCanWriteDataRef(gRefs.mcpHeading)) {
        gIpc->publish(copilot::makeActionResult(action.requestId, false, false, "heading_dataref_not_writable"));
        return;
    }
    XPLMSetDataf(gRefs.mcpHeading, static_cast<float>(action.value));
    gIpc->publish(copilot::makeActionResult(action.requestId, true, true));
}

float flightLoop(float, float, int, void*) {
    if (!gRefs.initialized) bindDataRefs();
    if (gIpc) {
        while (auto action = gIpc->tryPopAction()) executeAction(*action);
        if (gIpc->connected()) gIpc->publish(copilot::makeState(readState()));
    }
    return 0.05f;
}
} // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSignature, char* outDescription) {
    std::strcpy(outName, "Copilot AI MVP");
    std::strcpy(outSignature, "com.example.copilotai.mvp");
    std::strcpy(outDescription, "Offline Copilot AI bridge: X-Plane main-thread adapter + local IPC");
    gIpc = std::make_unique<PluginIpcServer>();
    gIpc->start(copilot::kDefaultPort);
    XPLMRegisterFlightLoopCallback(flightLoop, -1.0f, nullptr);
    logLine("plugin started; IPC listening on 127.0.0.1:49075");
    return 1;
}

PLUGIN_API void XPluginStop() {
    XPLMUnregisterFlightLoopCallback(flightLoop, nullptr);
    if (gIpc) {
        gIpc->stop();
        gIpc.reset();
    }
    gRefs = {};
}
PLUGIN_API int XPluginEnable() { return 1; }
PLUGIN_API void XPluginDisable() {}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void*) {
    if (message == XPLM_MSG_PLANE_LOADED) gRefs = {};
}
