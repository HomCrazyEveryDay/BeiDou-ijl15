#include "stdafx.h"
#include "CrashReporter.h"
#include "ClientLog.h"
#include "ClientDiagnostics.h"

#include <DbgHelp.h>
#include <cstdarg>
#include <strsafe.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

volatile LONG g_handlingException = 0;
volatile LONG g_handledExceptionCaptured = 0;
bool g_dumpEnabled = false;
enum class DumpMode {
	Normal,
	Triage,
	Full
};
DumpMode g_dumpMode = DumpMode::Triage;
volatile LONG g_traceEnabled = 0;
SRWLOCK g_traceLock = SRWLOCK_INIT;

constexpr size_t kRecentEventCapacity = 128;
constexpr size_t kRecentPacketCapacity = 256;

struct RecentEvent {
	char text[384]{};
};

struct RecentPacket {
	SYSTEMTIME time{};
	DWORD threadId = 0;
	unsigned short opcode = 0;
	unsigned long size = 0;
	unsigned int offset = 0;
};

RecentEvent g_recentEvents[kRecentEventCapacity]{};
size_t g_recentEventNext = 0;
size_t g_recentEventCount = 0;
RecentPacket g_recentPackets[kRecentPacketCapacity]{};
size_t g_recentPacketNext = 0;
size_t g_recentPacketCount = 0;
DWORD g_lastActivityTick = 0;
unsigned long g_receivedPacketCount = 0;

const char* GetDumpModeName()
{
	switch (g_dumpMode) {
	case DumpMode::Normal:
		return "normal";
	case DumpMode::Full:
		return "full";
	case DumpMode::Triage:
	default:
		return "mini-triage";
	}
}

MINIDUMP_TYPE GetMiniDumpType()
{
	if (g_dumpMode == DumpMode::Full) {
		return static_cast<MINIDUMP_TYPE>(
			MiniDumpWithFullMemory
			| MiniDumpWithHandleData
			| MiniDumpWithUnloadedModules
			| MiniDumpWithFullMemoryInfo
			| MiniDumpWithThreadInfo);
	}
	if (g_dumpMode == DumpMode::Normal) {
		return MiniDumpNormal;
	}

	// Keep the default dump compact while retaining stack-referenced heap objects.
	// Those objects are required to diagnose native animation/resource crashes.
	return static_cast<MINIDUMP_TYPE>(
		MiniDumpWithDataSegs
		| MiniDumpScanMemory
		| MiniDumpWithIndirectlyReferencedMemory
		| MiniDumpWithUnloadedModules
		| MiniDumpWithProcessThreadData
		| MiniDumpWithFullMemoryInfo
		| MiniDumpWithThreadInfo);
}

void WriteText(HANDLE file, const char* text)
{
	DWORD written = 0;
	WriteFile(file, text, lstrlenA(text), &written, nullptr);
}

void WriteLine(HANDLE file, const char* key, const char* value)
{
	WriteText(file, key);
	WriteText(file, "=");
	WriteText(file, value != nullptr ? value : "");
	WriteText(file, "\r\n");
}

void WriteLine(HANDLE file, const char* key, DWORD value)
{
	char line[64]{};
	StringCchPrintfA(line, ARRAYSIZE(line), "%lu", value);
	WriteLine(file, key, line);
}

void WriteHexLine(HANDLE file, const char* key, ULONG_PTR value)
{
	char line[64]{};
#ifdef _WIN64
	StringCchPrintfA(line, ARRAYSIZE(line), "0x%016llX", static_cast<unsigned long long>(value));
#else
	StringCchPrintfA(line, ARRAYSIZE(line), "0x%08lX", static_cast<unsigned long>(value));
#endif
	WriteLine(file, key, line);
}

