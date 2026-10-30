#include "StreamCopy.h"

#include <cstring>
#include <new>

#include "EntryDetour.h"
#include "Gains.h"
#include "GainsLogic.h"
#include "Memory.h"
#include "StreamCopyLogic.h"
#include "Types.h"

namespace framefixes {
namespace {

// The indexed stream copy of the NiDX9 renderer: cdecl(const desc*), the
// counted total in eax. Its first five bytes in 1.2.416 - sub esp,0Ch;
// push ebx; push ebp - are whole instructions with nothing relative in them.
constexpr UInt32 kStreamCopy = 0x0076E610;
constexpr UInt8 kOriginalBytes[] = {0x83, 0xEC, 0x0C, 0x53, 0x55};

using CopyFn = UInt32(__cdecl*)(const StreamCopyDesc* desc);
CopyFn g_original = nullptr;
LogFn g_log = nullptr;

// The check mode's scratch: the engine's result is copied out of the
// destination before the fast copy overwrites it, then compared.
constexpr UInt32 kCheckCapacity = 1u << 20;
UInt8* g_checkScratch = nullptr;
UInt32 g_checked = 0;
UInt32 g_mismatches = 0;
UInt32 g_tooLarge = 0;
UInt32 g_mismatchLinesLeft = 8;
UInt32 g_nextReport = 2000;

UInt32 __cdecl StreamCopyChecked(const StreamCopyDesc* desc) {
	const UInt32 span = StreamCopyDestSpan(*desc);
	if (g_checkScratch == nullptr || span > kCheckCapacity) {
		++g_tooLarge;
		return g_original(desc);
	}
	const UInt32 engineTotal = g_original(desc);
	std::memcpy(g_checkScratch, desc->dest, span);
	const UInt32 fastTotal = StreamCopyFast(*desc);
	++g_checked;
	if (fastTotal != engineTotal || std::memcmp(g_checkScratch, desc->dest, span) != 0) {
		++g_mismatches;
		if (g_mismatchLinesLeft != 0) {
			--g_mismatchLinesLeft;
			UInt32 firstDiff = span;
			for (UInt32 i = 0; i < span; ++i) {
				if (g_checkScratch[i] != desc->dest[i]) {
					firstDiff = i;
					break;
				}
			}
			g_log("Stream copy: MISMATCH - count %u, element %u bytes, indexed %d, source %s, source words %u, "
			      "strides %u/%u, counted %u; totals %u/%u, first differing byte %u of %u",
			      desc->count, desc->elementBytes, desc->indices != nullptr ? 1 : 0,
			      desc->source != nullptr ? "set" : "null", desc->sourceWords, desc->sourceStride, desc->destStride,
			      desc->countedBytes, engineTotal, fastTotal, firstDiff, span);
		}
	}
	if (g_checked >= g_nextReport) {
		g_nextReport += 20000;
		g_log("Stream copy: check - %u copies compared, %u mismatches, %u too large to compare", g_checked,
		      g_mismatches, g_tooLarge);
	}
	return fastTotal;
}

GainSampler g_gains;
ReportClock g_gainsClock;
long long g_gainsStart = 0;
UInt64 g_copies = 0;

void WriteGains(long long now, long long frequency) {
	char line[400];
	if (FormatGain(line, sizeof(line), "Stream copy", "vertex copies", g_gains, g_copies, frequency,
	               static_cast<double>(now - g_gainsStart) / frequency / 60.0)) {
		g_log("%s", line);
	}
}

// One copy in 1024 is also done the engine's way first, both timed (the same
// bytes land twice; src/GainsLogic.h). The engine's copies run on the render
// thread alone.
UInt32 __cdecl StreamCopyFastEntry(const StreamCopyDesc* desc) {
	if (!ReportGains()) return StreamCopyFast(*desc);
	++g_copies;
	if (!g_gains.Due()) return StreamCopyFast(*desc);
	const long long t0 = GainsCounter();
	g_original(desc);
	const long long t1 = GainsCounter();
	const UInt32 total = StreamCopyFast(*desc);
	const long long now = GainsCounter();
	g_gains.Add(now - t1, t1 - t0);
	if (g_gainsStart == 0) g_gainsStart = now;
	const long long frequency = GainsFrequency();
	if (g_gainsClock.Due(now, frequency, GainsPeriod())) WriteGains(now, frequency);
	return total;
}

}  // namespace

// The lines are read without the copies' thread stopped: should a copy run
// on another thread meanwhile, one line may be a sample off - no more.
void WriteStreamCopyGainsNow() {
	if (g_log == nullptr || g_gainsStart == 0) return;
	const long long now = GainsCounter();
	const long long frequency = GainsFrequency();
	WriteGains(now, frequency);
	g_gainsClock.Restart(now, frequency, GainsPeriod());
}

bool InstallStreamCopy(bool check, LogFn log) {
	static bool s_done = false;
	if (s_done) return g_original != nullptr;
	s_done = true;
	g_log = log;
	if (!Verify(kStreamCopy, kOriginalBytes, sizeof(kOriginalBytes))) {
		char bytes[32];
		DescribeBytes(kStreamCopy, bytes, sizeof(bytes));
		log("Stream copy: the routine's entry at %08X reads %s, not the game's own bytes - another plugin's patch or "
		    "another game version; the engine's copy stays",
		    kStreamCopy, bytes);
		return false;
	}
	constexpr UInt32 capacity = 24;
	auto* trampoline = static_cast<UInt8*>(AllocExecutable(capacity));
	if (trampoline == nullptr) {
		log("Stream copy: no executable memory; the engine's copy stays");
		return false;
	}
	if (BuildEntryTrampoline(trampoline, capacity, reinterpret_cast<UInt32>(trampoline), kStreamCopy, kOriginalBytes,
	                         sizeof(kOriginalBytes)) == 0) {
		log("Stream copy: trampoline did not fit; the engine's copy stays");
		return false;
	}
	if (check) {
		g_checkScratch = new (std::nothrow) UInt8[kCheckCapacity];
		if (g_checkScratch == nullptr) {
			log("Stream copy: no memory for the check scratch; the engine's copy stays");
			return false;
		}
	}
	const void* replacement = check ? reinterpret_cast<const void*>(&StreamCopyChecked)
	                                : reinterpret_cast<const void*>(&StreamCopyFastEntry);
	UInt8 patch[sizeof(kOriginalBytes)];
	if (BuildEntryPatch(patch, sizeof(patch), kStreamCopy, reinterpret_cast<UInt32>(replacement),
	                    sizeof(kOriginalBytes)) == 0) {
		log("Stream copy: patch did not fit; the engine's copy stays");
		return false;
	}
	g_original = reinterpret_cast<CopyFn>(trampoline);
	if (!SafeWrite(kStreamCopy, patch, sizeof(patch))) {
		g_original = nullptr;
		log("Stream copy: could not write the patch; the engine's copy stays");
		return false;
	}
	log("Stream copy: fast - the indexed stream copy at %08X decides the element size once instead of a memcpy "
	    "call per vertex%s",
	    kStreamCopy, check ? "; check mode compares every copy with the engine's" : "");
	return true;
}

}  // namespace framefixes
