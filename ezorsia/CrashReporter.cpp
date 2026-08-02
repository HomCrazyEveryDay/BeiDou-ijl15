#include "stdafx.h"
#include "CrashReporter.h"

#include <DbgHelp.h>
#include <cstdarg>
#include <strsafe.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

volatile LONG g_handlingException = 0;
volatile LONG g_handledExceptionCaptured = 0;
bool g_dumpEnabled = false;
bool g_fullDump = false;
volatile LONG g_traceEnabled = 0;
SRWLOCK g_traceLock = SRWLOCK_INIT;
HANDLE g_traceFile = INVALID_HANDLE_VALUE;

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

bool GetExeDirectory(WCHAR dir[MAX_PATH])
{
	if (GetModuleFileNameW(nullptr, dir, MAX_PATH) == 0) {
		return false;
	}

	for (int i = lstrlenW(dir) - 1; i >= 0; i--) {
		if (dir[i] == L'\\' || dir[i] == L'/') {
			dir[i] = L'\0';
			return true;
		}
	}

	return false;
}

void InitializeTraceFile()
{
	WCHAR exeDir[MAX_PATH]{};
	if (!GetExeDirectory(exeDir)) {
		lstrcpynW(exeDir, L".", MAX_PATH);
	}

	WCHAR crashDir[MAX_PATH]{};
	StringCchPrintfW(crashDir, ARRAYSIZE(crashDir), L"%s\\crash", exeDir);
	CreateDirectoryW(crashDir, nullptr);

	WCHAR tracePath[MAX_PATH]{};
	StringCchPrintfW(
		tracePath,
		ARRAYSIZE(tracePath),
		L"%s\\client_trace_pid%lu.log",
		crashDir,
		GetCurrentProcessId());
	g_traceFile = CreateFileW(
		tracePath,
		GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
}

void RecordFormattedEvent(bool persist, const char* category, const char* format, va_list args)
{
	if (InterlockedCompareExchange(&g_traceEnabled, 0, 0) == 0) {
		return;
	}

	char message[256]{};
	StringCchVPrintfA(message, ARRAYSIZE(message), format != nullptr ? format : "", args);

	SYSTEMTIME now{};
	GetLocalTime(&now);
	char line[384]{};
	StringCchPrintfA(
		line,
		ARRAYSIZE(line),
		"%04u-%02u-%02u %02u:%02u:%02u.%03u tid=%lu category=%s %s\r\n",
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

	if (persist && g_traceFile != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(g_traceFile, line, lstrlenA(line), &written, nullptr);
	}
	ReleaseSRWLockExclusive(&g_traceLock);
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
		char line[192]{};
		StringCchPrintfA(
			line,
			ARRAYSIZE(line),
			"packet%03u=%04u-%02u-%02u %02u:%02u:%02u.%03u tid=%lu opcode=0x%04X size=%lu offset=%u\r\n",
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
	WCHAR exeDir[MAX_PATH]{};
	if (!GetExeDirectory(exeDir)) {
		lstrcpynW(exeDir, L".", MAX_PATH);
	}

	WCHAR crashDir[MAX_PATH]{};
	StringCchPrintfW(crashDir, ARRAYSIZE(crashDir), L"%s\\crash", exeDir);
	CreateDirectoryW(crashDir, nullptr);

	SYSTEMTIME now{};
	GetLocalTime(&now);

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
		L"BeiDou%s_%04u%02u%02u_%02u%02u%02u_pid%lu_tid%lu_code%08lX_addr%p",
		reportTag != nullptr ? reportTag : L"",
		now.wYear,
		now.wMonth,
		now.wDay,
		now.wHour,
		now.wMinute,
		now.wSecond,
		GetCurrentProcessId(),
		GetCurrentThreadId(),
		exceptionCode,
		reinterpret_cast<void*>(exceptionAddress));

	StringCchPrintfW(dumpPath, MAX_PATH, L"%s\\%s.dmp", crashDir, baseName);
	StringCchPrintfW(textPath, MAX_PATH, L"%s\\%s.txt", crashDir, baseName);
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

void WriteStackSnapshot(HANDLE file, const CONTEXT* context)
{
	if (context == nullptr) {
		return;
	}

#if defined(_M_IX86)
	ULONG_PTR stack = context->Esp;
#elif defined(_M_X64)
	ULONG_PTR stack = context->Rsp;
#else
	ULONG_PTR stack = 0;
#endif

	if (stack == 0) {
		return;
	}

	WriteText(file, "stackSnapshot=begin\r\n");
	for (int i = 0; i < 96; i++) {
		const ULONG_PTR address = stack + static_cast<ULONG_PTR>(i * sizeof(ULONG_PTR));
		ULONG_PTR value = 0;
		if (!TryReadPointer(address, &value)) {
			break;
		}

		char moduleDescription[MAX_PATH * 3]{};
		DescribeModuleAddress(moduleDescription, ARRAYSIZE(moduleDescription), value);

		char line[512]{};
#ifdef _WIN64
		StringCchPrintfA(
			line,
			ARRAYSIZE(line),
			"stack%02d=0x%016llX:0x%016llX %s\r\n",
			i,
			static_cast<unsigned long long>(address),
			static_cast<unsigned long long>(value),
			moduleDescription);
#else
		StringCchPrintfA(
			line,
			ARRAYSIZE(line),
			"stack%02d=0x%08lX:0x%08lX %s\r\n",
			i,
			static_cast<unsigned long>(address),
			static_cast<unsigned long>(value),
			moduleDescription);
#endif
		WriteText(file, line);
	}
	WriteText(file, "stackSnapshot=end\r\n");
}

void WriteTextReport(
	const WCHAR* textPath,
	const WCHAR* dumpPath,
	EXCEPTION_POINTERS* exceptionInfo,
	BOOL dumpWritten,
	const char* reportType)
{
	HANDLE file = CreateFileW(textPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	WriteText(file, "BeiDou crash report\r\n");
	WriteLine(file, "reportType", reportType);
	WriteLine(file, "processId", GetCurrentProcessId());
	WriteLine(file, "threadId", GetCurrentThreadId());
	WriteLine(file, "dumpWritten", dumpWritten ? "true" : "false");

	char dumpPathUtf8[MAX_PATH * 3]{};
	WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, dumpPathUtf8, sizeof(dumpPathUtf8), nullptr, nullptr);
	WriteLine(file, "dumpPath", dumpPathUtf8);

	WCHAR exePath[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0) {
		char exePathUtf8[MAX_PATH * 3]{};
		WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exePathUtf8, sizeof(exePathUtf8), nullptr, nullptr);
		WriteLine(file, "exePath", exePathUtf8);
	}

	WCHAR dllPath[MAX_PATH]{};
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), dllPath, MAX_PATH) != 0) {
		char dllPathUtf8[MAX_PATH * 3]{};
		WideCharToMultiByte(CP_UTF8, 0, dllPath, -1, dllPathUtf8, sizeof(dllPathUtf8), nullptr, nullptr);
		WriteLine(file, "ijl15Path", dllPathUtf8);
	}

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
	WriteStackSnapshot(file, exceptionInfo != nullptr ? exceptionInfo->ContextRecord : nullptr);
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

		const MINIDUMP_TYPE dumpType = g_fullDump
			? static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo)
			: MiniDumpNormal;

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
	g_fullDump = dumpType == "full" || dumpType == "Full" || dumpType == "FULL";
	InterlockedExchange(&g_traceEnabled, traceEnabled ? 1 : 0);
	if (traceEnabled) {
		InitializeTraceFile();
		RecordEvent(
			"session",
			"started crashDump=%s dumpType=%s pid=%lu",
			enabled ? "true" : "false",
			dumpType.c_str(),
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

void CrashReporter::RecordIncomingPacket(unsigned short opcode, unsigned long size, unsigned int offset)
{
	if (InterlockedCompareExchange(&g_traceEnabled, 0, 0) == 0) {
		return;
	}

	RecentPacket packet{};
	GetLocalTime(&packet.time);
	packet.threadId = GetCurrentThreadId();
	packet.opcode = opcode;
	packet.size = size;
	packet.offset = offset;

	AcquireSRWLockExclusive(&g_traceLock);
	g_recentPackets[g_recentPacketNext] = packet;
	g_recentPacketNext = (g_recentPacketNext + 1) % kRecentPacketCapacity;
	if (g_recentPacketCount < kRecentPacketCapacity) {
		g_recentPacketCount++;
	}
	ReleaseSRWLockExclusive(&g_traceLock);
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