void WriteModuleBuildInfo(HANDLE file, const char* keyPrefix, HMODULE module)
{
	if (module == nullptr) {
		return;
	}

	DWORD timestamp = 0;
	DWORD imageSize = 0;
	bool validImage = false;
	__try {
		const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
			const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
				reinterpret_cast<const unsigned char*>(module) + dosHeader->e_lfanew);
			if (ntHeaders->Signature == IMAGE_NT_SIGNATURE) {
				timestamp = ntHeaders->FileHeader.TimeDateStamp;
				imageSize = ntHeaders->OptionalHeader.SizeOfImage;
				validImage = true;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		validImage = false;
	}

	char key[96]{};
	StringCchPrintfA(key, ARRAYSIZE(key), "%sBase", keyPrefix);
	WriteHexLine(file, key, reinterpret_cast<ULONG_PTR>(module));
	if (validImage) {
		StringCchPrintfA(key, ARRAYSIZE(key), "%sTimestamp", keyPrefix);
		WriteHexLine(file, key, timestamp);
		StringCchPrintfA(key, ARRAYSIZE(key), "%sImageSize", keyPrefix);
		WriteHexLine(file, key, imageSize);
	}
}

bool TryReadPointer(ULONG_PTR address, ULONG_PTR* value)
{
	if (value == nullptr || address == 0) {
		return false;
	}

	__try {
		*value = *reinterpret_cast<ULONG_PTR*>(address);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void RecordFormattedEvent(bool persist, const char* category, const char* format, va_list args)
{
	if (InterlockedCompareExchange(&g_traceEnabled, 0, 0) == 0) {
		return;
	}

	char message[256]{};
	StringCchVPrintfA(message, ARRAYSIZE(message), format != nullptr ? format : "", args);

	SYSTEMTIME now{};
	GetSystemTime(&now);
	char line[384]{};
	StringCchPrintfA(
		line,
		ARRAYSIZE(line),
		"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ tid=%lu category=%s %s\r\n",
		now.wYear,
		now.wMonth,
		now.wDay,
		now.wHour,
		now.wMinute,
		now.wSecond,
		now.wMilliseconds,
		GetCurrentThreadId(),
		category != nullptr ? category : "unknown",
		message);

	AcquireSRWLockExclusive(&g_traceLock);
	lstrcpynA(g_recentEvents[g_recentEventNext].text, line, ARRAYSIZE(g_recentEvents[g_recentEventNext].text));
	g_recentEventNext = (g_recentEventNext + 1) % kRecentEventCapacity;
	if (g_recentEventCount < kRecentEventCapacity) {
		g_recentEventCount++;
	}

	ReleaseSRWLockExclusive(&g_traceLock);
	if (persist) {
		ClientLog::Append(ClientLog::Component::Trace, "category=%s %s", category ? category : "unknown", message);
	}
}

void WriteRecentTrace(HANDLE file)
{
	WriteLine(file, "crashTraceEnabled", InterlockedCompareExchange(&g_traceEnabled, 0, 0) != 0 ? "true" : "false");
	if (InterlockedCompareExchange(&g_traceEnabled, 0, 0) == 0) {
		return;
	}

	if (!TryAcquireSRWLockShared(&g_traceLock)) {
		WriteLine(file, "recentTraceUnavailable", "lockBusy");
		return;
	}

	WriteText(file, "recentEvents=begin\r\n");
	const size_t eventStart = (g_recentEventNext + kRecentEventCapacity - g_recentEventCount) % kRecentEventCapacity;
	for (size_t i = 0; i < g_recentEventCount; i++) {
		const size_t index = (eventStart + i) % kRecentEventCapacity;
		WriteText(file, g_recentEvents[index].text);
	}
	WriteText(file, "recentEvents=end\r\n");

	WriteText(file, "recentIncomingPackets=begin\r\n");
	const size_t packetStart = (g_recentPacketNext + kRecentPacketCapacity - g_recentPacketCount) % kRecentPacketCapacity;
	for (size_t i = 0; i < g_recentPacketCount; i++) {
		const size_t index = (packetStart + i) % kRecentPacketCapacity;
		const RecentPacket& packet = g_recentPackets[index];
		char line[384]{};
		StringCchPrintfA(
			line,
			ARRAYSIZE(line),
			"packet%03u=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ tid=%lu opcode=0x%04X size=%lu offset=%u\r\n",
			static_cast<unsigned int>(i),
			packet.time.wYear,
			packet.time.wMonth,
			packet.time.wDay,
			packet.time.wHour,
			packet.time.wMinute,
			packet.time.wSecond,
			packet.time.wMilliseconds,
			packet.threadId,
			static_cast<unsigned int>(packet.opcode),
			packet.size,
			packet.offset);
		WriteText(file, line);
	}
	WriteText(file, "recentIncomingPackets=end\r\n");
	ReleaseSRWLockShared(&g_traceLock);
}

void BuildCrashPaths(
	WCHAR dumpPath[MAX_PATH],
	WCHAR textPath[MAX_PATH],
	EXCEPTION_POINTERS* exceptionInfo,
	const WCHAR* reportTag)
{
	const WCHAR* logDir = ClientLog::Directory();
	if (!logDir[0]) return;
	WCHAR crashDir[MAX_PATH]{};
	StringCchPrintfW(crashDir, ARRAYSIZE(crashDir), L"%s\\crash", logDir);
	CreateDirectoryW(crashDir, nullptr);

	const DWORD exceptionCode = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
		? exceptionInfo->ExceptionRecord->ExceptionCode
		: 0;
	const ULONG_PTR exceptionAddress = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
		? reinterpret_cast<ULONG_PTR>(exceptionInfo->ExceptionRecord->ExceptionAddress)
		: 0;

	WCHAR baseName[MAX_PATH]{};
	StringCchPrintfW(
		baseName,
		ARRAYSIZE(baseName),
		L"ijl15-crash-%s%s-tid%lu-code%08lX-addr%p",
		ClientLog::SessionId(),
		reportTag != nullptr ? reportTag : L"",
		GetCurrentThreadId(),
		exceptionCode,
		reinterpret_cast<void*>(exceptionAddress));

	StringCchPrintfW(dumpPath, MAX_PATH, L"%s\\%s.dmp", crashDir, baseName);
	StringCchPrintfW(textPath, MAX_PATH, L"%s\\%s.txt", logDir, baseName);
}

void WriteModuleInfo(HANDLE file, const char* keyPrefix, void* address)
{
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(address),
		&module)) {
		WriteLine(file, (std::string(keyPrefix) + "Module").c_str(), "unknown");
		return;
	}

	WCHAR modulePath[MAX_PATH]{};
	GetModuleFileNameW(module, modulePath, MAX_PATH);

	char modulePathUtf8[MAX_PATH * 3]{};
	WideCharToMultiByte(CP_UTF8, 0, modulePath, -1, modulePathUtf8, sizeof(modulePathUtf8), nullptr, nullptr);

	const ULONG_PTR moduleBase = reinterpret_cast<ULONG_PTR>(module);
	const ULONG_PTR target = reinterpret_cast<ULONG_PTR>(address);
	WriteLine(file, (std::string(keyPrefix) + "Module").c_str(), modulePathUtf8);
	WriteHexLine(file, (std::string(keyPrefix) + "ModuleBase").c_str(), moduleBase);
	WriteHexLine(file, (std::string(keyPrefix) + "ModuleOffset").c_str(), target >= moduleBase ? target - moduleBase : 0);
}

