// Checks the bytes of the variable lookup's stub (VarLookupStub.h) and of the
// patch at xOBSE's walk: every byte, the call and the jumps landing where
// they must, and the refusals when a buffer is too small.

#include "EntryDetour.h"
#include "VarLookupStub.h"

#include <cstdio>
#include <cstring>

using namespace framefixes;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++g_failures;
	}
}

UInt32 Rel32At(const UInt8* bytes, UInt32 at) {
	UInt32 value = 0;
	std::memcpy(&value, bytes + at, 4);
	return value;
}

void TestStub() {
	std::printf("The stub\n");
	const UInt32 stub = 0x20000000;
	const UInt32 lookup = 0x30001234;
	const UInt32 back = 0x10091499;   // xOBSE 22.13: mov [ebx+28h], edx
	UInt8 code[64];
	std::memset(code, 0xCC, sizeof(code));
	const UInt32 size = BuildVarLookupStub(code, sizeof(code), stub, lookup, back);
	const UInt8 expected[] = {
		0x0F, 0xB7, 0x4D, 0x08,         // movzx ecx, word ptr [ebp+8] - the token's index
		0x51,                           // push ecx
		0x50,                           // push eax - the event list
		0xE8, 0, 0, 0, 0,               // call lookup
		0x83, 0xC4, 0x08,               // add esp, 8
		0x8B, 0xD0,                     // mov edx, eax - the variable, where the walk leaves it
		0xE9, 0, 0, 0, 0,               // jmp back
	};
	Check(size == sizeof(expected), "21 bytes");
	bool same = true;
	for (UInt32 i = 0; i < sizeof(expected); ++i) {
		if (i >= 7 && i < 11) continue;
		if (i >= 17 && i < 21) continue;
		same = same && code[i] == expected[i];
	}
	Check(same, "the instructions");
	Check(stub + 11 + Rel32At(code, 7) == lookup, "the call lands on the lookup");
	Check(stub + 21 + Rel32At(code, 17) == back, "the jump lands after the walk");
	Check(code[21] == 0xCC, "nothing written past the stub");
	Check(BuildVarLookupStub(code, 20, stub, lookup, back) == 0, "a buffer one byte short: refused");
}

void TestSitePatch() {
	std::printf("The patch at the walk\n");
	const UInt32 site = 0x10091479;   // xOBSE 22.13: mov ecx, [eax+0Ch]
	const UInt32 stub = 0x20000000;
	UInt8 patch[kVarLookupSiteLength];
	const UInt32 size = BuildEntryPatch(patch, sizeof(patch), site, stub, kVarLookupSiteLength);
	Check(size == kVarLookupSiteLength && kVarLookupSiteLength == 32, "the walk's 32 bytes replaced");
	Check(patch[0] == 0xE9 && site + 5 + Rel32At(patch, 1) == stub, "a jump to the stub");
	bool nops = true;
	for (UInt32 i = 5; i < size; ++i) nops = nops && patch[i] == 0x90;
	Check(nops, "the rest nops, no torn instruction left");
	Check(kVarLookupSiteRva + kVarLookupSiteLength == kVarLookupReturnRva, "the stub returns right after the walk");
}

void TestExpectedBytes() {
	std::printf("The bytes the patch expects\n");
	Check(sizeof(kVarLookupWindow) == kVarLookupReturnRva + 7 - kVarLookupWindowRva,
	      "the window runs from the token read to the store after the walk");
	Check(kVarLookupWindowRva < kVarLookupSiteRva &&
	          std::memcmp(kVarLookupWindow + (kVarLookupSiteRva - kVarLookupWindowRva),
	                      "\x8B\x48\x0C\x85\xC9\x74\x17\x8B\x11", 9) == 0,
	      "the site begins with mov ecx,[eax+0Ch] / test ecx,ecx / je / mov edx,[ecx]");
	Check(std::memcmp(kVarLookupWindow + (kVarLookupReturnRva - kVarLookupWindowRva), "\x89\x53\x28\x85\xD2", 5) == 0,
	      "the return point is mov [ebx+28h],edx / test edx,edx");
}

}  // namespace

int main() {
	TestStub();
	TestSitePatch();
	TestExpectedBytes();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
