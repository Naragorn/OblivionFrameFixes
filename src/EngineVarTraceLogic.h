#pragma once

// A trace of the engine's own variable lookup - a diagnostic, nothing in the
// game changes.
//
// What the engine does (Oblivion.exe 1.2.0.416, 0x004FA110, read
// 2026-10-26): a thiscall on an event list with a variable id and the
// script (for the error message), returning the variable's value in st0,
// `ret 8`. A one-entry cache first - the last list (0x00B361BC), id
// (0x00B09E20) and variable (0x00B361C0) - which xOBSE switches off at
// 0x004FA11E, because function scripts get new lists at freed lists'
// addresses (VarLookupLogic.h). Then a walk of the list's entries from the
// head: each entry's variable compared by its whole 32-bit id; an entry with
// no next and no variable ends the walk. Not found, it prints an error and
// answers 0. The engine's command path calls it (0x004FAE1F); xOBSE's
// expression tokens do not - FastVariableLookup indexes those, not this.
//
// Why a trace: in one frame of Maskar's Oblivion Overhaul building an
// outpost (OBVR's harness, 2026-10-25), 14 of the frame's 57 render-thread
// samples sat in this walk - about 15 ms. Seen once; the outposts are random.
// Before an index is built for it, the trace says how often the engine walks,
// how far, and how much time it costs, burst by burst.
//
// The pure half; the detour is in EngineVarTrace.cpp.

#include "Types.h"
#include "VarLookupLogic.h"

namespace framefixes {

// The engine's walk, step for step, counted: the entries looked at until the
// id is found, or until the list ends. An entry without a variable in the
// middle of a list would make the engine read address 0 - the count steps
// over it instead.
inline UInt32 EngineWalkLength(const EngineEventList& list, UInt32 id, bool* found) {
	UInt32 steps = 0;
	*found = false;
	for (const EngineVarEntry* entry = list.vars; entry != nullptr; entry = entry->next) {
		++steps;
		if (entry->var == nullptr) {
			if (entry->next == nullptr) {
				break;
			}
			continue;
		}
		if (entry->var->id == id) {
			*found = true;
			break;
		}
	}
	return steps;
}

// The lookups of one burst - the game's script work in one stretch, one
// hitch: lookups less than the tracker's gap apart.
struct LookupBurst {
	static constexpr UInt32 kListSlots = 8;

	UInt32 calls = 0;
	UInt64 firstTick = 0;    // the first lookup's start
	UInt64 lastTick = 0;     // the last one's end
	UInt64 insideTicks = 0;  // the time inside the engine's lookup, summed
	UInt64 walked = 0;       // entries looked at, summed
	UInt32 longestWalk = 0;
	UInt32 notFound = 0;
	UInt32 lists = 0;        // the lists tracked below
	UInt32 otherListCalls = 0;  // lookups of lists past the tracked ones
	UInt32 busiestList = 0;
	UInt32 busiestCalls = 0;
	UInt32 busiestLongestWalk = 0;
	UInt32 listKey[kListSlots] = {};
	UInt32 listCalls[kListSlots] = {};
	UInt32 listLongestWalk[kListSlots] = {};
};

class BurstTracker {
public:
	explicit BurstTracker(UInt64 gapTicks) : m_gap(gapTicks) {}

	// Records one lookup. When it starts more than the gap after the last
	// one ended, the burst before it is finished: it is written to *finished
	// and true is returned, and this lookup opens the next one.
	bool Record(UInt64 startTick, UInt64 endTick, UInt32 walked, bool found, UInt32 list, LookupBurst* finished) {
		bool closed = false;
		if (m_burst.calls != 0 && startTick > m_burst.lastTick + m_gap) {
			Finish(&m_burst);
			*finished = m_burst;
			m_burst = LookupBurst{};
			closed = true;
		}
		LookupBurst& b = m_burst;
		if (b.calls == 0) {
			b.firstTick = startTick;
		}
		++b.calls;
		b.lastTick = endTick;
		b.insideTicks += endTick > startTick ? endTick - startTick : 0;
		b.walked += walked;
		if (walked > b.longestWalk) b.longestWalk = walked;
		if (!found) ++b.notFound;
		UInt32 slot = 0;
		while (slot < b.lists && b.listKey[slot] != list) ++slot;
		if (slot == b.lists && b.lists < LookupBurst::kListSlots) {
			b.listKey[slot] = list;
			++b.lists;
		}
		if (slot < b.lists) {
			++b.listCalls[slot];
			if (walked > b.listLongestWalk[slot]) b.listLongestWalk[slot] = walked;
		} else {
			++b.otherListCalls;
		}
		return closed;
	}

	// The burst still open, with its busiest list worked out.
	LookupBurst Current() const {
		LookupBurst b = m_burst;
		Finish(&b);
		return b;
	}

private:
	static void Finish(LookupBurst* b) {
		b->busiestList = 0;
		b->busiestCalls = 0;
		b->busiestLongestWalk = 0;
		for (UInt32 i = 0; i < b->lists; ++i) {
			if (b->listCalls[i] > b->busiestCalls) {
				b->busiestCalls = b->listCalls[i];
				b->busiestList = b->listKey[i];
				b->busiestLongestWalk = b->listLongestWalk[i];
			}
		}
	}

	UInt64 m_gap;
	LookupBurst m_burst;
};

// A burst gets a line in the log when it held a thousand lookups or a
// millisecond inside them - the ones that can show in a frame.
inline bool BurstWorthReporting(const LookupBurst& b, UInt64 ticksPerMs) {
	if (ticksPerMs == 0) {
		return false;
	}
	return b.calls >= 1000 || b.insideTicks >= ticksPerMs;
}

}  // namespace framefixes
