// The DLL's exports and real side effects; the decisions live in PluginEntry,
// FrameClock and StreamCopy.

#include "EngineVarTrace.h"
#include "CastCache.h"
#include "FastProtect.h"
#include "FrameClock.h"
#include "Gains.h"
#include "PluginEntry.h"
#include "StreamCopy.h"
#include "VarLookup.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kLogFile = "OblivionFrameFixes.log";
// The last start's log, kept: the next start replaced it with its own, and
// Nadi's run of 2026-10-29 lost its gains to a test start an hour later.
constexpr const char* kPreviousLogFile = "OblivionFrameFixes.log.prev";
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
	MoveFileExA(kLogFile, kPreviousLogFile, MOVEFILE_REPLACE_EXISTING);
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
	settings->fastVariableLookup = GetPrivateProfileIntA("Main", "FastVariableLookup", 1, path) != 0;
	settings->variableLookupCheck = GetPrivateProfileIntA("Diagnostics", "VariableLookupCheck", 0, path) != 0;
	settings->engineVariableTrace = GetPrivateProfileIntA("Diagnostics", "EngineVariableTrace", 0, path) != 0;
	settings->reportGains = GetPrivateProfileIntA("Main", "ReportGains", 1, path) != 0;
	settings->gainsSeconds =
		static_cast<float>(GetPrivateProfileIntA("Diagnostics", "GainsSeconds", 300, path));
	framefixes::SetReportGains(settings->reportGains, settings->gainsSeconds);
	settings->fastEventListCast = GetPrivateProfileIntA("Main", "FastEventListCast", 0, path) != 0;
	settings->eventListCastCheck = GetPrivateProfileIntA("Diagnostics", "EventListCastCheck", 0, path) != 0;
	settings->fastCodeProtect = GetPrivateProfileIntA("Main", "FastCodeProtect", 1, path) != 0;
	settings->codeProtectCheck = GetPrivateProfileIntA("Diagnostics", "CodeProtectCheck", 0, path) != 0;
	Log("Settings from %s: PreciseFrameClock=%d FastStreamCopy=%d FastVariableLookup=%d FastEventListCast=%d "
	    "FastCodeProtect=%d ReportGains=%d FrameClockTrace=%d StreamCopyCheck=%d VariableLookupCheck=%d EventListCastCheck=%d "
	    "EngineVariableTrace=%d CodeProtectCheck=%d",
	    path, settings->preciseFrameClock ? 1 : 0, settings->fastStreamCopy ? 1 : 0,
	    settings->fastVariableLookup ? 1 : 0, settings->fastEventListCast ? 1 : 0, settings->fastCodeProtect ? 1 : 0,
	    settings->reportGains ? 1 : 0,
	    settings->frameClockTrace ? 1 : 0,
	    settings->streamCopyCheck ? 1 : 0, settings->variableLookupCheck ? 1 : 0,
	    settings->eventListCastCheck ? 1 : 0, settings->engineVariableTrace ? 1 : 0, settings->codeProtectCheck ? 1 : 0);
	return true;
}

bool InstallFrameClockSideEffect(bool trace) {
	return framefixes::InstallFrameClock(trace, &Log);
}

bool InstallStreamCopySideEffect(bool check) {
	return framefixes::InstallStreamCopy(check, &Log);
}

bool InstallVarLookupSideEffect(bool check) {
	return framefixes::InstallVarLookup(check, &Log);
}

bool InstallEngineVarTraceSideEffect() {
	return framefixes::InstallEngineVarTrace(&Log);
}

bool InstallCastCacheSideEffect(bool check) {
	return framefixes::InstallCastCache(check, &Log);
}

bool InstallCodeProtectSideEffect(bool check) {
	return framefixes::InstallCodeProtect(check, &Log);
}

// xOBSE's interface past the head PluginEntry reads (obse/PluginAPI.h
// OBSEInterface; QueryInterface since the first, GetPluginHandle since v0015)
// and its messaging (kInterface_Messaging 4, since v0017).
struct ExtenderInterface {
	framefixes::ExtenderInterfaceHead head;
	bool (*RegisterCommand)(void* info);
	void (*SetOpcodeBase)(UInt32 opcode);
	void* (*QueryInterface)(UInt32 id);
	UInt32 (*GetPluginHandle)();
};
struct MessagingInterface {
	struct Message {
		const char* sender;
		UInt32 type;
		UInt32 dataLen;
		void* data;
	};
	UInt32 version;
	bool (*RegisterListener)(UInt32 listener, const char* sender, void (*handler)(Message* message));
};
constexpr UInt32 kInterfaceMessaging = 4;
constexpr UInt32 kMessagingSinceObse = 17;

const ExtenderInterface* g_xse = nullptr;

void ReportGainsNow() {
	framefixes::WriteFrameClockGainsNow();
	framefixes::WriteStreamCopyGainsNow();
	framefixes::WriteVarLookupGainsNow();
	framefixes::WriteCodeProtectGainsNow();
}

void OnObseMessage(MessagingInterface::Message* message);

bool ListenForGameMessages() {
	if (g_xse == nullptr || g_xse->head.obseVersion < kMessagingSinceObse || g_xse->QueryInterface == nullptr ||
	    g_xse->GetPluginHandle == nullptr) {
		return false;
	}
	auto* messaging = static_cast<MessagingInterface*>(g_xse->QueryInterface(kInterfaceMessaging));
	return messaging != nullptr && messaging->RegisterListener != nullptr &&
	       messaging->RegisterListener(g_xse->GetPluginHandle(), "OBSE", &OnObseMessage);
}

const framefixes::Host kHost{&ResetLog,
                             &Log,
                             &ReadSettings,
                             &InstallFrameClockSideEffect,
                             &InstallStreamCopySideEffect,
                             &InstallVarLookupSideEffect,
                             &InstallEngineVarTraceSideEffect,
                             &InstallCastCacheSideEffect,
                             &InstallCodeProtectSideEffect,
                             &ListenForGameMessages,
                             &ReportGainsNow};

void OnObseMessage(MessagingInterface::Message* message) {
	if (message != nullptr) framefixes::OnGameMessage(message->type, kHost);
}

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
	g_xse = reinterpret_cast<const ExtenderInterface*>(xse);
	return framefixes::Load(*xse, kHost);
}

}  // extern "C"
