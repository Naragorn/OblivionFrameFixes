#pragma once

// The engine's frame clock made precise - FrameClockLogic.h for what the
// engine does and why every frame rate suffers from it. The detour on the
// time info's update at 0x0047D170 (Oblivion 1.2.416).

namespace framefixes {

using LogFn = void (*)(const char* format, ...);

// Installs the detour once and logs the outcome; false when the engine's
// bytes at the entry are not the ones of 1.2.416 (another plugin's patch, or
// another version) or the system refused memory. With trace on, a log line
// every ten seconds reports the frame lengths the engine's own clock
// produced and the lengths the frames were given.
bool InstallFrameClock(bool trace, LogFn log);

}  // namespace framefixes
