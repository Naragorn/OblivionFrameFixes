#pragma once

// What the fixes gained, measured on the player's own machine (Nadi,
// 2026-10-28: "kann man die performance gains measuren in dem plugin um den
// leuten da draussen das schmackhaft zu machen").
//
// A fix that replaces a slow way with a fast one samples itself: one call in
// `every` is done both ways and both are timed with the performance counter.
// The difference per sampled call, times the calls the fix served, is the
// time saved - an estimate from samples, not a whole game measured with the
// fix on and off, and the log says so. A fix that came out slower says that
// too. The frame clock saves no time; what it gains is accuracy - how far the
// game's own whole-millisecond clock was from the frame's real length.
// [Main] ReportGains=1 writes a "Gains:" line for each fix every five minutes,
// and after each load and on quitting (GainsMomentOf): a session shorter than
// the period, or the log of the next start replacing this one's, lost them all
// (Nadi's Windows run, 2026-10-29).

#include <cmath>
#include <cstdio>

#include "Types.h"

namespace framefixes {

struct GainSampler {
	UInt32 every = 1024;  // one call in this many is done both ways; 0 never
	UInt32 countdown = 0;
	UInt32 samples = 0;
	long long fastTicks = 0;
	long long slowTicks = 0;

	bool Due() {
		if (every == 0) return false;
		if (countdown == 0) {
			countdown = every - 1;
			return true;
		}
		--countdown;
		return false;
	}

	// A counter that went back (another core's reading) is no sample.
	void Add(long long fast, long long slow) {
		if (fast < 0 || slow < 0) return;
		fastTicks += fast;
		slowTicks += slow;
		++samples;
	}

	double FastMicros(long long frequency) const {
		return samples == 0 || frequency <= 0 ? 0.0
		                                      : 1e6 * static_cast<double>(fastTicks) / samples / frequency;
	}
	double SlowMicros(long long frequency) const {
		return samples == 0 || frequency <= 0 ? 0.0
		                                      : 1e6 * static_cast<double>(slowTicks) / samples / frequency;
	}

	// Over `served` calls; negative when the fast way was slower.
	double SavedMs(UInt64 served, long long frequency) const {
		if (samples == 0 || frequency <= 0) return 0.0;
		return (SlowMicros(frequency) - FastMicros(frequency)) * static_cast<double>(served) / 1000.0;
	}
};

struct ClockAccuracy {
	UInt64 frames = 0;
	double errorSumMs = 0.0;
	double maxErrorMs = 0.0;
	UInt64 offByOneMs = 0;

