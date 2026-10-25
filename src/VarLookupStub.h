#pragma once

#include "Types.h"

namespace framefixes {

// The patch at xOBSE's inlined variable walk (VarLookupLogic.h), as bytes.
//
// In xOBSE 22.13's obse_1_2_416.dll (image base 0x10000000, relocatable),
// ScriptToken::ReadFrom reads the token's 16-bit index into [ebp+8] and the
// event list into eax, and if the list is set walks its variables at RVA
// 0x91479..0x91498, leaving the variable (or zero) in edx for the store at
// 0x91499. After it, eax and ecx are read again before use on both paths,
// so the walk's 32 bytes can become a jump to a stub that calls the cache
// and comes back with edx set, eax, ecx and the flags clobbered as the walk
// clobbered them.
//
// The window checked before patching runs from the token read at 0x9145C to
// the store and test after the walk - nothing in it is relocated.
inline constexpr UInt32 kVarLookupWindowRva = 0x9145C;
inline constexpr UInt32 kVarLookupSiteRva = 0x91479;
inline constexpr UInt32 kVarLookupSiteLength = 32;
inline constexpr UInt32 kVarLookupReturnRva = 0x91499;
inline constexpr UInt8 kVarLookupWindow[] = {
	0x8B, 0x47, 0x18,                           // mov eax, [edi+18h]      - the token stream
	0x0F, 0xB7, 0x08,                           // movzx ecx, word [eax]   - the variable's index
	0x83, 0xC0, 0x02,                           // add eax, 2
	0x89, 0x47, 0x18,                           // mov [edi+18h], eax
	0x8B, 0x45, 0xEC,                           // mov eax, [ebp-14h]      - the event list
	0x89, 0x4D, 0x08,                           // mov [ebp+8], ecx
	0xC7, 0x43, 0x28, 0x00, 0x00, 0x00, 0x00,   // mov dword [ebx+28h], 0
	0x85, 0xC0,                                 // test eax, eax
	0x74, 0x2A,                                 // je (no list)
	// the walk, 0x91479
	0x8B, 0x48, 0x0C,                           // mov ecx, [eax+0Ch]      - the first entry
	0x85, 0xC9,                                 // test ecx, ecx
	0x74, 0x17,                                 // je -> xor edx, edx
	0x8B, 0x11,                                 // mov edx, [ecx]          - the entry's variable
	0x85, 0xD2,                                 // test edx, edx
	0x74, 0x0A,                                 // je -> next
	0x8B, 0x45, 0x08,                           // mov eax, [ebp+8]
	0x0F, 0xB7, 0xC0,                           // movzx eax, ax
	0x39, 0x02,                                 // cmp [edx], eax          - its id
	0x74, 0x09,                                 // je -> the store
	0x8B, 0x49, 0x04,                           // mov ecx, [ecx+4]        - next entry
	0x85, 0xC9,                                 // test ecx, ecx
	0x75, 0xE9,                                 // jne -> loop
	0x33, 0xD2,                                 // xor edx, edx            - not found
	// the store, 0x91499
	0x89, 0x53, 0x28,                           // mov [ebx+28h], edx
	0x85, 0xD2,                                 // test edx, edx
	0x75, 0x46,                                 // jne
};

// Writes the stub at stubAddress:
//   movzx ecx, word ptr [ebp+8]
//   push ecx                     ; the index
//   push eax                     ; the event list
//   call lookup                  ; cdecl EngineVar* (const EngineEventList*, UInt32)
//   add esp, 8
//   mov edx, eax
//   jmp returnAddress
// Returns the bytes written (21), or 0 when the capacity is too small.
UInt32 BuildVarLookupStub(UInt8* buffer, UInt32 capacity, UInt32 stubAddress, UInt32 lookup,
                          UInt32 returnAddress);

}  // namespace framefixes