bool DescribeModuleAddress(char* output, size_t outputSize, ULONG_PTR address)
{
	if (output == nullptr || outputSize == 0) {
		return false;
	}

	HMODULE module = nullptr;
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(address),
		&module)) {
		output[0] = '\0';
		return false;
	}

	WCHAR modulePath[MAX_PATH]{};
	GetModuleFileNameW(module, modulePath, MAX_PATH);

	const WCHAR* moduleName = modulePath;
	for (const WCHAR* cursor = modulePath; *cursor != L'\0'; cursor++) {
		if (*cursor == L'\\' || *cursor == L'/') {
			moduleName = cursor + 1;
		}
	}

	char moduleNameUtf8[MAX_PATH * 3]{};
	WideCharToMultiByte(CP_UTF8, 0, moduleName, -1, moduleNameUtf8, sizeof(moduleNameUtf8), nullptr, nullptr);

	const ULONG_PTR moduleBase = reinterpret_cast<ULONG_PTR>(module);
#ifdef _WIN64
	StringCchPrintfA(
		output,
		outputSize,
		"%s+0x%llX",
		moduleNameUtf8,
		static_cast<unsigned long long>(address >= moduleBase ? address - moduleBase : 0));
#else
	StringCchPrintfA(
		output,
		outputSize,
		"%s+0x%lX",
		moduleNameUtf8,
		static_cast<unsigned long>(address >= moduleBase ? address - moduleBase : 0));