	// A frame of a tenth of a second or more on either clock is a load or a
	// stall, not the clock's rounding: the game's 166 ms ceiling against the
	// real length read 152.7 to 165 ms "off" in the first run (2026-10-28).
	void Add(float engineSeconds, float givenSeconds) {
		if (!(engineSeconds < 0.1f) || !(givenSeconds < 0.1f)) return;
		const double error = std::fabs(static_cast<double>(engineSeconds) - givenSeconds) * 1000.0;
		if (!(error == error)) return;
		++frames;
		errorSumMs += error;
		if (error > maxErrorMs) maxErrorMs = error;
		if (error >= 1.0) ++offByOneMs;
	}
	double MeanErrorMs() const { return frames == 0 ? 0.0 : errorSumMs / static_cast<double>(frames); }
	double OffByOnePercent() const {
		return frames == 0 ? 0.0 : 100.0 * static_cast<double>(offByOneMs) / static_cast<double>(frames);
	}
};

// "Gains: Variable lookup - about 4.0 ms saved over 1000 lookups in 5 min
// (sampled 1 in 1024: 4.00 us the old way, 1.00 us now)".
inline bool FormatGain(char* out, size_t size, const char* what, const char* unit, const GainSampler& g, UInt64 served,
                       long long frequency, double minutes) {
	if (g.samples == 0 || frequency <= 0) return false;
	const double saved = g.SavedMs(served, frequency);
	const int n = std::snprintf(out, size,
	                            "Gains: %s - about %.1f ms %s over %llu %s in %.0f min (sampled 1 in %u: %.2f us the "
	                            "old way, %.2f us now, %u samples; an estimate, not the game run without it)",
	                            what, saved < 0.0 ? -saved : saved, saved < 0.0 ? "slower" : "saved", served, unit,
	                            minutes, g.every, g.SlowMicros(frequency), g.FastMicros(frequency), g.samples);
	return n > 0 && static_cast<size_t>(n) < size;
}

// "Gains: Frame clock - the game's own clock would have been 0.42 ms off a
// frame on average, 31% of frames a millisecond or more, at most 11.1 ms".
inline bool FormatClockAccuracy(char* out, size_t size, const ClockAccuracy& c, double minutes) {
	if (c.frames == 0) return false;
	const int n = std::snprintf(out, size,
	                            "Gains: Frame clock - over %llu frames in %.0f min the game's own clock would have been "
	                            "%.2f ms off a frame on average, %.0f%% of frames a millisecond or more, at most %.1f ms",
	                            c.frames, minutes, c.MeanErrorMs(), c.OffByOnePercent(), c.maxErrorMs);
	return n > 0 && static_cast<size_t>(n) < size;
}

// The variable lookup's samples by how far the old walk went to its answer:
// the index pays for a long walk and costs a little on a short one, so
// where it starts paying is read off these (2026-10-28: on lists of about
// 94 entries the index was 0.10 us, the walk 0.08).
inline constexpr UInt32 kWalkBuckets = 4;
inline UInt32 WalkBucket(UInt32 nodes) { return nodes < 64 ? 0 : nodes < 256 ? 1 : nodes < 1024 ? 2 : 3; }
inline const char* WalkBucketName(UInt32 bucket) {
	static const char* const names[kWalkBuckets] = {"under 64", "64 to 255", "256 to 1023", "1024 and more"};
	return bucket < kWalkBuckets ? names[bucket] : "?";
}

struct GainBuckets {
	GainSampler bucket[kWalkBuckets];
	void Add(UInt32 nodes, long long fast, long long slow) { bucket[WalkBucket(nodes)].Add(fast, slow); }
};

// "Gains: Variable lookup, walks of 1024 and more entries - 12 samples, 4.00 us
// faster a lookup (5.00 us the old way, 1.00 us now)".
inline bool FormatBucket(char* out, size_t size, const GainBuckets& b, UInt32 which, long long frequency) {
	if (which >= kWalkBuckets) return false;
	const GainSampler& g = b.bucket[which];
	if (g.samples == 0 || frequency <= 0) return false;
	const double diff = g.SlowMicros(frequency) - g.FastMicros(frequency);
	const int n = std::snprintf(out, size,
	                            "Gains: Variable lookup, walks of %s entries - %u samples, %.2f us %s a lookup (%.2f us "
	                            "the old way, %.2f us now)",
	                            WalkBucketName(which), g.samples, diff < 0.0 ? -diff : diff,
	                            diff < 0.0 ? "slower" : "faster", g.SlowMicros(frequency), g.FastMicros(frequency));
	return n > 0 && static_cast<size_t>(n) < size;
}

// The xOBSE messages after which the lines are written besides the period
// (obse/PluginAPI.h OBSEMessagingInterface: kMessage_ExitGame 1,
// kMessage_ExitToMainMenu 2, kMessage_ExitGame_Console 7,
// kMessage_PostLoadGame 8); what the log says the lines were written at, or
// nullptr for any other message.
inline constexpr UInt32 kObseExitGame = 1;
inline constexpr UInt32 kObseExitToMainMenu = 2;
inline constexpr UInt32 kObseExitGameConsole = 7;
inline constexpr UInt32 kObsePostLoadGame = 8;
inline const char* GainsMomentOf(UInt32 message) {
	switch (message) {
		case kObsePostLoadGame: return "after a load";
		case kObseExitGame:
		case kObseExitGameConsole: return "on quitting";
		case kObseExitToMainMenu: return "on leaving for the main menu";
		default: return nullptr;
	}
}

// Due once every `periodSeconds`, the first reading starting it.
struct ReportClock {
	long long next = 0;
	bool started = false;

	bool Due(long long now, long long frequency, double periodSeconds) {
		if (frequency <= 0) return false;
		const long long period = static_cast<long long>(periodSeconds * static_cast<double>(frequency));
		if (!started) {
			started = true;
			next = now + period;
			return false;
		}
		if (now < next) return false;
		next = now + period;
		return true;
	}

	// The lines were just written out of turn: the next a whole period on.
	void Restart(long long now, long long frequency, double periodSeconds) {
		if (frequency <= 0) return;
		started = true;
		next = now + static_cast<long long>(periodSeconds * static_cast<double>(frequency));
	}
};

}  // namespace framefixes
