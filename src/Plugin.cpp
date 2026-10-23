// The DLL's exports and real side effects; the decisions live in PluginEntry,
// FrameClock and StreamCopy.

#include "FrameClock.h"
#include "PluginEntry.h"
#include "StreamCopy.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kLogFile = "OblivionFrameFixes.log";
constexpr const char* kIniFile = "OblivionFrameFixes.ini";

HMODULE g_module = nullptr;
SRWLOCK g_logLock = SRWLOCK_INIT;

void AppendLine(const char* format, va_list args) {
	AcquireSRWLockExclusive(&g_logLock);
	if (FILE* file = std::fopen(kLogFile, "a")) {
		std::vfprintf(file, format, args);
		std::fputc('\n', file);
		std::fclose(file);
	}
	ReleaseSRWLockExclusive(&g_logLock);
}

void Log(const char* format, ...) {
	va_list args;
	va_start(args, format);
	AppendLine(format, args);
	va_end(args);
}

void ResetLog() {
	AcquireSRWLockExclusive(&g_logLock);
	if (FILE* file = std::fopen(kLogFile, "w")) {
		std::fclose(file);
	}
	ReleaseSRWLockExclusive(&g_logLock);
}

bool IniPath(char* path, size_t length) {
	char module[MAX_PATH] = {};
	if (GetModuleFileNameA(g_module, module, MAX_PATH) == 0) {
		return false;
	}
	char* slash = std::strrchr(module, '\\');
	if (slash == nullptr) {
		return false;
	}
	*(slash + 1) = '\0';
	std::snprintf(path, length, "%s%s", module, kIniFile);
	return true;
}

bool ReadSettings(framefixes::Settings* settings) {
	char path[MAX_PATH] = {};
	if (!IniPath(path, sizeof(path)) || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	settings->preciseFrameClock = GetPrivateProfileIntA("Main", "PreciseFrameClock", 1, path) != 0;
	settings->fastStreamCopy = GetPrivateProfileIntA("Main", "FastStreamCopy", 1, path) != 0;
	settings->frameClockTrace = GetPrivateProfileIntA("Diagnostics", "FrameClockTrace", 0, path) != 0;
	settings->streamCopyCheck = GetPrivateProfileIntA("Diagnostics", "StreamCopyCheck", 0, path) != 0;
	Log("Settings from %s: PreciseFrameClock=%d FastStreamCopy=%d FrameClockTrace=%d StreamCopyCheck=%d", path,
	    settings->preciseFrameClock ? 1 : 0, settings->fastStreamCopy ? 1 : 0, settings->frameClockTrace ? 1 : 0,
	    settings->streamCopyCheck ? 1 : 0);
	return true;
}

bool InstallFrameClockSideEffect(bool trace) {
	return framefixes::InstallFrameClock(trace, &Log);
}

bool InstallStreamCopySideEffect(bool check) {
	return framefixes::InstallStreamCopy(check, &Log);
}

const framefixes::Host kHost{&ResetLog, &Log, &ReadSettings, &InstallFrameClockSideEffect,
                             &InstallStreamCopySideEffect};

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

extern "C" {

__declspec(dllexport) bool OBSEPlugin_Query(const framefixes::ExtenderInterfaceHead* xse,
                                            framefixes::PluginInfo* info) {
	return framefixes::Query(*xse, info, kHost);
}

__declspec(dllexport) bool OBSEPlugin_Load(const framefixes::ExtenderInterfaceHead* xse) {
	return framefixes::Load(*xse, kHost);
}

}  // extern "C"