#endif
	return true;
}

void WriteExceptionParameters(HANDLE file, const EXCEPTION_RECORD* record)
{
	if (record == nullptr) {
		return;
	}

	for (DWORD i = 0; i < record->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++) {
		char key[64]{};
		StringCchPrintfA(key, ARRAYSIZE(key), "exceptionInformation%lu", i);
		WriteHexLine(file, key, record->ExceptionInformation[i]);
	}
}

void WriteContext(HANDLE file, const CONTEXT* context)
{
	if (context == nullptr) {
		return;
	}

#if defined(_M_IX86)
	WriteHexLine(file, "EAX", context->Eax);
	WriteHexLine(file, "EBX", context->Ebx);
	WriteHexLine(file, "ECX", context->Ecx);
	WriteHexLine(file, "EDX", context->Edx);
	WriteHexLine(file, "ESI", context->Esi);
	WriteHexLine(file, "EDI", context->Edi);
	WriteHexLine(file, "EBP", context->Ebp);
	WriteHexLine(file, "ESP", context->Esp);
	WriteHexLine(file, "EIP", context->Eip);
	WriteHexLine(file, "EFLAGS", context->EFlags);
#elif defined(_M_X64)
	WriteHexLine(file, "RAX", context->Rax);
	WriteHexLine(file, "RBX", context->Rbx);
	WriteHexLine(file, "RCX", context->Rcx);
	WriteHexLine(file, "RDX", context->Rdx);
	WriteHexLine(file, "RSI", context->Rsi);
	WriteHexLine(file, "RDI", context->Rdi);
	WriteHexLine(file, "RBP", context->Rbp);
	WriteHexLine(file, "RSP", context->Rsp);
	WriteHexLine(file, "RIP", context->Rip);
	WriteHexLine(file, "EFLAGS", context->EFlags);
#endif
}

#if defined(_M_IX86)
void WriteAnimationContext(HANDLE file, ULONG_PTR displayerFrame)
{
	// BeiDou.exe CAnimationDisplayer::Update keeps the current animation object
	// in this local slot immediately before its virtual update call.
	constexpr ULONG_PTR kAnimationObjectLocalOffset = 0x28;
	constexpr ULONG_PTR kAnimationUpdateVtableOffset = 0x2C;
	constexpr ULONG_PTR kAnimationDisplayerGlobalOffset = 0x007EBF6C;
	constexpr ULONG_PTR kNormalDamageResourceOffset = 0x170;
	constexpr ULONG_PTR kNormalDamageResource2Offset = 0x174;
	constexpr ULONG_PTR kCriticalDamageResourceOffset = 0x188;
	constexpr ULONG_PTR kCriticalDamageResource2Offset = 0x18C;

	WriteHexLine(file, "animationDisplayerFrame", displayerFrame);
	const ULONG_PTR objectSlot = displayerFrame >= kAnimationObjectLocalOffset
		? displayerFrame - kAnimationObjectLocalOffset
		: 0;
	WriteHexLine(file, "animationObjectSlot", objectSlot);

	ULONG_PTR objectAddress = 0;
	if (TryReadPointer(objectSlot, &objectAddress)) {
		WriteHexLine(file, "animationObject", objectAddress);
		ULONG_PTR vtable = 0;
		if (TryReadPointer(objectAddress, &vtable)) {
			WriteHexLine(file, "animationVtable", vtable);
			ULONG_PTR updateMethod = 0;
			if (TryReadPointer(vtable + kAnimationUpdateVtableOffset, &updateMethod)) {
				WriteHexLine(file, "animationUpdateMethod", updateMethod);
				WriteModuleInfo(file, "animationUpdate", reinterpret_cast<void*>(updateMethod));
			}
		}
	}

	const ULONG_PTR exeBase = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
	const ULONG_PTR displayerGlobalSlot = exeBase + kAnimationDisplayerGlobalOffset;
	WriteHexLine(file, "animationDisplayerGlobalSlot", displayerGlobalSlot);
	ULONG_PTR displayer = 0;
	if (!TryReadPointer(displayerGlobalSlot, &displayer)) {
		return;
	}
	WriteHexLine(file, "animationDisplayer", displayer);

	struct DamageResourceOffset {
		const char* key;
		ULONG_PTR offset;
	};
	const DamageResourceOffset resources[] = {
		{"normalDamageResource", kNormalDamageResourceOffset},
		{"normalDamageResource2", kNormalDamageResource2Offset},
		{"criticalDamageResource", kCriticalDamageResourceOffset},
		{"criticalDamageResource2", kCriticalDamageResource2Offset}
	};
	for (const DamageResourceOffset& resource : resources) {
		ULONG_PTR value = 0;
		if (TryReadPointer(displayer + resource.offset, &value)) {
			WriteHexLine(file, resource.key, value);
		}
	}
}
#endif

