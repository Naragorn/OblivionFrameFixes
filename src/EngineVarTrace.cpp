#include "EngineVarTrace.h"

#include <windows.h>

#include <new>

#include "EngineVarTraceLogic.h"
#include "EntryDetour.h"
#include "Memory.h"
#include "Types.h"

namespace framefixes {
namespace {

// The engine's variable lookup: thiscall(ScriptEventList*, id, script) ->
// double in st0, ret 8. Its first nine bytes in 1.2.416 - push esi; xor
// esi,esi; cmp [00B361BC],ecx - are whole instructions with nothing relative
// in them; xOBSE's switch at 0x004FA11E lies past them.
constexpr UInt32 kLookup = 0x004FA110;
constexpr UInt8 kLookupBytes[] = {0x56, 0x33, 0xF6, 0x39, 0x0D, 0xBC, 0x61, 0xB3, 0x00};

// __fastcall with a dummy edx mirrors __thiscall; both pop the two stack
// arguments and return the double in st0.
using LookupFn = double(__fastcall*)(EngineEventList* list, void* edx, UInt32 id, void* script);
LookupFn g_original = nullptr;
LogFn g_log = nullptr;

SRWLOCK g_lock = SRWLOCK_INIT;
BurstTracker* g_tracker = nullptr;
UInt64 g_ticksPerMs = 0;
UInt64 g_frequency = 0;
UInt64 g_lookups = 0;
UInt32 g_bursts = 0;

UInt64 Now() {
	LARGE_INTEGER value{};
	QueryPerformanceCounter(&value);
	return static_cast<UInt64>(value.QuadPart);
}

double Ms(UInt64 ticks) {
	return g_frequency == 0 ? 0.0 : static_cast<double>(ticks) * 1000.0 / static_cast<double>(g_frequency);
}

void ReportLocked(const LookupBurst& b) {
	g_log("Engine variable lookup: a burst of %u lookups over %.1f ms, %.2f ms inside the engine's lookup (%.2f us "
	      "each), %llu entries walked, the longest walk %u, %u not found; %u lists, the busiest %08X with %u "
	      "lookups (its longest walk %u)%s; %llu lookups so far in %u bursts",
	      b.calls, Ms(b.lastTick - b.firstTick), Ms(b.insideTicks), Ms(b.insideTicks) * 1000.0 / b.calls,
	      static_cast<unsigned long long>(b.walked), b.longestWalk, b.notFound, b.lists, b.busiestList,
	      b.busiestCalls, b.busiestLongestWalk, b.otherListCalls != 0 ? ", more lists beyond those" : "",
	      static_cast<unsigned long long>(g_lookups), g_bursts);
}

double __fastcall TracedLookup(EngineEventList* list, void* /*edx*/, UInt32 id, void* script) {
	// The walk counted before, outside the timed call: the count is the
	// trace's own cost, the time is the engine's.
	bool found = false;
	const UInt32 walked = list != nullptr ? EngineWalkLength(*list, id, &found) : 0;
	const UInt64 start = Now();
	const double value = g_original(list, nullptr, id, script);
	const UInt64 end = Now();

	AcquireSRWLockExclusive(&g_lock);
	++g_lookups;
	LookupBurst finished{};
	if (g_tracker->Record(start, end, walked, found,
	                      static_cast<UInt32>(reinterpret_cast<UINT_PTR>(list)), &finished)) {
		++g_bursts;
		if (BurstWorthReporting(finished, g_ticksPerMs)) {
			ReportLocked(finished);
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
	return value;
}

}  // namespace

bool InstallEngineVarTrace(LogFn log) {
	static bool s_done = false;
	static bool s_made = false;
	if (s_done) return s_made;
	s_done = true;
	g_log = log;

	if (!Verify(kLookup, kLookupBytes, sizeof(kLookupBytes))) {
		char bytes[32];
		DescribeBytes(kLookup, bytes, sizeof(bytes));
		log("Engine variable trace: the engine's variable lookup at %08X reads %s, not the game's own bytes - "
		    "another plugin hooks it; no trace",
		    kLookup, bytes);
		return false;
	}
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	g_frequency = static_cast<UInt64>(frequency.QuadPart);
	g_ticksPerMs = g_frequency / 1000;
	// Lookups 5 ms apart belong to one burst: a hitch's script work runs in
	// one stretch, the frames around it are 10 ms and more.
	g_tracker = new (std::nothrow) BurstTracker(g_ticksPerMs * 5);
	constexpr UInt32 capacity = 32;
	auto* trampoline = static_cast<UInt8*>(AllocExecutable(capacity));
	if (g_tracker == nullptr || trampoline == nullptr ||
	    BuildEntryTrampoline(trampoline, capacity, reinterpret_cast<UInt32>(trampoline), kLookup, kLookupBytes,
	                         sizeof(kLookupBytes)) == 0) {
		log("Engine variable trace: no memory for the trampoline; no trace");
		return false;
	}
	UInt8 patch[sizeof(kLookupBytes)];
	if (BuildEntryPatch(patch, sizeof(patch), kLookup, reinterpret_cast<UInt32>(&TracedLookup),
	                    sizeof(kLookupBytes)) == 0) {
		log("Engine variable trace: the patch did not fit; no trace");
		return false;
	}
	g_original = reinterpret_cast<LookupFn>(trampoline);
	if (!SafeWrite(kLookup, patch, sizeof(patch))) {
		g_original = nullptr;
		log("Engine variable trace: could not write the patch; no trace");
		return false;
	}
	s_made = true;
	log("Engine variable trace: on - the engine's variable lookup at %08X is timed; a line for every burst of "
	    "lookups (5 ms apart at most) with a thousand of them or a millisecond inside",
	    kLookup);
	return true;
}

}  // namespace framefixes
