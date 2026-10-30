#include "FastProtect.h"

#include <windows.h>

#include "Gains.h"
#include "GainsLogic.h"
#include "Memory.h"
#include "ProtectLogic.h"
#include "Types.h"

namespace framefixes {
namespace {

constexpr const char* kXobseDll = "obse_1_2_416.dll";
// The import address table's slot for KERNEL32!VirtualProtect in xOBSE 22.13
// (read 2026-10-28: 41 loads and calls of [0x100AA02C], image base 0x10000000).
constexpr UInt32 kVirtualProtectSlotRva = 0xAA02C;

using ProtectFn = BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD);
ProtectFn g_real = nullptr;
LogFn g_log = nullptr;
bool g_check = false;

SRWLOCK g_lock = SRWLOCK_INIT;
ProtectCache g_cache;
UInt32 g_fast = 0;
UInt32 g_real_calls = 0;
UInt32 g_mended = 0;
UInt32 g_mismatches = 0;
UInt32 g_lines = 8;
GainSampler g_gains;
ReportClock g_gainsClock;
long long g_gainsStart = 0;

void WriteGainsLocked(long long now, long long frequency) {
	char line[400];
	if (FormatGain(line, sizeof(line), "Code protection", "calls answered without the system", g_gains, g_fast,
	               frequency, static_cast<double>(now - g_gainsStart) / frequency / 60.0)) {
		g_log("%s", line);
	}
}

BOOL WINAPI FastVirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, PDWORD oldProtect) {
	const UInt32 a = static_cast<UInt32>(reinterpret_cast<UINT_PTR>(address));
	const UInt32 n = static_cast<UInt32>(size);
	AcquireSRWLockExclusive(&g_lock);
	const ProtectRoute route = Route(g_cache, a, n, newProtect);
	if (route == ProtectRoute::Fast) {
		++g_fast;
		if (g_fast % 20000 == 0) {
			g_log("Code protection: %u calls answered, %u to the system, %u pages held, %u faults mended, %u mismatches",
			      g_fast, g_real_calls, g_cache.count, g_mended, g_mismatches);
		}
		if (g_check) {
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(address, &info, sizeof(info)) != 0 && !StillWritable(info.Protect)) {
				++g_mismatches;
				DWORD ignored = 0;
				g_real(address, size, PAGE_EXECUTE_READWRITE, &ignored);
				if (g_lines != 0) {
					--g_lines;
					g_log("Code protection: MISMATCH - %08X was held writable but is %08X; made writable", a,
					      static_cast<UInt32>(info.Protect));
				}
			}
		}
		// One answered call in 1024 asks the system as well, timed: the call
		// that was saved (src/GainsLogic.h).
		if (!g_check && ReportGains() && g_gains.Due()) {
			const long long t0 = GainsCounter();
			DWORD ignored = 0;
			g_real(address, size, PAGE_EXECUTE_READWRITE, &ignored);
			const long long t1 = GainsCounter();
			g_gains.Add(0, t1 - t0);
			const long long frequency = GainsFrequency();
			if (g_gainsStart == 0) g_gainsStart = t1;
			if (g_gainsClock.Due(t1, frequency, GainsPeriod())) WriteGainsLocked(t1, frequency);
		}
		ReleaseSRWLockExclusive(&g_lock);
		if (oldProtect != nullptr) *oldProtect = ReportedOld();
		return TRUE;
	}
	++g_real_calls;
	const BOOL ok = g_real(address, size, newProtect, oldProtect);
	if (ok && route == ProtectRoute::RealThenHold) {
		g_cache.Hold(a, n);
		if (oldProtect != nullptr) *oldProtect = ReportedOld();
	} else if (route == ProtectRoute::RealThenForget) {
		g_cache.Forget(a, n);
	}
	ReleaseSRWLockExclusive(&g_lock);
	return ok;
}

// A write into a held page that faults: something made it read-only behind
// the cache. The page is made writable again and the write runs.
LONG CALLBACK OnFault(EXCEPTION_POINTERS* info) {
	const EXCEPTION_RECORD* r = info->ExceptionRecord;
	if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
	const bool write = r->ExceptionInformation[0] == 1;
	const UInt32 at = static_cast<UInt32>(r->ExceptionInformation[1]);
	AcquireSRWLockExclusive(&g_lock);
	const bool ours = FaultIsOurs(g_cache, write, at);
	ReleaseSRWLockExclusive(&g_lock);
	if (!ours) return EXCEPTION_CONTINUE_SEARCH;
	DWORD ignored = 0;
	if (!g_real(reinterpret_cast<LPVOID>(static_cast<UINT_PTR>(at)), 1, PAGE_EXECUTE_READWRITE, &ignored)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	++g_mended;
	return EXCEPTION_CONTINUE_EXECUTION;
}

}  // namespace

void WriteCodeProtectGainsNow() {
	if (g_real == nullptr || g_log == nullptr) return;
	AcquireSRWLockExclusive(&g_lock);
	const long long now = GainsCounter();
	const long long frequency = GainsFrequency();
	if (g_gainsStart != 0) WriteGainsLocked(now, frequency);
	g_gainsClock.Restart(now, frequency, GainsPeriod());
	ReleaseSRWLockExclusive(&g_lock);
}

bool InstallCodeProtect(bool check, LogFn log) {
	static bool s_done = false;
	if (s_done) return g_real != nullptr;
	s_done = true;
	g_log = log;
	g_check = check;
	HMODULE xobse = GetModuleHandleA(kXobseDll);
	if (xobse == nullptr) {
		log("Code protection: %s is not loaded; nothing to patch", kXobseDll);
		return false;
	}
	const UInt32 slot = static_cast<UInt32>(reinterpret_cast<UINT_PTR>(xobse)) + kVirtualProtectSlotRva;
	const UINT_PTR held = *reinterpret_cast<const UINT_PTR*>(static_cast<UINT_PTR>(slot));
	FARPROC k32 = GetProcAddress(GetModuleHandleA("kernel32.dll"), "VirtualProtect");
	HMODULE kb = GetModuleHandleA("kernelbase.dll");
	FARPROC kbp = kb != nullptr ? GetProcAddress(kb, "VirtualProtect") : nullptr;
	if (held == 0 || (held != reinterpret_cast<UINT_PTR>(k32) && held != reinterpret_cast<UINT_PTR>(kbp))) {
		log("Code protection: xOBSE's import slot at %s+%05X holds %08X, not the system's VirtualProtect - another "
		    "xOBSE version or another plugin's hook; xOBSE asks the system as before",
		    kXobseDll, kVirtualProtectSlotRva, static_cast<UInt32>(held));
		return false;
	}
	g_real = reinterpret_cast<ProtectFn>(held);
	if (AddVectoredExceptionHandler(0, &OnFault) == nullptr) {
		g_real = nullptr;
		log("Code protection: no fault handler; xOBSE asks the system as before");
		return false;
	}
	const UINT_PTR fast = reinterpret_cast<UINT_PTR>(&FastVirtualProtect);
	if (!SafeWrite(slot, &fast, sizeof(fast))) {
		log("Code protection: could not write xOBSE's import slot; xOBSE asks the system as before");
		return false;
	}
	log("Code protection: fast - xOBSE's VirtualProtect (%s+%05X) answered from the game's code pages already made "
	    "writable, the first ask to the system%s",
	    kXobseDll, kVirtualProtectSlotRva, check ? "; check mode asks the system what each answered page is" : "");
	return true;
}

}  // namespace framefixes
