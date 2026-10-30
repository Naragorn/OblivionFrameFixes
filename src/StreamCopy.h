#pragma once

// The engine's per-vertex stream copy replaced by one that decides the
// element size once (StreamCopyLogic.h for what the engine does and why it
// costs a fifth of a scene render with people in view). The detour on
// 0x0076E610 (Oblivion 1.2.416).

namespace framefixes {

using LogFn = void (*)(const char* format, ...);

// Installs the detour once and logs the outcome; false when the engine's
// bytes at the entry are not the ones of 1.2.416 or the system refused
// memory. With check on, every copy runs the engine's own routine first and
// compares the bytes and the counted total with the fast copy's - the proof
// the two agree in the game; a difference is logged as MISMATCH.
bool InstallStreamCopy(bool check, LogFn log);
// The "Gains:" line now, out of turn (after a load, on quitting).
void WriteStreamCopyGainsNow();

}  // namespace framefixes
