#include "stdafx.h"
#include "CrashReporter.h"

#include <DbgHelp.h>
#include <strsafe.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

volatile LONG g_handlingException = 0;
bool g_fullDump = false;

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

void BuildCrashPaths(WCHAR dumpPath[MAX_PATH], WCHAR textPath[MAX_PATH], EXCEPTION_POINTERS* exceptionInfo)
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
		L"BeiDou_%04u%02u%02u_%02u%02u%02u_pid%lu_tid%lu_code%08lX_addr%p",
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

void WriteTextReport(const WCHAR* textPath, const WCHAR* dumpPath, EXCEPTION_POINTERS* exceptionInfo, BOOL dumpWritten)
{
	HANDLE file = CreateFileW(textPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	WriteText(file, "BeiDou crash report\r\n");
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
	WriteStackSnapshot(file, exceptionInfo != nullptr ? exceptionInfo->ContextRecord : nullptr);
	CloseHandle(file);
}

LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
{
	if (InterlockedExchange(&g_handlingException, 1) != 0) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	WCHAR dumpPath[MAX_PATH]{};
	WCHAR textPath[MAX_PATH]{};
	BuildCrashPaths(dumpPath, textPath, exceptionInfo);

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

	WriteTextReport(textPath, dumpPath, exceptionInfo, dumpWritten);
	return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void CrashReporter::Install(bool enabled, const std::string& dumpType)
{
	if (!enabled) {
		return;
	}

	g_fullDump = dumpType == "full" || dumpType == "Full" || dumpType == "FULL";
	SetUnhandledExceptionFilter(HandleUnhandledException);
}
