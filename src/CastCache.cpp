#include "CastCache.h"

#include <windows.h>

#include <new>

#include "CastCacheLogic.h"
#include "EventListSites.h"
#include "Memory.h"
#include "Types.h"

namespace framefixes {
namespace {

// The game's __RTDynamicCast (the MSVC runtime's, linked into Oblivion.exe
// 1.2.416): cdecl(object, vtable displacement, source type, target type,
// is a reference cast) - xOBSE's Oblivion_DynamicCast.
constexpr UInt32 kGameCast = 0x009832E6;
using CastFn = void*(__cdecl*)(void* object, SInt32 vtableDisplacement, void* sourceType, void* targetType,
                               SInt32 isReference);
const CastFn g_gameCast = reinterpret_cast<CastFn>(kGameCast);

constexpr const char* kXobseDll = "obse_1_2_416.dll";

LogFn g_log = nullptr;
bool g_check = false;

// The casts run on the game's script thread; the lock is there because
// whether every caller of these three sites does is not known.
SRWLOCK g_lock = SRWLOCK_INIT;
CastCache* g_cache = nullptr;
UInt32 g_casts = 0;
UInt32 g_fromCache = 0;
UInt32 g_asked = 0;
UInt32 g_refused = 0;
UInt32 g_mismatches = 0;
UInt32 g_mismatchLinesLeft = 8;
UInt32 g_nextReport = 0;
bool g_firstSaid = false;

// The object's vtable, or false when reading it faults - then the game's
// cast reads it under its own handler and answers (or throws) as always.
bool ReadVtable(const void* object, UInt32* vtable) {
	__try {
		*vtable = *static_cast<const UInt32*>(object);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void ReportLocked() {
	g_log("Event list cast: %u casts at xOBSE's three sites, %u from the cache, %u asked of the game; %u classes "
	      "held, %u refused for room%s%u mismatches",
	      g_casts, g_fromCache, g_asked, g_cache->Held(), g_refused, g_check ? "; check: " : "; ", g_mismatches);
}

void* __cdecl CachedCast(void* object, SInt32 vtableDisplacement, void* sourceType, void* targetType,
                         SInt32 isReference) {
	const UInt32 address = static_cast<UInt32>(reinterpret_cast<UINT_PTR>(object));
	UInt32 vtable = 0;
	if (!CastIsCacheable(address, vtableDisplacement, isReference) || !ReadVtable(object, &vtable) || vtable == 0) {
		return g_gameCast(object, vtableDisplacement, sourceType, targetType, isReference);
	}
	const CastKey key{vtable, static_cast<UInt32>(reinterpret_cast<UINT_PTR>(sourceType)),
	                  static_cast<UInt32>(reinterpret_cast<UINT_PTR>(targetType))};

	AcquireSRWLockExclusive(&g_lock);
	++g_casts;
	CastAnswer known{};
	const bool isKnown = g_cache->Find(key, &known);
	if (isKnown && !g_check) {
		++g_fromCache;
		if (g_casts >= g_nextReport) {
			g_nextReport = g_casts + 1000000u;
			ReportLocked();
		}
		ReleaseSRWLockExclusive(&g_lock);
		return reinterpret_cast<void*>(static_cast<UINT_PTR>(AnswerOfCast(address, known)));
	}
	ReleaseSRWLockExclusive(&g_lock);

	// Not known yet, or checked: the game's answer, asked outside the lock
	// (it may throw, and it calls nothing of ours).
	void* answer = g_gameCast(object, vtableDisplacement, sourceType, targetType, isReference);
	const UInt32 answered = static_cast<UInt32>(reinterpret_cast<UINT_PTR>(answer));
	const CastAnswer learned = DeltaOfCast(address, answered);

	AcquireSRWLockExclusive(&g_lock);
	++g_asked;
	if (isKnown) {
		if (AnswerOfCast(address, known) != answered) {
			++g_mismatches;
			if (g_mismatchLinesLeft != 0) {
				--g_mismatchLinesLeft;
				g_log("Event list cast: MISMATCH - object %08X (vtable %08X) from %08X to %08X: the cache answered "
				      "%08X, the game %08X",
				      address, vtable, key.sourceType, key.targetType, AnswerOfCast(address, known), answered);
			}
		}
	} else if (!g_cache->Store(key, learned)) {
		++g_refused;
	} else if (!g_firstSaid) {
		g_firstSaid = true;
		g_log("Event list cast: the first class learned - vtable %08X from %08X to %08X %s", vtable, key.sourceType,
		      key.targetType, learned.fails ? "fails" : "succeeds");
	}
	if (g_casts >= g_nextReport) {
		g_nextReport = g_casts + (g_check ? 200000u : 1000000u);
		ReportLocked();
	}
	ReleaseSRWLockExclusive(&g_lock);
	return answer;
}

}  // namespace

bool InstallCastCache(bool check, LogFn log) {
	static bool s_done = false;
	static bool s_made = false;
	if (s_done) return s_made;
	s_done = true;
	g_log = log;
	g_check = check;

	HMODULE xobse = GetModuleHandleA(kXobseDll);
	if (xobse == nullptr) {
		log("Event list cast: %s is not loaded; nothing to patch", kXobseDll);
		return false;
	}
	const UInt32 base = static_cast<UInt32>(reinterpret_cast<UINT_PTR>(xobse));
	if (!Verify(base + kEventListWindowRva, kEventListWindow, sizeof(kEventListWindow))) {
		char bytes[32];
		DescribeBytes(base + kEventListCastSiteRvas[0], bytes, sizeof(bytes));
		log("Event list cast: xOBSE's event list lookup at %s+%05X reads %s, not xOBSE 22.13's - another xOBSE "
		    "version; it casts as before",
		    kXobseDll, kEventListCastSiteRvas[0], bytes);
		return false;
	}
	g_cache = new (std::nothrow) CastCache();
	if (g_cache == nullptr) {
		log("Event list cast: no memory for the cache; xOBSE casts as before");
		return false;
	}
	const UInt32 target = static_cast<UInt32>(reinterpret_cast<UINT_PTR>(&CachedCast));
	UInt32 made = 0;
	for (UInt32 rva : kEventListCastSiteRvas) {
		// The `mov eax, imm32`'s operand, one byte after its opcode.
		if (SafeWrite(base + rva + 1, &target, sizeof(target))) {
			++made;
		} else {
			log("Event list cast: could not write the site at %s+%05X; that one casts as before", kXobseDll, rva);
		}
	}
	s_made = made != 0;
	if (s_made) {
		log("Event list cast: cached - %u of xOBSE's 3 casts in its event list lookup (%s+%05X) ask a cache of each "
		    "class's answer before the game's cast at %08X%s",
		    made, kXobseDll, kEventListWindowRva, kGameCast,
		    check ? "; check mode asks the game every time as well and uses its answer" : "");
	}
	return s_made;
}

}  // namespace framefixes
