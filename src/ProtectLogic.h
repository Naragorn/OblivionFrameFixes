#pragma once

// xOBSE 22.13 changes a few bytes of the game's code at 0x00585C90 around
// every command that runs without its message, and lifts the page's
// protection with VirtualProtect for each byte, then puts it back: four to
// twelve system calls a command. With Maskar's Oblivion Overhaul the first
// world frame after a load ran 32,006 scripts in 2.3 s, and about half of
// the sampler's samples in that frame were in those calls (OBVR
// perf-load-scripts.txt, 2026-10-28, Linux; Windows unmeasured).
//
// FastCodeProtect answers them from what is known: xOBSE's import of
// VirtualProtect points here; the first ask to make a page of the game's
// code writable goes to the system, the page is held writable from then on,
// and the caller is told it was writable already - so its "put it back"
// asks for writable again and is answered without a call. Any other ask (a
// page outside the game's code, another protection) goes to the system and
// a held page is forgotten. Should anything make a held page read-only
// again behind this, the write that faults is caught (FaultIsOurs), the page
// made writable and the write done. The pure half; FastProtect.cpp patches.

#include "Types.h"

namespace framefixes {

inline constexpr UInt32 kPageExecuteReadWrite = 0x40;
inline constexpr UInt32 kGameCodeStart = 0x00401000;
inline constexpr UInt32 kGameCodeEnd = 0x01000000;

inline bool InGameCode(UInt32 address, UInt32 size) {
	if (size == 0) return false;
	const UInt32 end = address + size;
	return end > address && address >= kGameCodeStart && end <= kGameCodeEnd;
}

struct ProtectCache {
	static constexpr UInt32 kPages = 32;
	UInt32 page[kPages] = {};
	UInt32 count = 0;

	bool HoldsPage(UInt32 p) const {
		for (UInt32 i = 0; i < count; ++i) {
			if (page[i] == p) return true;
		}
		return false;
	}
	bool Holds(UInt32 address, UInt32 size) const {
		if (size == 0) return false;
		const UInt32 first = address >> 12;
		const UInt32 last = (address + size - 1) >> 12;
		for (UInt32 p = first; p <= last; ++p) {
			if (!HoldsPage(p)) return false;
		}
		return true;
	}
	// All of the range or nothing.
	void Hold(UInt32 address, UInt32 size) {
		if (size == 0) return;
		const UInt32 first = address >> 12;
		const UInt32 last = (address + size - 1) >> 12;
		UInt32 missing = 0;
		for (UInt32 p = first; p <= last; ++p) missing += HoldsPage(p) ? 0 : 1;
		if (count + missing > kPages) return;
		for (UInt32 p = first; p <= last; ++p) {
			if (!HoldsPage(p)) page[count++] = p;
		}
	}
	void Forget(UInt32 address, UInt32 size) {
		if (size == 0) return;
		const UInt32 first = address >> 12;
		const UInt32 last = (address + size - 1) >> 12;
		for (UInt32 i = 0; i < count;) {
			if (page[i] >= first && page[i] <= last) {
				page[i] = page[--count];
			} else {
				++i;
			}
		}
	}
};

enum class ProtectRoute { Real, RealThenHold, RealThenForget, Fast };

inline ProtectRoute Route(const ProtectCache& c, UInt32 address, UInt32 size, UInt32 newProtect) {
	if (!InGameCode(address, size)) return ProtectRoute::Real;
	if (newProtect != kPageExecuteReadWrite) return ProtectRoute::RealThenForget;
	return c.Holds(address, size) ? ProtectRoute::Fast : ProtectRoute::RealThenHold;
}

// What a caller is told the protection was, on the fast and the holding way.
inline constexpr UInt32 ReportedOld() { return kPageExecuteReadWrite; }

// What VirtualQuery may say of a held page: writable and executable. An
// image page made PAGE_EXECUTE_READWRITE reads PAGE_EXECUTE_WRITECOPY until
// its first write (copy on write), which is writable too.
inline bool StillWritable(UInt32 protect) { return protect == 0x40 || protect == 0x80; }

inline bool FaultIsOurs(const ProtectCache& c, bool write, UInt32 address) {
	return write && c.Holds(address, 1);
}

}  // namespace framefixes
