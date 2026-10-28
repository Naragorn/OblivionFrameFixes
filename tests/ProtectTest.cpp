// Checks the code protection's decisions (src/ProtectLogic.h): which calls
// to VirtualProtect go to the system, which are answered from what is known,
// what the caller is told, and when a page is forgotten.

#include <cstdio>

#include "ProtectLogic.h"

using namespace framefixes;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++g_failures;
	}
}

constexpr UInt32 kRwx = 0x40;
constexpr UInt32 kRx = 0x20;

void TestRange() {
	std::printf("Which addresses are the game's code\n");
	Check(InGameCode(0x00585C90, 4), "xOBSE's byte at 0x00585C90: the game's code");
	Check(!InGameCode(0x10000000, 4), "an address in a DLL: not");
	Check(!InGameCode(0x00400000, 4), "the image's header page: not");
	Check(!InGameCode(0x00FFFFFE, 4), "running past the image's end: not");
	Check(!InGameCode(0x00585C90, 0), "nothing asked for: not");
	Check(!InGameCode(0xFFFFFFFE, 4), "a range that wraps: not");
}

void TestRoutes() {
	std::printf("Which calls go to the system\n");
	ProtectCache c;
	Check(Route(c, 0x00585C90, 4, kRwx) == ProtectRoute::RealThenHold, "the first ask to write a page: the system, then held");
	c.Hold(0x00585C90, 4);
	Check(c.Holds(0x00585C90, 4), "held");
	Check(Route(c, 0x00585C92, 4, kRwx) == ProtectRoute::Fast, "the same page again: answered from what is held");
	Check(Route(c, 0x00585FFE, 4, kRwx) == ProtectRoute::RealThenHold, "a range into the next page: the system");
	Check(Route(c, 0x00585C90, 4, kRx) == ProtectRoute::RealThenForget,
	      "another protection asked for a held page: the system, and the page forgotten");
	Check(Route(c, 0x10001000, 4, kRwx) == ProtectRoute::Real, "outside the game's code: always the system");
	Check(Route(c, 0x00585C90, 4, 0x04) == ProtectRoute::RealThenForget, "read-write without execute: the system");
	c.Forget(0x00585C90, 4);
	Check(!c.Holds(0x00585C90, 4) && Route(c, 0x00585C90, 4, kRwx) == ProtectRoute::RealThenHold, "forgotten: asked again");
	Check(ReportedOld() == kRwx, "the caller is told the page was writable, so its restore asks for that again");
}

void TestFull() {
	std::printf("A full cache\n");
	ProtectCache c;
	for (UInt32 i = 0; i < ProtectCache::kPages + 4; ++i) {
		c.Hold(0x00401000 + i * 0x1000, 4);
	}
	Check(c.count == ProtectCache::kPages, "it holds at most kPages pages");
	Check(c.Holds(0x00401000, 4), "the first ones stay");
	Check(!c.Holds(0x00401000 + (ProtectCache::kPages + 1) * 0x1000, 4), "past the room: not held, so the system is asked");
	c.Hold(0x00585C90, 0x2000);
	Check(c.count == ProtectCache::kPages, "a two-page range does not fit either");
}

void TestWritable() {
	std::printf("What counts as writable when asked again\n");
	Check(StillWritable(0x40), "execute-read-write");
	Check(StillWritable(0x80), "execute-write-copy: an image page not yet written (the check run's 8 'mismatches')");
	Check(!StillWritable(0x20) && !StillWritable(0x02) && !StillWritable(0x04), "execute-read, read-only, read-write: no");
	Check(!StillWritable(0x40 | 0x100), "a guard page: no");
}

void TestFault() {
	std::printf("A write fault on a held page\n");
	ProtectCache c;
	c.Hold(0x00585C90, 4);
	Check(FaultIsOurs(c, true, 0x00585C91), "a write into a held page: ours to mend");
	Check(!FaultIsOurs(c, false, 0x00585C91), "a read: not ours");
	Check(!FaultIsOurs(c, true, 0x00601000), "a write elsewhere: not ours");
}

}  // namespace

int main() {
	TestRange();
	TestRoutes();
	TestFull();
	TestWritable();
	TestFault();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
