#include "FrameClock.h"

#include <windows.h>

#include "EntryDetour.h"
#include "FrameClockLogic.h"
#include "Memory.h"
#include "Types.h"

namespace framefixes {
namespace {

// The time info's update: thiscall(TimeInfo* this, UInt32 tick), ret 4,
// called once a frame by the main loop at 0x0040D89F with GetTickCount's
// value. Its first nine bytes in 1.2.416 - mov edx,[esp+4]; fldz;
// sub edx,[ecx+14h] - are whole instructions with nothing relative in them.
constexpr UInt32 kTimeInfoUpdate = 0x0047D170;
constexpr UInt8 kOriginalBytes[] = {0x8B, 0x54, 0x24, 0x04, 0xD9, 0xEE, 0x2B, 0x51, 0x14};

// The global every frame's seconds are multiplied by at the update's end
// (fmul [0x00B06704] at 0x0047D247) - the time multiplier.
constexpr UInt32 kTimeMultiplier = 0x00B06704;

// __fastcall with a dummy edx mirrors __thiscall's register use; the tick
// travels the stack in both and both clean it up.
using UpdateFn = void(__fastcall*)(EngineTimeInfo* self, void* edx, UInt32 tick);
UpdateFn g_original = nullptr;

long long g_frequency = 0;
long long g_previous = 0;
bool g_havePrevious = false;

bool g_trace = false;
LogFn g_log = nullptr;
FrameClockTrace g_traceData;
long long g_traceStarted = 0;

long long Counter() {
	LARGE_INTEGER value{};
	QueryPerformanceCounter(&value);
	return value.QuadPart;
}

void __fastcall FrameClockUpdate(EngineTimeInfo* self, void* /*edx*/, UInt32 tick) {
	g_original(self, nullptr, tick);
	const float engineSeconds = self->frameSeconds;
	float given = engineSeconds;
	const long long now = Counter();
	if (PreciseClockApplies(self->frozen != 0, self->clampMs, g_havePrevious)) {
		given = PreciseFrameSeconds(now - g_previous, g_frequency,
		                            *reinterpret_cast<const float*>(kTimeMultiplier));
		self->frameSeconds = given;
	}
	// The previous reading moves only when a delta was taken from it, so a
	// frozen or clamped stretch is read afterwards the way the engine reads
	// it: as one long frame, held to the ceiling.
	if (!g_havePrevious || self->frameSeconds == given) {
		g_previous = now;
		g_havePrevious = true;
	}
	if (g_trace) {
		g_traceData.Add(engineSeconds, given);
		if (g_traceStarted == 0) g_traceStarted = now;
		if (g_frequency > 0 && now - g_traceStarted >= 10 * g_frequency) {
			char line[512];
			if (g_traceData.Summary(line, sizeof(line))) {
				g_log("Frame clock: %s", line);
			}
			g_traceData.Reset();
			g_traceStarted = now;
		}
	}
}

}  // namespace

bool InstallFrameClock(bool trace, LogFn log) {
	static bool s_done = false;
	if (s_done) return g_original != nullptr;
	s_done = true;
	g_log = log;
	g_trace = trace;
	if (!Verify(kTimeInfoUpdate, kOriginalBytes, sizeof(kOriginalBytes))) {
		char bytes[32];
		DescribeBytes(kTimeInfoUpdate, bytes, sizeof(bytes));
		log("Frame clock: the time update's entry at %08X reads %s, not the game's own bytes - another plugin's "
		    "patch or another game version; left to the engine",
		    kTimeInfoUpdate, bytes);
		return false;
	}
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	g_frequency = frequency.QuadPart;
	if (g_frequency <= 0) {
		log("Frame clock: no performance counter; left to the engine");
		return false;
	}
	constexpr UInt32 capacity = 24;
	auto* trampoline = static_cast<UInt8*>(AllocExecutable(capacity));
	if (trampoline == nullptr) {
		log("Frame clock: no executable memory; left to the engine");
		return false;
	}
	if (BuildEntryTrampoline(trampoline, capacity, reinterpret_cast<UInt32>(trampoline), kTimeInfoUpdate,
	                         kOriginalBytes, sizeof(kOriginalBytes)) == 0) {
		log("Frame clock: trampoline did not fit; left to the engine");
		return false;
	}
	UInt8 patch[sizeof(kOriginalBytes)];
	if (BuildEntryPatch(patch, sizeof(patch), kTimeInfoUpdate, reinterpret_cast<UInt32>(&FrameClockUpdate),
	                    sizeof(kOriginalBytes)) == 0) {
		log("Frame clock: patch did not fit; left to the engine");
		return false;
	}
	g_original = reinterpret_cast<UpdateFn>(trampoline);
	if (!SafeWrite(kTimeInfoUpdate, patch, sizeof(patch))) {
		g_original = nullptr;
		log("Frame clock: could not write the patch; left to the engine");
		return false;
	}
	log("Frame clock: precise - the frame's seconds come from the performance counter (%lld Hz) instead of "
	    "GetTickCount's whole milliseconds; trace %s",
	    g_frequency, trace ? "on, a line every ten seconds" : "off");
	return true;
}

}  // namespace framefixes
