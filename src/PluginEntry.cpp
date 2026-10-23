#include "PluginEntry.h"

namespace framefixes {

namespace {

Settings g_settings;

}  // namespace

bool Query(const ExtenderInterfaceHead& xse, PluginInfo* info, const Host& host) {
	info->infoVersion = kInfoVersion;
	info->name = kPluginName;
	info->version = kPluginVersion;
	host.resetLog();
	host.log("%s %u - xOBSE %u, Oblivion %08X, editor=%u", kPluginName, kPluginVersion, xse.obseVersion,
	         xse.oblivionVersion, xse.isEditor);
	return true;
}

bool Load(const ExtenderInterfaceHead& xse, const Host& host) {
	if (xse.isEditor != 0) {
		host.log("Editor: nothing to do");
		return true;
	}
	if (xse.oblivionVersion != kOblivionVersion_1_2_416) {
		host.log("Oblivion %08X is not 1.2.416 (%08X): the patched addresses are for 1.2.416 only, nothing done",
		         xse.oblivionVersion, kOblivionVersion_1_2_416);
		return true;
	}
	g_settings = Settings{};
	if (!host.readSettings(&g_settings)) {
		host.log("No OblivionFrameFixes.ini next to the DLL - defaults: PreciseFrameClock=1 FastStreamCopy=1");
	}
	if (g_settings.preciseFrameClock) {
		host.installFrameClock(g_settings.frameClockTrace);
	} else {
		host.log("Frame clock: left to the engine (PreciseFrameClock=0)");
	}
	if (g_settings.fastStreamCopy) {
		host.installStreamCopy(g_settings.streamCopyCheck);
	} else {
		host.log("Stream copy: the engine's (FastStreamCopy=0)");
	}
	return true;
}

const Settings& LoadedSettings() {
	return g_settings;
}

void ResetForTest() {
	g_settings = Settings{};
}

}  // namespace framefixes
