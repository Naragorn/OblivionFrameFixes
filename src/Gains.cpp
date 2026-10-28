#include "Gains.h"

#include <windows.h>

namespace framefixes {
namespace {
bool g_on = true;
double g_period = 300.0;
long long g_frequency = 0;
}  // namespace

void SetReportGains(bool on, float periodSeconds) {
	g_on = on;
	g_period = periodSeconds >= 1.0f ? periodSeconds : 300.0;
}
double GainsPeriod() { return g_period; }
bool ReportGains() { return g_on; }

long long GainsCounter() {
	LARGE_INTEGER value{};
	QueryPerformanceCounter(&value);
	return value.QuadPart;
}

long long GainsFrequency() {
	if (g_frequency == 0) {
		LARGE_INTEGER value{};
		QueryPerformanceFrequency(&value);
		g_frequency = value.QuadPart;
	}
	return g_frequency;
}

}  // namespace framefixes
