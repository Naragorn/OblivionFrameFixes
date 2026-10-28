// Checks what the plugin says it gained (src/GainsLogic.h): the sampling
// cadence, the time a sampled call saved, extrapolated over the calls the fix
// served, a fix that came out slower, the frame clock's error, and the lines.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "GainsLogic.h"

using namespace framefixes;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++g_failures;
	}
}

bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

void TestCadence() {
	std::printf("Which calls are sampled\n");
	GainSampler g;
	g.every = 4;
	int due = 0;
	bool first = false;
	for (int i = 0; i < 12; ++i) {
		const bool d = g.Due();
		if (i == 0) first = d;
		due += d ? 1 : 0;
	}
	Check(first, "the first call is sampled");
	Check(due == 3, "one call in every four");
	GainSampler never;
	never.every = 0;
	Check(!never.Due() && !never.Due(), "every 0: never sampled");
}

void TestSaved() {
	std::printf("The time saved\n");
	GainSampler g;
	Check(Near(g.SavedMs(1000, 10000000), 0.0), "no sample: nothing claimed");
	// Two samples at 10 MHz: the slow way 50 and 30 ticks, the fast way 10 and 10.
	g.Add(10, 50);
	g.Add(10, 30);
	Check(g.samples == 2, "two samples");
	Check(Near(g.SlowMicros(10000000), 4.0) && Near(g.FastMicros(10000000), 1.0), "per call: 4 us the old way, 1 us now");
	Check(Near(g.SavedMs(1000, 10000000), 3.0), "1000 calls served at 3 us each: 3 ms saved");
	Check(Near(g.SavedMs(1000, 0), 0.0), "no counter frequency: nothing claimed");

	GainSampler slower;
	slower.Add(40, 10);
	Check(slower.SavedMs(100, 10000000) < 0.0, "a fix that came out slower says so: a negative saving");
	GainSampler bogus;
	bogus.Add(-5, 10);
	Check(bogus.samples == 0, "a negative reading (the counter went back) is not a sample");
}

void TestClock() {
	std::printf("The frame clock's error\n");
	ClockAccuracy c;
	Check(c.frames == 0 && Near(c.MeanErrorMs(), 0.0), "no frame: no error");
	c.Add(0.016f, 0.01667f);  // 0.67 ms off
	c.Add(0.017f, 0.01667f);  // 0.33 ms off
	c.Add(0.000f, 0.01111f);  // a tick that did not move: 11.11 ms off
	Check(c.frames == 3, "three frames");
	Check(Near(c.MeanErrorMs(), (0.67 + 0.33 + 11.11) / 3.0, 1e-2), "the mean error");
	Check(Near(c.maxErrorMs, 11.11, 1e-2), "the largest error");
	Check(c.offByOneMs == 1, "one frame off by a millisecond or more");
	Check(Near(c.OffByOnePercent(), 100.0 / 3.0, 1e-3), "a third of the frames");
	ClockAccuracy load;
	load.Add(0.166f, 0.0137f);
	load.Add(0.016f, 0.150f);
	Check(load.frames == 0, "a load's frame (a tenth of a second or more either way) is not a frame's error");
	ClockAccuracy nan;
	nan.Add(std::nanf(""), 0.016f);
	Check(nan.frames == 0, "a reading that is no number: not counted");
}

void TestLines() {
	std::printf("The lines\n");
	char line[256];
	GainSampler g;
	Check(!FormatGain(line, sizeof(line), "Variable lookup", "lookups", g, 1000, 10000000, 5.0),
	      "no sample yet: no line");
	g.every = 1024;
	g.Add(10, 50);
	Check(FormatGain(line, sizeof(line), "Variable lookup", "lookups", g, 1000, 10000000, 5.0),
	      "a sample: a line");
	Check(std::strstr(line, "Gains: Variable lookup") != nullptr && std::strstr(line, "4.0 ms saved") != nullptr &&
	          std::strstr(line, "1000 lookups") != nullptr && std::strstr(line, "5 min") != nullptr &&
	          std::strstr(line, "1 in 1024") != nullptr,
	      "it names the fix, the time saved, the calls, the minutes and the sampling");
	GainSampler slower;
	slower.Add(40, 10);
	FormatGain(line, sizeof(line), "Stream copy", "copies", slower, 100, 10000000, 1.0);
	Check(std::strstr(line, "slower") != nullptr, "a negative saving reads as slower, not as saved");

	ClockAccuracy c;
	Check(!FormatClockAccuracy(line, sizeof(line), c, 1.0), "no frame: no line");
	c.Add(0.016f, 0.01667f);
	c.Add(0.000f, 0.01111f);
	Check(FormatClockAccuracy(line, sizeof(line), c, 1.0) && std::strstr(line, "Gains: Frame clock") != nullptr &&
	          std::strstr(line, "50%") != nullptr,
	      "the clock's line: the error and the share of frames a millisecond off");
	Check(!FormatGain(line, 8, "Variable lookup", "lookups", g, 1000, 10000000, 5.0), "too small a buffer: refused");
}

void TestReportClock() {
	std::printf("When a report is due\n");
	ReportClock r;
	Check(!r.Due(100, 10, 60.0), "the first reading starts the clock, no report");
	Check(!r.Due(100 + 599, 10, 60.0), "before the period: none");
	Check(r.Due(100 + 600, 10, 60.0), "at the period: one");
	Check(!r.Due(100 + 601, 10, 60.0), "and the next waits a period again");
	ReportClock none;
	Check(!none.Due(5, 0, 60.0) && !none.Due(1000000, 0, 60.0), "no counter frequency: never");
}

}  // namespace

int main() {
	TestCadence();
	TestSaved();
	TestClock();
	TestLines();
	TestReportClock();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
