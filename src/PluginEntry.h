#pragma once

#include <cstdint>

// What the plugin does when xOBSE queries and loads it - with the side
// effects behind a Host so every flow runs in a test.

namespace framefixes {

// First fields of xOBSE's OBSEInterface (obse/obse/PluginAPI.h); nothing
// past isEditor is read.
struct ExtenderInterfaceHead {
	uint32_t obseVersion;
	uint32_t oblivionVersion;
	uint32_t editorVersion;
	uint32_t isEditor;
};

// PluginAPI.h PluginInfo; kInfoVersion is 3 in xOBSE 22.
struct PluginInfo {
	uint32_t infoVersion;
	const char* name;
	uint32_t version;
};

constexpr const char* kPluginName = "OblivionFrameFixes";
constexpr uint32_t kPluginVersion = 1;
constexpr uint32_t kInfoVersion = 3;

// obse_common/obse_version.h: MAKE_OBLIVION_VERSION(1, 2, 416). The patches
// sit at addresses of this build and are made on no other.
constexpr uint32_t kOblivionVersion_1_2_416 = 0x010201A0;

struct Settings {
	// [Main]
	bool preciseFrameClock = true;
	bool fastStreamCopy = true;
	bool fastVariableLookup = true;
	bool reportGains = true;  // [Main] ReportGains: the "Gains:" lines
	float gainsSeconds = 300.0f;  // [Diagnostics] GainsSeconds: how often
	bool fastEventListCast = false;  // opt-in: no gain measured (2026-10-26)
	// [Diagnostics]
	bool frameClockTrace = false;
	bool streamCopyCheck = false;
	bool variableLookupCheck = false;
	bool engineVariableTrace = false;
	bool eventListCastCheck = false;
};

struct Host {
	void (*resetLog)();
	void (*log)(const char* format, ...);
	// Reads the INI next to the DLL into *settings; false if there is no INI
	// (the defaults then stand).
	bool (*readSettings)(Settings* settings);
	// The patches; each returns whether it was made.
	bool (*installFrameClock)(bool trace);
	bool (*installStreamCopy)(bool check);
	bool (*installVarLookup)(bool check);
	// A diagnostic, not a fix: the engine's own variable lookup timed.
	bool (*installEngineVarTrace)();
	bool (*installCastCache)(bool check);
};

bool Query(const ExtenderInterfaceHead& xse, PluginInfo* info, const Host& host);
// Makes the patches the settings ask for. Always true: refusing to load
// would make nothing safer, and the log says what was skipped.
bool Load(const ExtenderInterfaceHead& xse, const Host& host);

// The state Load left; exposed for tests.
const Settings& LoadedSettings();
void ResetForTest();

}  // namespace framefixes
