// The engine variable lookup trace's decisions (EngineVarTraceLogic.h): the
// engine's walk counted as the engine walks, lookups grouped into bursts by
// the gap between them, what a burst sums up, which list it asked most, and
// which bursts are worth a line in the log.

#include <cstdio>

#include "EngineVarTraceLogic.h"

using namespace framefixes;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++g_failures;
	}
}

constexpr UInt64 kTicksPerMs = 10000;  // a 10 MHz counter

void TestEngineWalk() {
	std::printf("The engine's walk, counted\n");
	EngineVar a{7, nullptr, 1.0};
	EngineVar b{9, nullptr, 2.0};
	EngineVar c{70000, nullptr, 3.0};
	EngineVarEntry e3{&c, nullptr};
	EngineVarEntry e2{&b, &e3};
	EngineVarEntry e1{&a, &e2};
	EngineEventList list{};
	list.vars = &e1;
	bool found = false;
	Check(EngineWalkLength(list, 7, &found) == 1 && found, "the first entry: one step");
	Check(EngineWalkLength(list, 9, &found) == 2 && found, "the second: two");
	Check(EngineWalkLength(list, 70000, &found) == 3 && found, "the whole 32-bit id is compared (the engine's, not xOBSE's 16 bits)");
	Check(EngineWalkLength(list, 70000 & 0xFFFF, &found) == 3 && !found, "so the id's low half alone is not found");
	Check(EngineWalkLength(list, 5, &found) == 3 && !found, "an absent id walks the whole list");
	EngineVarEntry tail{nullptr, nullptr};
	e3.next = &tail;
	Check(EngineWalkLength(list, 5, &found) == 4 && !found, "an empty last entry ends the walk, as in the engine");
	EngineEventList empty{};
	Check(EngineWalkLength(empty, 7, &found) == 0 && !found, "no entries: no steps");
}

void TestBursts() {
	std::printf("Bursts\n");
	BurstTracker tracker(5 * kTicksPerMs);
	LookupBurst done{};
	const UInt32 big = 0x10000000;
	const UInt32 small = 0x20000000;
	Check(!tracker.Record(1000, 1010, 3000, true, big, &done), "the first lookup opens a burst");
	Check(!tracker.Record(2000, 2050, 3500, true, big, &done), "one a millisecond later joins it");
	Check(!tracker.Record(3000, 3005, 2, false, small, &done), "so does a lookup of another list");
	// 10 ms after the last one ended: a new burst, the old one comes back.
	Check(tracker.Record(3005 + 10 * kTicksPerMs, 3005 + 10 * kTicksPerMs + 20, 1, true, small, &done),
	      "a lookup after the gap closes the burst");
	Check(done.calls == 3, "three lookups");
	Check(done.firstTick == 1000 && done.lastTick == 3005, "from the first one's start to the last one's end");
	Check(done.insideTicks == 10 + 50 + 5, "the time inside the engine's lookup summed");
	Check(done.walked == 3000 + 3500 + 2 && done.longestWalk == 3500, "the steps walked and the longest walk");
	Check(done.notFound == 1, "one id was not found");
	Check(done.busiestList == big && done.busiestCalls == 2 && done.busiestLongestWalk == 3500,
	      "the list asked most, how often, and its longest walk");
	Check(done.lists == 2, "two lists asked");
	LookupBurst open = tracker.Current();
	Check(open.calls == 1 && open.busiestList == small, "the new burst holds the lookup that opened it");
}

void TestManyLists() {
	std::printf("More lists than the burst tracks\n");
	BurstTracker tracker(5 * kTicksPerMs);
	LookupBurst done{};
	UInt64 t = 100;
	for (UInt32 i = 0; i < LookupBurst::kListSlots + 4; ++i) {
		tracker.Record(t, t + 1, 10, true, 0x30000000 + i * 0x100, &done);
		t += 10;
	}
	for (UInt32 i = 0; i < 5; ++i) {
		tracker.Record(t, t + 1, 900, true, 0x30000000, &done);
		t += 10;
	}
	tracker.Record(t + 10 * kTicksPerMs, t + 10 * kTicksPerMs + 1, 1, true, 0x1, &done);
	Check(done.calls == LookupBurst::kListSlots + 4 + 5, "every lookup counted");
	Check(done.lists == LookupBurst::kListSlots && done.otherListCalls == 4,
	      "the lists past the tracked ones are counted together");
	Check(done.busiestList == 0x30000000 && done.busiestCalls == 6 && done.busiestLongestWalk == 900,
	      "the busiest of the tracked lists is found");
}

void TestWorth() {
	std::printf("Which bursts are reported\n");
	LookupBurst b{};
	b.calls = 999;
	b.insideTicks = kTicksPerMs - 1;
	Check(!BurstWorthReporting(b, kTicksPerMs), "under a thousand lookups and under a millisecond inside: no line");
	b.calls = 1000;
	Check(BurstWorthReporting(b, kTicksPerMs), "a thousand lookups: a line");
	b.calls = 10;
	b.insideTicks = kTicksPerMs;
	Check(BurstWorthReporting(b, kTicksPerMs), "a millisecond inside: a line");
	Check(!BurstWorthReporting(b, 0), "no counter frequency: no line");
}

}  // namespace

int main() {
	TestEngineWalk();
	TestBursts();
	TestManyLists();
	TestWorth();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
