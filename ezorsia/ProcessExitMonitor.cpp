#include "stdafx.h"
#include "ProcessExitMonitor.h"
#include "ClientLog.h"
#include <strsafe.h>

bool ProcessExitMonitor::Start() {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    wchar_t* slash = wcsrchr(executable, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    if (FAILED(StringCchCatW(executable, ARRAYSIZE(executable), L"BeiDouExitMonitor.exe"))) return false;
    HANDLE process = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &process,
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) {
        ClientLog::Emergency("exit_monitor_start_failed stage=duplicate win32Error=%lu", GetLastError());
        return false;
    }
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    auto attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, bytes));
    bool initialized = attributes && InitializeProcThreadAttributeList(attributes, 1, 0, &bytes);
    BOOL started = FALSE;
    DWORD error = GetLastError();
    if (initialized && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        &process, sizeof(process), nullptr, nullptr)) {
        wchar_t command[3 * MAX_PATH]{};
        if (SUCCEEDED(StringCchPrintfW(command, ARRAYSIZE(command), L"\"%s\" %llu %lu \"%s\\ijl15-exit-%s.log\"",
            executable, static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(process)), GetCurrentProcessId(),
            ClientLog::Directory(), ClientLog::SessionId()))) {
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = attributes;
            PROCESS_INFORMATION child{};
            started = CreateProcessW(executable, command, nullptr, nullptr, TRUE,
                EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &child);
            error = GetLastError();
            if (started) {
                ClientLog::Emergency("exit_monitor_started monitorPid=%lu targetPid=%lu", child.dwProcessId, GetCurrentProcessId());
                CloseHandle(child.hThread);
                CloseHandle(child.hProcess);
            }
        } else error = ERROR_INSUFFICIENT_BUFFER;
    } else error = GetLastError();
    if (initialized) DeleteProcThreadAttributeList(attributes);
    if (attributes) HeapFree(GetProcessHeap(), 0, attributes);
    CloseHandle(process);
    if (!started) ClientLog::Emergency("exit_monitor_start_failed stage=create win32Error=%lu", error);
    return started != FALSE;
}
