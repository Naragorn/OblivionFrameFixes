#pragma once

#include "Types.h"

namespace framefixes {

// Where xOBSE 22.13 casts for the event list (CastCacheLogic.h), as bytes.
//
// In obse_1_2_416.dll (image base 0x10000000, relocatable), ScriptToken::
// ReadFrom holds EventListFromForm inlined at RVA 0x913C9..0x9145B, right
// before the variable walk that FastVariableLookup patches (its window
// starts at 0x9145C). Three times it calls the game's cast through
// `mov eax, 009832E6` / `call eax`: the form to TESObjectREFR, the
// reference's ExtraScript from BSExtraData, the form to TESQuest. The patch
// writes the cache's address over each `mov`'s operand, so the call goes
// there with the same arguments and the same cdecl cleanup. The window
// checked before - nothing in it is relocated, the `call` in it is relative
// - is all of the inlined function.
inline constexpr UInt32 kEventListWindowRva = 0x913C9;
inline constexpr UInt32 kEventListCastSiteRvas[3] = {0x913DA, 0x91406, 0x91448};
inline constexpr UInt8 kEventListWindow[] = {
	0x33, 0xC0,                                  // xor eax, eax
	0x50,                                        // push eax                 - isReference 0
	0x68, 0x54, 0x30, 0xB0, 0x00,                // push 00B03054            - TESObjectREFR
	0x68, 0x9C, 0x2F, 0xB0, 0x00,                // push 00B02F9C            - TESForm
	0x50,                                        // push eax                 - no vtable displacement
	0x89, 0x45, 0xEC,                            // mov [ebp-14h], eax       - no event list yet
	0xB8, 0xE6, 0x32, 0x98, 0x00,                // mov eax, 009832E6        - site 1, 0x913DA
	0x51,                                        // push ecx                 - the form
	0xFF, 0xD0,                                  // call eax
	0x83, 0xC4, 0x14,                            // add esp, 14h
	0x85, 0xC0,                                  // test eax, eax
	0x74, 0x4E,                                  // je -> the quest
	0x6A, 0x12,                                  // push 12h                 - kExtraData_Script
	0x8D, 0x48, 0x44,                            // lea ecx, [eax+44h]       - the reference's extra list
	0xE8, 0x9D, 0x5E, 0xF7, 0xFF,                // call 10007290            - GetByType
	0x85, 0xC0,                                  // test eax, eax
	0x74, 0x37,                                  // je -> none
	0x6A, 0x00,                                  // push 0
	0x68, 0xB8, 0x3B, 0xB0, 0x00,                // push 00B03BB8            - ExtraScript
	0x68, 0x50, 0x38, 0xB0, 0x00,                // push 00B03850            - BSExtraData
	0x6A, 0x00,                                  // push 0
	0x50,                                        // push eax                 - the extra data
	0xB8, 0xE6, 0x32, 0x98, 0x00,                // mov eax, 009832E6        - site 2, 0x91406
	0xFF, 0xD0,                                  // call eax
	0x83, 0xC4, 0x14,                            // add esp, 14h
	0x85, 0xC0,                                  // test eax, eax
	0x74, 0x1A,                                  // je -> none
	0x8B, 0x40, 0x10,                            // mov eax, [eax+10h]       - ExtraScript's event list
	0xEB, 0x40,                                  // jmp -> keep it
	0xFF, 0x71, 0x08,                            // push [ecx+8]             - (the reference list's form id)
	0xB8, 0x50, 0xB2, 0x46, 0x00,                // mov eax, 0046B250        - LookupFormByID
	0xFF, 0xD0,                                  // call eax
	0x8B, 0x4D, 0x08,                            // mov ecx, [ebp+8]
	0x83, 0xC4, 0x04,                            // add esp, 4
	0x89, 0x41, 0x08,                            // mov [ecx+8], eax
	0xEB, 0x8D,                                  // jmp back
	0xC7, 0x45, 0xEC, 0x00, 0x00, 0x00, 0x00,    // mov [ebp-14h], 0         - none
	0xEB, 0x25,                                  // jmp -> the variable
	0x6A, 0x00,                                  // push 0
	0x68, 0x68, 0x54, 0xB0, 0x00,                // push 00B05468            - TESQuest
	0x68, 0x9C, 0x2F, 0xB0, 0x00,                // push 00B02F9C            - TESForm
	0x6A, 0x00,                                  // push 0
	0xFF, 0x75, 0x08,                            // push [ebp+8]             - the form
	0xB8, 0xE6, 0x32, 0x98, 0x00,                // mov eax, 009832E6        - site 3, 0x91448
	0xFF, 0xD0,                                  // call eax
	0x83, 0xC4, 0x14,                            // add esp, 14h
	0x85, 0xC0,                                  // test eax, eax
	0x74, 0x06,                                  // je -> the variable
	0x8B, 0x40, 0x58,                            // mov eax, [eax+58h]       - the quest's event list
	0x89, 0x45, 0xEC,                            // mov [ebp-14h], eax       - keep it
};
static_assert(sizeof(kEventListWindow) == 0x9145C - 0x913C9, "the window runs to the variable walk's window");

}  // namespace framefixes
