# Copilot AI MVP for X-Plane 11/12

This repository is the first compile-oriented scaffold for the offline bilingual Copilot AI project.
It intentionally proves the safety-critical path before adding Whisper, TTS, emotion, or a local LLM.

## What this MVP does

- `CopilotAI.xpl` runs inside X-Plane.
- All XPLM reads/writes occur from the X-Plane flight-loop/main thread.
- A background IPC thread only handles TCP and queues actions.
- `CopilotCore` connects to `127.0.0.1:49075`.
- X-Plane sends altitude, IAS, V/S, on-ground state, and selected heading.
- The console command `heading 270` sends a structured `SET_HEADING` request.
- The plugin writes the generic X-Plane heading selector.
- `CopilotCore` waits for simulator state to confirm the selected heading before printing `VERIFIED`.

This is deliberately **not yet the final Zibo/LevelUp adapter**. Custom aircraft commands/datarefs must be discovered and validated against the exact installed aircraft build, then placed behind the adapter layer.

## Why only SET_HEADING first?

It proves the complete control loop:

```text
Core intent -> TCP -> plugin queue -> X-Plane main thread -> DataRef write
           -> state readback -> TCP -> Core verification
```

Once this works reliably, altitude, speed, LNAV, VNAV, gear, flaps, checklists, Whisper and TTS can be added without redesigning IPC.

## Requirements

### Core only

- CMake 3.24+
- C++20 compiler
- No third-party JSON dependency is required for this MVP; the fixed IPC schema uses a small internal codec.

### X-Plane plugin

- X-Plane 11.10+ or X-Plane 12
- Current X-Plane SDK headers/stubs (the project is configured for XPLM300 API compatibility)
- Windows: Visual Studio 2022 recommended

## Build CopilotCore

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOPILOT_BUILD_PLUGIN=OFF
cmake --build build --config Release --parallel
```

## Build Windows plugin

Download and extract XPSDK 4.3.0, then point `XPLANE_SDK` at the extracted `SDK` directory:

```powershell
cmake -S . -B build `
  -DCMAKE_BUILD_TYPE=Release `
  -DCOPILOT_BUILD_PLUGIN=ON `
  -DXPLANE_SDK="C:/dev/XPSDK430/SDK"

cmake --build build --config Release --parallel
```

The plugin target is emitted as `CopilotAI.xpl`.

## Install in X-Plane on Windows

Create:

```text
X-Plane 12/
  Resources/
    plugins/
      CopilotAI/
        win_x64/
          CopilotAI.xpl
```

Copy the built `.xpl` to `win_x64`.

Then start X-Plane and inspect `Log.txt` for:

```text
[CopilotAI] plugin started; IPC listening on 127.0.0.1:49075
[CopilotAI] generic datarefs bound
```

## Run the core

With X-Plane running and the plugin loaded:

```powershell
CopilotCore.exe
```

Then:

```text
> heading 270
```

Expected flow:

```text
[ACTION] #1 accepted=1 executed=1
[VERIFIED] request #1 heading 270 set.
```

## Important safety architecture

The socket thread never calls XPLM.

```text
IPC thread
   |
   +--> parse JSON
   +--> ActionQueue
             |
             v
X-Plane flight loop/main thread
   |
   +--> validate whitelist + bounds
   +--> XPLMSetDataf
   +--> read state
```

Incoming messages cannot specify arbitrary dataref names. Only whitelisted semantic actions are accepted.

## Generic datarefs in this proof

Read telemetry:

- `sim/cockpit2/gauges/indicators/altitude_ft_pilot`
- `sim/cockpit2/gauges/indicators/airspeed_kts_pilot`
- `sim/cockpit2/gauges/indicators/vvi_fpm_pilot`
- `sim/flightmodel/failures/onground_any`

Generic heading selector:

- `sim/cockpit2/autopilot/heading_dial_deg_mag_pilot`

For Zibo/LevelUp production use, do not assume this generic write is the best cockpit-control mechanism. The next implementation step is an aircraft-profile adapter that maps semantic actions to validated aircraft-specific commands/datarefs and verifies the resulting state.

## Next implementation order

1. `IAircraftAdapter` + aircraft auto-detection.
2. Zibo profile validated with DataRefTool/commands.
3. LevelUp profile.
4. `SET_ALTITUDE`, `SET_SPEED`, LNAV/VNAV, gear and flaps.
5. Deterministic bilingual NumberParser/IntentResolver.
6. Whisper.cpp worker and microphone/VAD.
7. TTS manager and barge-in/echo suppression.
8. SQLite interaction logger.
9. Emotion/workload state machine.
10. llama.cpp only as conversational/low-confidence structured fallback.

## Verification performed for this scaffold

- `CopilotCore` was configured and compiled successfully with GCC/C++20 in an offline environment.
- The NDJSON protocol was smoke-tested against a local mock endpoint: `SET_HEADING 270` received an action acknowledgement and was only marked verified after a state update reported MCP heading 270.
- Plugin translation units were syntax-checked against minimal XPLM-compatible declarations. Actual linking/loading is performed by the included Windows GitHub Actions job using Laminar's XPSDK 4.3.0.
