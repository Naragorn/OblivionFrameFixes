#pragma once

// [Main] ReportGains (src/GainsLogic.h): whether the fixes sample and report
// what they gained. Set once from the INI before the fixes are installed.

namespace framefixes {

void SetReportGains(bool on, float periodSeconds);
bool ReportGains();
// How often each fix writes its line, seconds ([Diagnostics] GainsSeconds).
double GainsPeriod();

// The performance counter, and its frequency (0 before it is read).
long long GainsCounter();
long long GainsFrequency();

}  // namespace framefixes
