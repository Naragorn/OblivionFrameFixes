#pragma once

// xOBSE's event list lookup with the game's RTTI cast answered from a cache
// (CastCacheLogic.h for what xOBSE does and why the answer can be cached;
// EventListSites.h for the three call sites in xOBSE 22.13).

namespace framefixes {

using LogFn = void (*)(const char* format, ...);

// Points the three casts of xOBSE's inlined EventListFromForm at the cache
// once and logs the outcome; false when xOBSE's DLL is not loaded, its bytes
// are not 22.13's, or the system refused memory - then nothing is patched.
// With check on, every cast is also asked of the game, the game's answer is
// used and a different answer from the cache is logged as MISMATCH.
bool InstallCastCache(bool check, LogFn log);

}  // namespace framefixes
