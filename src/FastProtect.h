#pragma once

// FastCodeProtect (ProtectLogic.h): xOBSE 22.13's import of VirtualProtect
// answered from the pages already held writable.

namespace framefixes {

using LogFn = void (*)(const char* format, ...);

// Points xOBSE's import slot for VirtualProtect (obse_1_2_416.dll RVA
// 0xAA02C, checked to hold the system's VirtualProtect) here, and adds the
// fault handler for a held page made read-only behind it. False and nothing
// changed when xOBSE is not loaded or the slot holds something else. With
// check on, every answered call also asks the system what the page is, and
// a page that is not writable is logged as MISMATCH and made writable.
bool InstallCodeProtect(bool check, LogFn log);

}  // namespace framefixes
