// The cast cache's decisions (CastCacheLogic.h) and the patch sites'
// constants (EventListSites.h): what may be answered from the cache, what
// the cache answers, how full it may get, and that the three patch offsets
// sit on xOBSE 22.13's `mov eax, 009832E6` in the window that is checked.

#include <cstdio>
#include <cstring>

#include "CastCacheLogic.h"
#include "EventListSites.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++g_failures;
	}
}

using framefixes::AnswerOfCast;
using framefixes::CastAnswer;
using framefixes::CastCache;
using framefixes::CastIsCacheable;
using framefixes::CastKey;
using framefixes::DeltaOfCast;

// Types as xOBSE names them for EventListFromForm (RTTI type descriptors in
// Oblivion.exe 1.2.416).
constexpr UInt32 kTESForm = 0x00B02F9C;
constexpr UInt32 kTESObjectREFR = 0x00B03054;
constexpr UInt32 kTESQuest = 0x00B05468;

void TestCacheable() {
	std::printf("What may be answered from the cache\n");
	Check(CastIsCacheable(0x12345678, 0, 0), "an object, no vtable displacement, a pointer cast");
	Check(!CastIsCacheable(0, 0, 0), "no object: the engine answers (it answers null)");
	Check(!CastIsCacheable(0x12345678, 4, 0), "a vtable displacement: the engine answers");
	Check(!CastIsCacheable(0x12345678, 0, 1), "a reference cast, which throws on failure: the engine answers");
}

void TestDeltas() {
	std::printf("The answer as an offset from the object\n");
	CastAnswer a = DeltaOfCast(0x20000000, 0x20000000);
	Check(!a.fails && a.delta == 0 && AnswerOfCast(0x30000000, a) == 0x30000000, "the same object: offset 0");
	a = DeltaOfCast(0x20000044, 0x20000000);
	Check(!a.fails && a.delta == -0x44 && AnswerOfCast(0x31000044, a) == 0x31000000,
	      "a base further in: a negative offset, applied to another object of the type");
	a = DeltaOfCast(0x20000000, 0);
	Check(a.fails && AnswerOfCast(0x30000000, a) == 0, "a failed cast: null for every object of the type");
}

void TestFindAndStore() {
	std::printf("Find and store\n");
	static CastCache cache;
	cache.Clear();
	CastAnswer got{};
	const CastKey refAsRefr{0x00A6FC9C, kTESForm, kTESObjectREFR};
	Check(!cache.Find(refAsRefr, &got), "an empty cache knows nothing");
	Check(cache.Store(refAsRefr, CastAnswer{0, false}), "a reference's cast to TESObjectREFR is stored");
	Check(cache.Find(refAsRefr, &got) && !got.fails && got.delta == 0, "and found again");
	const CastKey questAsRefr{0x00A71E2C, kTESForm, kTESObjectREFR};
	const CastKey questAsQuest{0x00A71E2C, kTESForm, kTESQuest};
	Check(cache.Store(questAsRefr, CastAnswer{0, true}) && cache.Store(questAsQuest, CastAnswer{0, false}),
	      "a quest: its failed cast to TESObjectREFR and its cast to TESQuest");
	Check(cache.Find(questAsRefr, &got) && got.fails, "the failure is kept apart");
	Check(cache.Find(questAsQuest, &got) && !got.fails && got.delta == 0, "from the success of another target");
	const CastKey otherSource{0x00A71E2C, 0x00B03850, kTESQuest};
	Check(!cache.Find(otherSource, &got), "the same vtable and target from another source type is not known");
	Check(!cache.Store(CastKey{0, kTESForm, kTESQuest}, CastAnswer{0, false}), "a vtable of zero is refused");
	Check(cache.Held() == 3, "three answers held");
	Check(cache.Store(refAsRefr, CastAnswer{0, false}) && cache.Held() == 3, "storing a known key again holds no more");
}

void TestFull() {
	std::printf("A full cache\n");
	static CastCache cache;
	cache.Clear();
	UInt32 stored = 0;
	// Vtables four bytes apart, as a table of classes would have them: the
	// hash has to spread neighbours.
	for (UInt32 i = 0; i < CastCache::kMaxHeld; ++i) {
		if (cache.Store(CastKey{0x00A60000 + i * 4, kTESForm, kTESObjectREFR},
		                CastAnswer{static_cast<SInt32>(i), (i % 3) == 0})) {
			++stored;
		}
	}
	Check(stored == CastCache::kMaxHeld && cache.Held() == CastCache::kMaxHeld, "fills to its limit");
	UInt32 found = 0;
	bool allRight = true;
	for (UInt32 i = 0; i < CastCache::kMaxHeld; ++i) {
		CastAnswer got{};
		if (cache.Find(CastKey{0x00A60000 + i * 4, kTESForm, kTESObjectREFR}, &got)) {
			++found;
			allRight = allRight && got.delta == static_cast<SInt32>(i) && got.fails == ((i % 3) == 0);
		}
	}
	Check(found == CastCache::kMaxHeld && allRight, "and finds every answer, each its own");
	Check(!cache.Store(CastKey{0x00B00000, kTESForm, kTESObjectREFR}, CastAnswer{0, false}),
	      "past the limit a new key is refused (the engine keeps answering it)");
	CastAnswer got{};
	Check(!cache.Find(CastKey{0x00B00000, kTESForm, kTESObjectREFR}, &got), "and is not found");
	Check(cache.Store(CastKey{0x00A60000, kTESForm, kTESObjectREFR}, CastAnswer{0, true}),
	      "a known key can still be stored when full");
	cache.Clear();
	Check(cache.Held() == 0 && !cache.Find(CastKey{0x00A60004, kTESForm, kTESObjectREFR}, &got),
	      "cleared, it is empty");
}

void TestSites() {
	std::printf("The patch sites\n");
	using framefixes::kEventListWindow;
	using framefixes::kEventListWindowRva;
	using framefixes::kEventListCastSiteRvas;
	constexpr UInt8 kMovEaxCast[] = {0xB8, 0xE6, 0x32, 0x98, 0x00};
	bool allOnTheMov = true;
	for (UInt32 rva : kEventListCastSiteRvas) {
		const UInt32 at = rva - kEventListWindowRva;
		allOnTheMov = allOnTheMov && at + sizeof(kMovEaxCast) <= sizeof(kEventListWindow) &&
		              std::memcmp(kEventListWindow + at, kMovEaxCast, sizeof(kMovEaxCast)) == 0;
	}
	Check(allOnTheMov, "each of the three sites is a `mov eax, 009832E6` inside the checked window");
	UInt32 movs = 0;
	for (UInt32 i = 0; i + sizeof(kMovEaxCast) <= sizeof(kEventListWindow); ++i) {
		if (std::memcmp(kEventListWindow + i, kMovEaxCast, sizeof(kMovEaxCast)) == 0) {
			++movs;
		}
	}
	Check(movs == 3, "and the window holds no other");
	Check(kEventListWindowRva + sizeof(kEventListWindow) <= 0x9145C,
	      "the window ends where the variable lookup's checked window begins");
}

}  // namespace

int main() {
	TestCacheable();
	TestDeltas();
	TestFindAndStore();
	TestFull();
	TestSites();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
