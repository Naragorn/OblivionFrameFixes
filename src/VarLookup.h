#pragma once

// xOBSE's variable lookup by index instead of by list walk (VarLookupLogic.h
// for what xOBSE and the engine do and why a script with thousands of
// variables costs a long frame). Two patches: an entry detour on the
// engine's 0x004FA080, which frees a list's variables, so the cache forgets
// the list first; and xOBSE 22.13's inlined walk in ScriptToken::ReadFrom
// (obse_1_2_416.dll RVA 0x91479, VarLookupStub.h) made a call to the cache.

namespace framefixes {

using LogFn = void (*)(const char* format, ...);

// Installs both patches once and logs the outcome; false when xOBSE's DLL is
// not loaded, its bytes are not 22.13's, the engine's entry is not
// 1.2.416's, or the system refused memory - then nothing is patched and
// xOBSE walks as before. With check on, every lookup is also walked, the
// walk's answer is used and a different answer from the cache is logged as
// MISMATCH: the proof the two agree in the game.
bool InstallVarLookup(bool check, LogFn log);
// The "Gains:" lines now, out of turn (after a load, on quitting).
void WriteVarLookupGainsNow();

}  // namespace framefixes
