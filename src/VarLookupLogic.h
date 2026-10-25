#pragma once

// xOBSE's variable lookup by index instead of by list walk.
//
// What xOBSE does (llde/xOBSE obse/obse/ScriptTokens.cpp, ScriptToken::
// ReadFrom, case 'V'; obse/obse/GameAPI.cpp, ScriptEventList::GetVariable):
// every expression token that names a variable - "MOO.ini_event_density",
// "set x to y" in an OBSE expression - reads the variable's 16-bit index
// from the compiled script and looks it up in the event list that holds it
// by walking the list from its head until an entry's variable has that id.
// In xOBSE 22.13 (obse_1_2_416.dll) the walk is inlined into ReadFrom at
// RVA 0x91479..0x91498. A quest script with thousands of variables - Maskar's
// Oblivion Overhaul's MOOScript has 3532 - makes every token cost a walk of
// a thousand nodes and more; MOO's worldspace event map runs that tens of
// thousands of times in one frame (127 ms in OBVR's harness, 2026-10-25, 77 %
// of the frame's samples in that loop).
//
// What the engine does with the list (read in Oblivion.exe 1.2.0.416,
// 2026-10-25):
// - 0x004FBDC0 Script::CreateEventList allocates the 0x14-byte list and has
//   0x004FA910 build its variables from the script's.
// - 0x004FA080 frees every variable and entry of a list and clears +0x0C. It
//   is the only freeing of variables found: called by the destructor
//   0x004FB4E0 (eight callers, and xOBSE's ScriptEventList::Destructor, which
//   every user function's own list goes through at the end of its call) and
//   by the reset 0x004FBA50, which then builds the variables anew.
// - 0x004FB630 sets a variable's value by id and allocates nothing.
// - The engine's own one-entry cache of the last numeric variable
//   (0x004FA110, keyed by the list's address) is what xOBSE switches off at
//   0x004FA11E: function scripts get new lists at the addresses of freed
//   ones, and the cache handed out freed variables. A cache here must forget
//   a list when it is freed, not trust the address.
//
// So the cache below indexes a long list's variables by id - one walk, then
// an array - and forgets a list whenever 0x004FA080 frees its variables. On
// every lookup it compares the list's head with the one it indexed, so a list
// rebuilt without that call is indexed anew. Short lists are walked as
// before: their walk is short, and a user function's list lives for one call.
// The pure half; the patches are in VarLookup.cpp.

#include <new>

#include "Types.h"

namespace framefixes {

// The engine's structures as xOBSE's GameAPI.h has them.
struct EngineVarEntry;

struct EngineVar {
	UInt32 id;                   // 00
	EngineVarEntry* nextEntry;   // 04
	double data;                 // 08
};

struct EngineVarEntry {
	EngineVar* var;              // 00
	EngineVarEntry* next;        // 04
};

struct EngineEventList {
	void* script;                // 00
	UInt32 unk04;                // 04
	void* events;                // 08
	EngineVarEntry* vars;        // 0C
	void* effectInfo;            // 10
};

// xOBSE's inlined walk, exactly: the first entry whose variable is set and
// has the id; the id is the token's 16-bit index, compared with the
// variable's 32-bit id. nodes counts the entries looked at.
inline EngineVar* WalkVariables(const EngineEventList& list, UInt32 id16, UInt32* nodes) {
	UInt32 seen = 0;
	EngineVar* found = nullptr;
	for (EngineVarEntry* entry = list.vars; entry != nullptr; entry = entry->next) {
		++seen;
		if (entry->var != nullptr && entry->var->id == (id16 & 0xFFFF)) {
			found = entry->var;
			break;
		}
	}
	if (nodes != nullptr) *nodes = seen;
	return found;
}

struct VarLookupStats {
	UInt32 lookups = 0;
	UInt32 fromIndex = 0;     // answered by an index
	UInt32 walked = 0;        // answered by a walk
	UInt32 built = 0;         // indexes built
	UInt32 rebuilt = 0;       // indexes dropped because the list's head changed
	UInt32 forgotten = 0;     // indexes dropped because the engine freed the list's variables
	UInt32 evicted = 0;       // indexes dropped to make room
	UInt32 refused = 0;       // indexes not built: the budget spent, memory refused, or no id to index
};

class VarLookupCache {
public:
	// A list is worth an index when a walk passes this many entries.
	static constexpr UInt32 kLongList = 16;
	// ...and has been looked up this many times while not indexed.
	static constexpr UInt32 kLookupsBeforeIndex = 4;
	// Lists held: kSets sets of two, found by the list's address.
	static constexpr UInt32 kSets = 128;
	// The pointers all indexes may hold together: 4 MB.
	static constexpr UInt32 kDefaultBudget = 1u << 20;

	explicit VarLookupCache(UInt32 pointerBudget = kDefaultBudget) : budget_(pointerBudget) {}
	~VarLookupCache() { Clear(); }
	VarLookupCache(const VarLookupCache&) = delete;
	VarLookupCache& operator=(const VarLookupCache&) = delete;