void WriteFrameWalk(HANDLE file, const CONTEXT* context)
{
	if (context == nullptr) {
		return;
	}

#if defined(_M_IX86)
	ULONG_PTR frame = context->Ebp;
	const ULONG_PTR animationUpdateReturn =
		reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr)) + 0x005E45F5;
	bool animationContextWritten = false;
#elif defined(_M_X64)
	ULONG_PTR frame = context->Rbp;
#else
	ULONG_PTR frame = 0;
#endif
	if (frame == 0) {
		return;
	}

	WriteText(file, "frameWalk=begin\r\n");
	for (int i = 0; i < 32; i++) {
		ULONG_PTR nextFrame = 0;
		ULONG_PTR returnAddress = 0;
		if (!TryReadPointer(frame, &nextFrame)
			|| !TryReadPointer(frame + sizeof(ULONG_PTR), &returnAddress)) {
			break;
		}

		char moduleDescription[MAX_PATH * 3]{};
		DescribeModuleAddress(moduleDescription, ARRAYSIZE(moduleDescription), returnAddress);
		char line[512]{};
#ifdef _WIN64
		StringCchPrintfA(
			line,
			ARRAYSIZE(line),
			"frame%02d=rbp:0x%016llX next:0x%016llX return:0x%016llX %s\r\n",
			i,
			static_cast<unsigned long long>(frame),
			static_cast<unsigned long long>(nextFrame),
			static_cast<unsigned long long>(returnAddress),
			moduleDescription);
#else
		StringCchPrintfA(
			line,
			ARRAYSIZE(line),
			"frame%02d=ebp:0x%08lX next:0x%08lX return:0x%08lX %s\r\n",
			i,
			static_cast<unsigned long>(frame),
			static_cast<unsigned long>(nextFrame),
			static_cast<unsigned long>(returnAddress),
			moduleDescription);
#endif
		WriteText(file, line);

#if defined(_M_IX86)
		if (!animationContextWritten && returnAddress == animationUpdateReturn) {
			WriteAnimationContext(file, nextFrame);
			animationContextWritten = true;
		}
#endif

		if (nextFrame <= frame || nextFrame - frame > 0x100000) {
			break;
		}
		frame = nextFrame;
	}
	WriteText(file, "frameWalk=end\r\n");
}

