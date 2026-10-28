// Every flow of Query and Load, with the side effects replaced by fakes.

#include "PluginEntry.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++g_failures;
	}
}

struct Fake {
	int resets = 0;
	bool iniExists = true;
	framefixes::Settings iniSettings;
	int clockInstalls = 0;
	bool clockTrace = false;
	int copyInstalls = 0;
	bool copyCheck = false;
	int lookupInstalls = 0;
	bool lookupCheck = false;
	int traceInstalls = 0;
	int castInstalls = 0;
	int protectInstalls = 0;
	bool protectCheck = false;
	bool castCheck = false;
	std::vector<std::string> lines;

	bool Logged(const char* text) const {
		for (const auto& line : lines) {
			if (line.find(text) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
};
Fake* g_fake = nullptr;

void FakeReset() { ++g_fake->resets; }
void FakeLog(const char* format, ...) {
	char buffer[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	g_fake->lines.emplace_back(buffer);
}
bool FakeReadSettings(framefixes::Settings* settings) {
	if (!g_fake->iniExists) {
		return false;
	}
	*settings = g_fake->iniSettings;
	return true;
}
bool FakeInstallClock(bool trace) {
	++g_fake->clockInstalls;
	g_fake->clockTrace = trace;
	return true;
}
bool FakeInstallCopy(bool check) {
	++g_fake->copyInstalls;
	g_fake->copyCheck = check;
	return true;
}

bool FakeInstallLookup(bool check) {
	++g_fake->lookupInstalls;
	g_fake->lookupCheck = check;
	return true;
}

bool FakeInstallTrace() {
	++g_fake->traceInstalls;
	return true;
}

bool FakeInstallCast(bool check) {
	++g_fake->castInstalls;
	g_fake->castCheck = check;
	return true;
}

bool FakeInstallProtect(bool check) {
	++g_fake->protectInstalls;
	g_fake->protectCheck = check;
	return true;
}

const framefixes::Host kHost{&FakeReset, &FakeLog, &FakeReadSettings, &FakeInstallClock, &FakeInstallCopy,
                             &FakeInstallLookup, &FakeInstallTrace, &FakeInstallCast,
                             &FakeInstallProtect};

framefixes::ExtenderInterfaceHead Game() {
	return framefixes::ExtenderInterfaceHead{22, framefixes::kOblivionVersion_1_2_416, 0, 0};
}

void TestQuery() {
	std::printf("Query\n");
	Fake fake;
	g_fake = &fake;
	framefixes::PluginInfo info{};
	Check(framefixes::Query(Game(), &info, kHost), "says yes");
	Check(info.infoVersion == framefixes::kInfoVersion && info.version == framefixes::kPluginVersion &&
	          std::string(info.name) == "OblivionFrameFixes",
	      "fills the plugin info");
	Check(fake.resets == 1 && fake.Logged("OblivionFrameFixes 1 - xOBSE 22"), "resets the log and names itself");
}

void TestLoadDefaults() {
	std::printf("Load with the defaults\n");
	Fake fake;
	fake.iniExists = false;
	g_fake = &fake;
	framefixes::ResetForTest();
	Check(framefixes::Load(Game(), kHost), "loads");
	Check(fake.Logged("No OblivionFrameFixes.ini"), "says the INI is missing");
	Check(fake.clockInstalls == 1 && !fake.clockTrace, "the frame clock is installed without the trace");
	Check(fake.copyInstalls == 1 && !fake.copyCheck, "the stream copy is installed without the check");
	Check(fake.lookupInstalls == 1 && !fake.lookupCheck, "the variable lookup is installed without the check");
	Check(fake.Logged("FastVariableLookup=1"), "the defaults name the variable lookup");
	Check(fake.traceInstalls == 0, "the engine variable trace is off by default");
	Check(fake.castInstalls == 0, "the event list cast is opt-in: not installed by default");
	Check(fake.protectInstalls == 1 && !fake.protectCheck, "the code protection is installed without the check");
	Check(framefixes::LoadedSettings().reportGains && framefixes::LoadedSettings().gainsSeconds == 300.0f,
	      "the gains are reported by default, every five minutes");
	Check(fake.Logged("FastEventListCast=0"), "the defaults name the event list cast as off");
}

void TestLoadSettings() {
	std::printf("Load with settings\n");
	Fake fake;
	fake.iniSettings.frameClockTrace = true;
	fake.iniSettings.streamCopyCheck = true;
	fake.iniSettings.variableLookupCheck = true;
	fake.iniSettings.engineVariableTrace = true;
	fake.iniSettings.fastEventListCast = true;
	fake.iniSettings.eventListCastCheck = true;
	fake.iniSettings.codeProtectCheck = true;
	g_fake = &fake;
	framefixes::ResetForTest();
	framefixes::Load(Game(), kHost);
	Check(fake.clockInstalls == 1 && fake.clockTrace, "the trace reaches the frame clock");
	Check(fake.copyInstalls == 1 && fake.copyCheck, "the check reaches the stream copy");
	Check(fake.lookupInstalls == 1 && fake.lookupCheck, "the check reaches the variable lookup");
	Check(fake.traceInstalls == 1, "EngineVariableTrace=1 installs the engine variable trace");
	Check(fake.castInstalls == 1 && fake.castCheck, "the check reaches the event list cast");
	Check(fake.protectInstalls == 1 && fake.protectCheck, "the check reaches the code protection");
	Check(framefixes::LoadedSettings().frameClockTrace, "the settings are kept");

	Fake off;
	off.iniSettings.preciseFrameClock = false;
	off.iniSettings.fastStreamCopy = false;
	off.iniSettings.fastVariableLookup = false;
	off.iniSettings.engineVariableTrace = true;
	off.iniSettings.fastEventListCast = false;
	off.iniSettings.fastCodeProtect = false;
	g_fake = &off;
	framefixes::ResetForTest();
	framefixes::Load(Game(), kHost);
	Check(off.clockInstalls == 0 && off.Logged("PreciseFrameClock=0"), "PreciseFrameClock=0: not installed, said");
	Check(off.copyInstalls == 0 && off.Logged("FastStreamCopy=0"), "FastStreamCopy=0: not installed, said");
	Check(off.lookupInstalls == 0 && off.Logged("FastVariableLookup=0"), "FastVariableLookup=0: not installed, said");
	Check(off.traceInstalls == 1, "the trace stands on its own, with every fix off");
	Check(off.castInstalls == 0 && off.Logged("FastEventListCast=0"), "FastEventListCast=0: not installed, said");
	Check(off.protectInstalls == 0 && off.Logged("FastCodeProtect=0"), "FastCodeProtect=0: not installed, said");

	Fake one;
	one.iniSettings.fastStreamCopy = false;
	g_fake = &one;
	framefixes::ResetForTest();
	framefixes::Load(Game(), kHost);
	Check(one.clockInstalls == 1 && one.copyInstalls == 0 && one.lookupInstalls == 1 && one.castInstalls == 0,
	      "one off, the other defaults on, the opt-in cast off");

	Fake castOnly;
	castOnly.iniSettings.preciseFrameClock = false;
	castOnly.iniSettings.fastStreamCopy = false;
	castOnly.iniSettings.fastVariableLookup = false;
	castOnly.iniSettings.fastEventListCast = true;
	g_fake = &castOnly;
	framefixes::ResetForTest();
	framefixes::Load(Game(), kHost);
	Check(castOnly.castInstalls == 1 && castOnly.lookupInstalls == 0,
	      "the event list cast stands on its own, without the variable lookup");
}

void TestLoadRefusals() {
	std::printf("Load where nothing is done\n");
	Fake editor;
	g_fake = &editor;
	framefixes::ResetForTest();
	framefixes::ExtenderInterfaceHead xse = Game();
	xse.isEditor = 1;
	Check(framefixes::Load(xse, kHost) && editor.clockInstalls == 0 && editor.copyInstalls == 0 &&
	          editor.lookupInstalls == 0 && editor.traceInstalls == 0 && editor.castInstalls == 0 &&
	          editor.protectInstalls == 0 &&
	          editor.Logged("Editor"),
	      "the editor gets nothing");

	Fake other;
	g_fake = &other;
	framefixes::ResetForTest();
	xse = Game();
	xse.oblivionVersion = 0x010201A1;
	Check(framefixes::Load(xse, kHost) && other.clockInstalls == 0 && other.copyInstalls == 0 &&
	          other.lookupInstalls == 0 && other.traceInstalls == 0 && other.castInstalls == 0 &&
	          other.protectInstalls == 0 &&
	          other.Logged("is not 1.2.416"),
	      "another game version gets nothing, and the log says which");
}

}  // namespace

int main() {
	TestQuery();
	TestLoadDefaults();
	TestLoadSettings();
	TestLoadRefusals();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