	// The variable id16 names in list - what WalkVariables would return.
	EngineVar* Find(const EngineEventList* list, UInt32 id16) {
		++stats_.lookups;
		const UInt32 id = id16 & 0xFFFF;
		Slot* slot = Lookup(list);
		if (slot != nullptr && slot->head != list->vars) {
			if (slot->index != nullptr) ++stats_.rebuilt;
			Drop(*slot);
			slot->list = list;
			slot->head = list->vars;
		}
		if (slot != nullptr && slot->index != nullptr) {
			++stats_.fromIndex;
			Touch(slot);
			return id < slot->size ? slot->index[id] : nullptr;
		}
		UInt32 nodes = 0;
		EngineVar* found = WalkVariables(*list, id, &nodes);
		++stats_.walked;
		if (slot == nullptr && nodes >= kLongList) slot = Take(list);
		if (slot != nullptr) {
			Touch(slot);
			if (++slot->lookups >= kLookupsBeforeIndex) Build(*slot);
		}
		return found;
	}

	// The engine is about to free list's variables (0x004FA080).
	void Forget(const EngineEventList* list) {
		Slot* slot = Lookup(list);
		if (slot == nullptr) return;
		if (slot->index != nullptr) ++stats_.forgotten;
		Drop(*slot);
		*slot = Slot{};
	}

	void Clear() {
		for (Slot& slot : slots_) {
			Drop(slot);
			slot = Slot{};
		}
	}

	// The set a list's address falls into; for the tests.
	static UInt32 SetFor(const EngineEventList* list) { return SetOf(list); }

	const VarLookupStats& Stats() const { return stats_; }
	UInt32 PointersHeld() const { return held_; }
	UInt32 IndexesHeld() const {
		UInt32 count = 0;
		for (const Slot& slot : slots_) count += slot.index != nullptr ? 1 : 0;
		return count;
	}
	bool Indexed(const EngineEventList* list) const {
		const Slot* slot = Lookup(list);
		return slot != nullptr && slot->index != nullptr;
	}

private:
	struct Slot {
		const EngineEventList* list = nullptr;
		EngineVarEntry* head = nullptr;
		EngineVar** index = nullptr;
		UInt32 size = 0;
		UInt32 lookups = 0;
		UInt32 lastUse = 0;
	};

	static UInt32 SetOf(const EngineEventList* list) {
		const UInt32 address = static_cast<UInt32>(reinterpret_cast<unsigned long long>(list));
		return ((address >> 4) ^ (address >> 12)) & (kSets - 1);
	}

	const Slot* Lookup(const EngineEventList* list) const {
		const Slot* set = &slots_[SetOf(list) * 2];
		if (set[0].list == list) return &set[0];
		if (set[1].list == list) return &set[1];
		return nullptr;
	}
	Slot* Lookup(const EngineEventList* list) {
		return const_cast<Slot*>(static_cast<const VarLookupCache*>(this)->Lookup(list));
	}

	void Touch(Slot* slot) { slot->lastUse = ++clock_; }

	// A slot for list: a free way of its set, else the one used longer ago.
	Slot* Take(const EngineEventList* list) {
		Slot* set = &slots_[SetOf(list) * 2];
		Slot* slot = set[0].list == nullptr ? &set[0]
		             : set[1].list == nullptr ? &set[1]
		             : set[0].lastUse <= set[1].lastUse ? &set[0]
		                                                : &set[1];
		if (slot->list != nullptr) {
			if (slot->index != nullptr) ++stats_.evicted;
			Drop(*slot);
		}
		*slot = Slot{};
		slot->list = list;
		slot->head = list->vars;
		return slot;
	}

	void Drop(Slot& slot) {
		if (slot.index != nullptr) {
			held_ -= slot.size;
			delete[] slot.index;
		}
		slot.index = nullptr;
		slot.size = 0;
		slot.lookups = 0;
	}

	// One walk for the largest id, one to fill; the first entry of an id wins,
	// as in the walk. Ids past 0xFFFF are never asked for and left out.
	void Build(Slot& slot) {
		UInt32 largest = 0;
		bool any = false;
		for (EngineVarEntry* entry = slot.head; entry != nullptr; entry = entry->next) {
			if (entry->var == nullptr || entry->var->id > 0xFFFF) continue;
			if (!any || entry->var->id > largest) largest = entry->var->id;
			any = true;
		}
		const UInt32 size = any ? largest + 1 : 0;
		if (size == 0 || size > budget_ - held_) {
			++stats_.refused;
			slot.lookups = 0;
			return;
		}
		EngineVar** index = new (std::nothrow) EngineVar*[size];
		if (index == nullptr) {
			++stats_.refused;
			slot.lookups = 0;
			return;
		}
		for (UInt32 i = 0; i < size; ++i) index[i] = nullptr;
		for (EngineVarEntry* entry = slot.head; entry != nullptr; entry = entry->next) {
			EngineVar* var = entry->var;
			if (var == nullptr || var->id > 0xFFFF || index[var->id] != nullptr) continue;
			index[var->id] = var;
		}
		slot.index = index;
		slot.size = size;
		held_ += size;
		++stats_.built;
	}

	Slot slots_[kSets * 2] = {};
	UInt32 budget_;
	UInt32 held_ = 0;
	UInt32 clock_ = 0;
	VarLookupStats stats_;
};

}  // namespace framefixes