void WriteTextReport(
	const WCHAR* textPath,
	const WCHAR* dumpPath,
	EXCEPTION_POINTERS* exceptionInfo,
	BOOL dumpWritten,
	const char* reportType)
{
	HANDLE file = CreateFileW(textPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	WriteText(file, "BeiDou crash report\r\n");
	SYSTEMTIME now{};
	GetSystemTime(&now);
	char time[80]{};
	StringCchPrintfA(time, ARRAYSIZE(time), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
	WriteLine(file, "timestamp", time);
	WriteLine(file, "reportType", reportType);
	WriteLine(file, "processId", GetCurrentProcessId());
	WriteLine(file, "threadId", GetCurrentThreadId());
	ClientDiagnostics::Snapshot diagnostic;
	const bool snapshotAvailable = ClientDiagnostics::TrySnapshot(diagnostic);
	WriteLine(file, "clientRunId", diagnostic.clientRunId);
	WriteLine(file, "connectionId", diagnostic.connectionId);
	WriteLine(file, "diagnosticAttempt", static_cast<DWORD>(diagnostic.diagnosticAttempt));
	WriteLine(file, "connectionState", ClientDiagnostics::StateName(diagnostic.connectionState));
	WriteLine(file, "diagnosticSnapshot", snapshotAvailable ? "available" : "unavailable");
	WriteLine(file, "dumpWritten", dumpWritten ? "true" : "false");
	WriteLine(file, "dumpType", GetDumpModeName());

	char dumpPathUtf8[MAX_PATH * 3]{};
	WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, dumpPathUtf8, sizeof(dumpPathUtf8), nullptr, nullptr);
	WriteLine(file, "dumpPath", dumpPathUtf8);

	WCHAR exePath[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0) {
		char exePathUtf8[MAX_PATH * 3]{};
		WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exePathUtf8, sizeof(exePathUtf8), nullptr, nullptr);
		WriteLine(file, "exePath", exePathUtf8);
	}
	WriteModuleBuildInfo(file, "exe", GetModuleHandleW(nullptr));

	WCHAR dllPath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), dllPath, MAX_PATH) != 0) {
		char dllPathUtf8[MAX_PATH * 3]{};
		WideCharToMultiByte(CP_UTF8, 0, dllPath, -1, dllPathUtf8, sizeof(dllPathUtf8), nullptr, nullptr);
		WriteLine(file, "ijl15Path", dllPathUtf8);
	}
	WriteModuleBuildInfo(file, "ijl15", reinterpret_cast<HMODULE>(&__ImageBase));

	if (exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr) {
		const auto* record = exceptionInfo->ExceptionRecord;
		WriteHexLine(file, "exceptionCode", record->ExceptionCode);
		WriteHexLine(file, "exceptionFlags", record->ExceptionFlags);
		WriteHexLine(file, "exceptionAddress", reinterpret_cast<ULONG_PTR>(record->ExceptionAddress));
		WriteModuleInfo(file, "fault", record->ExceptionAddress);
		WriteExceptionParameters(file, record);
	}

	WriteContext(file, exceptionInfo != nullptr ? exceptionInfo->ContextRecord : nullptr);
	WriteRecentTrace(file);
	// Uploadable text keeps module/frame metadata; arbitrary memory remains local in the dump.
	WriteFrameWalk(file, exceptionInfo != nullptr ? exceptionInfo->ContextRecord : nullptr);
	CloseHandle(file);
}

void WriteExceptionArtifacts(EXCEPTION_POINTERS* exceptionInfo, const WCHAR* reportTag, const char* reportType)
{
	WCHAR dumpPath[MAX_PATH]{};
	WCHAR textPath[MAX_PATH]{};
	BuildCrashPaths(dumpPath, textPath, exceptionInfo, reportTag);

	HANDLE dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	BOOL dumpWritten = FALSE;
	if (dumpFile != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION dumpExceptionInfo{};
		dumpExceptionInfo.ThreadId = GetCurrentThreadId();
		dumpExceptionInfo.ExceptionPointers = exceptionInfo;
		dumpExceptionInfo.ClientPointers = FALSE;

		const MINIDUMP_TYPE dumpType = GetMiniDumpType();

		dumpWritten = MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			dumpFile,
			dumpType,
			&dumpExceptionInfo,
			nullptr,
			nullptr);
		CloseHandle(dumpFile);
	}

	WriteTextReport(textPath, dumpPath, exceptionInfo, dumpWritten, reportType);
}

LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
{
	if (InterlockedExchange(&g_handlingException, 1) != 0) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	WriteExceptionArtifacts(exceptionInfo, L"", "unhandled");
	return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void CrashReporter::Install(bool enabled, const std::string& dumpType, bool traceEnabled)
{
	g_dumpEnabled = enabled;
	if (dumpType == "full" || dumpType == "Full" || dumpType == "FULL") {
		g_dumpMode = DumpMode::Full;
	} else if (dumpType == "normal" || dumpType == "Normal" || dumpType == "NORMAL") {
		g_dumpMode = DumpMode::Normal;
	} else {
		g_dumpMode = DumpMode::Triage;
	}
	InterlockedExchange(&g_traceEnabled, traceEnabled ? 1 : 0);
	if (traceEnabled) {
		RecordEvent(
			"session",
			"started crashDump=%s requestedDumpType=%s effectiveDumpType=%s pid=%lu",
			enabled ? "true" : "false",
			dumpType.c_str(),
			GetDumpModeName(),
			GetCurrentProcessId());
	}

	if (!enabled) {
		return;
	}

	SetUnhandledExceptionFilter(HandleUnhandledException);
}

void CrashReporter::RecordEvent(const char* category, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	RecordFormattedEvent(true, category, format, args);
	va_end(args);
}

void CrashReporter::RecordRecentEvent(const char* category, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	RecordFormattedEvent(false, category, format, args);
	va_end(args);
}

void CrashReporter::RecordIncomingPacket(
	unsigned short opcode,
	unsigned long size,
	unsigned int offset,
	const unsigned char* payload,
	size_t payloadSize)
{
	if (InterlockedCompareExchange(&g_traceEnabled, 0, 0) == 0) {
		return;
	}

	RecentPacket packet{};
	GetSystemTime(&packet.time);
	packet.threadId = GetCurrentThreadId();
	packet.opcode = opcode;
	packet.size = size;
	packet.offset = offset;
	// Keep the call signature for existing hooks without retaining packet contents.
	(void)payload;
	(void)payloadSize;

	AcquireSRWLockExclusive(&g_traceLock);
	g_recentPackets[g_recentPacketNext] = packet;
	g_recentPacketNext = (g_recentPacketNext + 1) % kRecentPacketCapacity;
	if (g_recentPacketCount < kRecentPacketCapacity) {
		g_recentPacketCount++;
	}
	const unsigned long received = ++g_receivedPacketCount;
	const DWORD tick = GetTickCount();
	const bool reportActivity = received == 1 || tick - g_lastActivityTick >= 60000;
	if (reportActivity) g_lastActivityTick = tick;
	ReleaseSRWLockExclusive(&g_traceLock);
	if (reportActivity || opcode == 0x11) {
		RecordEvent("network.receive", "event=%s packets=%lu lastOpcode=0x%04X size=%lu uptimeMs=%llu",
			opcode == 0x11 ? "server_ping" : "activity", received, opcode, size, GetTickCount64());
	}
}

LONG CrashReporter::CaptureHandledException(
	const char* category,
	const char* stage,
	EXCEPTION_POINTERS* exceptionInfo)
{
	const EXCEPTION_RECORD* record = exceptionInfo != nullptr ? exceptionInfo->ExceptionRecord : nullptr;
	const DWORD code = record != nullptr ? record->ExceptionCode : 0;
	const ULONG_PTR address = record != nullptr
		? reinterpret_cast<ULONG_PTR>(record->ExceptionAddress)
		: 0;
	char moduleDescription[MAX_PATH * 3]{};
	DescribeModuleAddress(moduleDescription, ARRAYSIZE(moduleDescription), address);
	RecordEvent(
		category,
		"stage=%s code=0x%08lX address=%p location=%s",
		stage != nullptr ? stage : "unknown",
		code,
		reinterpret_cast<void*>(address),
		moduleDescription[0] != '\0' ? moduleDescription : "unknown");

#if defined(_M_IX86)
	if (exceptionInfo != nullptr && exceptionInfo->ContextRecord != nullptr) {
		const CONTEXT* context = exceptionInfo->ContextRecord;
		RecordEvent(
			category,
			"registers eax=0x%08lX ebx=0x%08lX ecx=0x%08lX edx=0x%08lX esi=0x%08lX edi=0x%08lX ebp=0x%08lX esp=0x%08lX eip=0x%08lX",
			context->Eax,
			context->Ebx,
			context->Ecx,
			context->Edx,
			context->Esi,
			context->Edi,
			context->Ebp,
			context->Esp,
			context->Eip);
	}
#endif

	if (g_dumpEnabled && InterlockedExchange(&g_handledExceptionCaptured, 1) == 0) {
		WriteExceptionArtifacts(exceptionInfo, L"_handled", "handled");
	}
	return EXCEPTION_EXECUTE_HANDLER;
}
