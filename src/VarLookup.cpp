#include "VarLookup.h"

#include <windows.h>

#include "EntryDetour.h"
#include "Gains.h"
#include "GainsLogic.h"
#include "Memory.h"
#include "Types.h"
#include "VarLookupLogic.h"
#include "VarLookupStub.h"

namespace framefixes {
namespace {

// The engine's free of a list's variables: thiscall(ScriptEventList*), no
// arguments, plain ret. Its first six bytes in 1.2.416 - push esi; mov esi,
// ecx; mov eax,[esi+0Ch] - are whole instructions with nothing relative in
// them.
constexpr UInt32 kFreeVariables = 0x004FA080;
constexpr UInt8 kFreeVariablesBytes[] = {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x0C};

constexpr const char* kXobseDll = "obse_1_2_416.dll";

// __fastcall with a dummy edx mirrors __thiscall's register use.
using FreeFn = void(__fastcall*)(EngineEventList* list, void* edx);
FreeFn g_originalFree = nullptr;
LogFn g_log = nullptr;
bool g_check = false;

// The cache is touched under one lock: the walk runs on the game's thread,
// but whether every free of a list does is not known.
SRWLOCK g_lock = SRWLOCK_INIT;
VarLookupCache* g_cache = nullptr;
UInt32 g_mismatches = 0;
UInt32 g_mismatchLinesLeft = 8;
UInt32 g_nextReport = 0;
bool g_firstIndexSaid = false;
GainSampler g_gains;
GainBuckets g_buckets;
ReportClock g_gainsClock;
long long g_gainsStart = 0;

void ReportLocked() {
	const VarLookupStats& s = g_cache->Stats();
	g_log("Variable lookup: %u lookups, %u from an index, %u walked; indexes %u built, %u held (%u pointers), "
	      "dropped %u for a changed head, %u freed by the engine, %u for room; %u refused%s%u mismatches",
	      s.lookups, s.fromIndex, s.walked, s.built, g_cache->IndexesHeld(), g_cache->PointersHeld(), s.rebuilt, s.forgotten, s.evicted, s.refused, g_check ? "; check: " : "; ",
	      g_mismatches);
}

void WriteGainsLocked(long long now, long long frequency) {
	char line[400];
	if (FormatGain(line, sizeof(line), "Variable lookup", "lookups answered from an index", g_gains,
	               g_cache->Stats().fromIndex, frequency, static_cast<double>(now - g_gainsStart) / frequency / 60.0)) {
		g_log("%s", line);
	}
	for (UInt32 b = 0; b < kWalkBuckets; ++b) {
		if (FormatBucket(line, sizeof(line), g_buckets, b, frequency)) g_log("%s", line);
	}
}

EngineVar* __cdecl LookupVariable(const EngineEventList* list, UInt32 id) {
	AcquireSRWLockExclusive(&g_lock);
	// One lookup in 1024 answered from an index is also walked the old way,
	// both timed (src/GainsLogic.h).
	const bool sample = !g_check && ReportGains() && g_gains.Due();
	const UInt32 fromIndexBefore = g_cache->Stats().fromIndex;
	const long long t0 = sample ? GainsCounter() : 0;
	EngineVar* found = g_cache->Find(list, id);
	if (sample && g_cache->Stats().fromIndex != fromIndexBefore) {
		const long long t1 = GainsCounter();
		UInt32 nodes = 0;
		WalkVariables(*list, id, &nodes);
		const long long t2 = GainsCounter();
		g_gains.Add(t1 - t0, t2 - t1);
		g_buckets.Add(nodes, t1 - t0, t2 - t1);
	}
	if (sample) {
		const long long now = GainsCounter();
		if (g_gainsStart == 0) g_gainsStart = now;
		const long long frequency = GainsFrequency();
		if (g_gainsClock.Due(now, frequency, GainsPeriod())) WriteGainsLocked(now, frequency);
	}
	if (g_check) {
		EngineVar* walked = WalkVariables(*list, id, nullptr);
		if (walked != found) {
			++g_mismatches;
			if (g_mismatchLinesLeft != 0) {
				--g_mismatchLinesLeft;
				g_log("Variable lookup: MISMATCH - list %08X, id %u: the cache answered %08X, the walk %08X",
				      static_cast<UInt32>(reinterpret_cast<UINT_PTR>(list)), id & 0xFFFF,
				      static_cast<UInt32>(reinterpret_cast<UINT_PTR>(found)),
				      static_cast<UInt32>(reinterpret_cast<UINT_PTR>(walked)));
			}
		}
		found = walked;
	}
	const VarLookupStats& s = g_cache->Stats();
	if (!g_firstIndexSaid && s.built != 0) {
		g_firstIndexSaid = true;
		g_log("Variable lookup: the first index built, for a list of %u ids", g_cache->PointersHeld());
	}
	if (s.lookups >= g_nextReport) {
		g_nextReport = s.lookups + (g_check ? 200000u : 1000000u);
		ReportLocked();
	}
	ReleaseSRWLockExclusive(&g_lock);
	return found;
}

void __fastcall OnFreeVariables(EngineEventList* list, void* /*edx*/) {
	AcquireSRWLockExclusive(&g_lock);
	g_cache->Forget(list);
	ReleaseSRWLockExclusive(&g_lock);
	g_originalFree(list, nullptr);
}

bool InstallFreeHook(LogFn log) {
	if (!Verify(kFreeVariables, kFreeVariablesBytes, sizeof(kFreeVariablesBytes))) {
		char bytes[32];
		DescribeBytes(kFreeVariables, bytes, sizeof(bytes));
		log("Variable lookup: the engine's free of a list's variables at %08X reads %s, not the game's own bytes - "
		    "without it the cache could not forget a freed list; xOBSE walks as before",
		    kFreeVariables, bytes);
		return false;
	}
	constexpr UInt32 capacity = 24;
	auto* trampoline = static_cast<UInt8*>(AllocExecutable(capacity));
	if (trampoline == nullptr ||
	    BuildEntryTrampoline(trampoline, capacity, reinterpret_cast<UInt32>(trampoline), kFreeVariables,
	                         kFreeVariablesBytes, sizeof(kFreeVariablesBytes)) == 0) {
		log("Variable lookup: no trampoline for the engine's free; xOBSE walks as before");
		return false;
	}
	UInt8 patch[sizeof(kFreeVariablesBytes)];
	if (BuildEntryPatch(patch, sizeof(patch), kFreeVariables, reinterpret_cast<UInt32>(&OnFreeVariables),
	                    sizeof(kFreeVariablesBytes)) == 0) {
		log("Variable lookup: the free's patch did not fit; xOBSE walks as before");
		return false;
	}
	g_originalFree = reinterpret_cast<FreeFn>(trampoline);
	if (!SafeWrite(kFreeVariables, patch, sizeof(patch))) {
		g_originalFree = nullptr;
		log("Variable lookup: could not write the free's patch; xOBSE walks as before");
		return false;
	}
	return true;
}

}  // namespace

void WriteVarLookupGainsNow() {
	if (g_cache == nullptr || g_log == nullptr) return;
	AcquireSRWLockExclusive(&g_lock);
	const long long now = GainsCounter();
	const long long frequency = GainsFrequency();
	if (g_gainsStart != 0) WriteGainsLocked(now, frequency);
	g_gainsClock.Restart(now, frequency, GainsPeriod());
	ReleaseSRWLockExclusive(&g_lock);
}

bool InstallVarLookup(bool check, LogFn log) {
	static bool s_done = false;
	if (s_done) return g_originalFree != nullptr;
	s_done = true;
	g_log = log;
	g_check = check;

	HMODULE xobse = GetModuleHandleA(kXobseDll);
	if (xobse == nullptr) {
		log("Variable lookup: %s is not loaded; nothing to patch", kXobseDll);
		return false;
	}
	const UInt32 base = static_cast<UInt32>(reinterpret_cast<UINT_PTR>(xobse));
	const UInt32 window = base + kVarLookupWindowRva;
	if (!Verify(window, kVarLookupWindow, sizeof(kVarLookupWindow))) {
		char bytes[32];
		DescribeBytes(base + kVarLookupSiteRva, bytes, sizeof(bytes));
		log("Variable lookup: xOBSE's variable walk at %s+%05X reads %s, not xOBSE 22.13's - another xOBSE "
		    "version; it walks as before",
		    kXobseDll, kVarLookupSiteRva, bytes);
		return false;
	}

	g_cache = new (std::nothrow) VarLookupCache();
	auto* stub = static_cast<UInt8*>(AllocExecutable(32));
	if (g_cache == nullptr || stub == nullptr) {
		log("Variable lookup: no memory for the cache; xOBSE walks as before");
		return false;
	}
	const UInt32 stubAddress = reinterpret_cast<UInt32>(stub);
	if (BuildVarLookupStub(stub, 32, stubAddress, reinterpret_cast<UInt32>(&LookupVariable),
	                       base + kVarLookupReturnRva) == 0) {
		log("Variable lookup: the stub did not fit; xOBSE walks as before");
		return false;
	}
	UInt8 site[kVarLookupSiteLength];
	if (BuildEntryPatch(site, sizeof(site), base + kVarLookupSiteRva, stubAddress, kVarLookupSiteLength) == 0) {
		log("Variable lookup: the walk's patch did not fit; xOBSE walks as before");
		return false;
	}

	// The free first: once the walk calls the cache, every freed list must
	// already be forgotten.
	if (!InstallFreeHook(log)) return false;
	if (!SafeWrite(base + kVarLookupSiteRva, site, sizeof(site))) {
		log("Variable lookup: could not write the walk's patch; xOBSE walks as before (the free's hook stays and "
		    "forgets nothing)");
		return false;
	}
	log("Variable lookup: indexed - xOBSE's variable walk at %s+%05X asks a cache of long lists' variables by id; "
	    "the engine's free at %08X makes it forget a list%s",
	    kXobseDll, kVarLookupSiteRva, kFreeVariables,
	    check ? "; check mode walks every lookup as well and uses the walk's answer" : "");
	return true;
}

}  // namespace framefixes
