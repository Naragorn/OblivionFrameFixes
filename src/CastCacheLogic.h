#pragma once

// The engine's RTTI cast answered from a cache, for xOBSE's event list
// lookup.
//
// What xOBSE does (llde/xOBSE obse/obse/GameAPI.cpp, EventListFromForm,
// inlined into ScriptToken::ReadFrom in xOBSE 22.13): every expression token
// that names a variable of another script ("SomeQuest.someVariable") finds
// that script's event list through the form it names - cast to TESObjectREFR,
// and if that is a reference, its ExtraScript cast from BSExtraData; if not,
// the form cast to TESQuest. Each cast is a call of the game's
// __RTDynamicCast (0x009832E6, the MSVC runtime's), which reads the object's
// RTTI and walks its class hierarchy comparing type descriptors - for a
// quest, the cast to TESObjectREFR fails only after the whole walk. A mod
// that builds its content in one frame from thousands of such tokens pays
// that for every one (Maskar's Oblivion Overhaul's map build on leaving a
// house, OBVR's harness 2026-10-25: about a tenth of the frame's samples in
// the cast once the variable walk was gone).
//
// Why the answer can be cached: what the cast returns for an object depends
// on nothing but the object's class, the source type and the target type -
// the class decides where the requested base sits in the object, or that
// there is none. The class is identified by the object's vtable pointer:
// MSVC puts the class's complete object locator right before its vtable, so
// two classes never share one (a class mid-construction has its base's
// vtable, and casts as that base - so does the engine's cast). The answer
// is kept as an offset from the object, or as a failure, under the key
// (vtable, source type, target type). Only pointer casts with no vtable
// displacement are answered from the cache - the three xOBSE sites make
// exactly those; a reference cast (which throws on failure) or a null object
// goes to the engine.
//
// The pure half; the patch is in CastCache.cpp.

#include "Types.h"

namespace framefixes {

struct CastKey {
	UInt32 vtable;      // the object's first word
	UInt32 sourceType;  // the RTTI type descriptor the object is cast from
	UInt32 targetType;  // and the one it is cast to
};

// The cast's answer for every object of a class: the offset from the object
// to the answer, or that there is none (null).
struct CastAnswer {
	SInt32 delta;
	bool fails;
};

// Whether a call may be answered from the cache (the header above).
inline bool CastIsCacheable(UInt32 object, SInt32 vtableDisplacement, SInt32 isReference) {
	return object != 0 && vtableDisplacement == 0 && isReference == 0;
}

inline CastAnswer DeltaOfCast(UInt32 object, UInt32 answer) {
	if (answer == 0) {
		return CastAnswer{0, true};
	}
	return CastAnswer{static_cast<SInt32>(answer - object), false};
}

inline UInt32 AnswerOfCast(UInt32 object, const CastAnswer& answer) {
	return answer.fails ? 0 : object + static_cast<UInt32>(answer.delta);
}

// Open addressing on the key's hash, never more than three quarters full.
// Past that a new key is refused, and the engine keeps answering it: the
// classes a game's scripts name are a few dozen. A vtable of zero marks a
// free slot and is never stored.
class CastCache {
public:
	static constexpr UInt32 kSlots = 1024;
	static constexpr UInt32 kMaxHeld = kSlots * 3 / 4;

	bool Find(const CastKey& key, CastAnswer* answer) const {
		if (key.vtable == 0) {
			return false;
		}
		for (UInt32 i = Hash(key), n = 0; n < kSlots; i = (i + 1) & (kSlots - 1), ++n) {
			const Slot& s = m_slots[i];
			if (s.key.vtable == 0) {
				return false;
			}
			if (Same(s.key, key)) {
				*answer = s.answer;
				return true;
			}
		}
		return false;
	}

	bool Store(const CastKey& key, const CastAnswer& answer) {
		if (key.vtable == 0) {
			return false;
		}
		for (UInt32 i = Hash(key), n = 0; n < kSlots; i = (i + 1) & (kSlots - 1), ++n) {
			Slot& s = m_slots[i];
			if (Same(s.key, key)) {
				s.answer = answer;
				return true;
			}
			if (s.key.vtable == 0) {
				if (m_held >= kMaxHeld) {
					return false;
				}
				s.key = key;
				s.answer = answer;
				++m_held;
				return true;
			}
		}
		return false;
	}

	UInt32 Held() const { return m_held; }

	void Clear() {
		for (Slot& s : m_slots) {
			s = Slot{};
		}
		m_held = 0;
	}

private:
	struct Slot {
		CastKey key{0, 0, 0};
		CastAnswer answer{0, true};
	};

	static bool Same(const CastKey& a, const CastKey& b) {
		return a.vtable == b.vtable && a.sourceType == b.sourceType && a.targetType == b.targetType;
	}

	// The three words mixed (multiply-xorshift), so vtables four bytes apart
	// land far apart.
	static UInt32 Hash(const CastKey& key) {
		UInt32 h = key.vtable * 0x9E3779B1u;
		h ^= key.sourceType * 0x85EBCA77u;
		h ^= key.targetType * 0xC2B2AE3Du;
		h ^= h >> 16;
		h *= 0x7FEB352Du;
		h ^= h >> 15;
		return h & (kSlots - 1);
	}

	Slot m_slots[kSlots];
	UInt32 m_held = 0;
};

}  // namespace framefixes
