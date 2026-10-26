#pragma once

// A trace of the engine's own variable lookup 0x004FA110
// (EngineVarTraceLogic.h for what it does and why it is traced). A
// diagnostic: nothing the game does changes, only the log gets a line for
// every burst of lookups that held a thousand of them or a millisecond.

namespace framefixes {

using LogFn = void (*)(const char* format, ...);

// Detours the lookup's entry once and logs the outcome; false when its bytes
// are not 1.2.416's (another plugin hooks it) or the system refused memory -
// then nothing is changed.
bool InstallEngineVarTrace(LogFn log);

}  // namespace framefixes
