#include "stdafx.h"
#include "ClientLog.h"
#include "ClientDiagnostics.h"

#include <cstdarg>
#include <strsafe.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
INIT_ONCE g_initialized = INIT_ONCE_STATIC_INIT;
SRWLOCK g_writeLock = SRWLOCK_INIT;
wchar_t g_directory[MAX_PATH]{};
wchar_t g_session[80]{};
constexpr LONGLONG kMaximumLogBytes = 8 * 1024 * 1024;
const wchar_t* const kNames[] = {
    L"startup", L"verify", L"launch-auth", L"trace", L"equipment", L"buff-icons", L"lifecycle"
};

bool EnsureDirectory(const wchar_t* path)
{
    if (CreateDirectoryW(path, nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool UseDirectory(const wchar_t* parent)
{
    wchar_t candidate[MAX_PATH]{};
    if (FAILED(StringCchPrintfW(candidate, ARRAYSIZE(candidate), L"%s\\logs", parent))
        || !EnsureDirectory(candidate)) return false;
    wchar_t path[MAX_PATH]{};
    if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\ijl15-lifecycle-%s.log", candidate, g_session)))
        return false;
    HANDLE probe = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe == INVALID_HANDLE_VALUE) return false;
    CloseHandle(probe);
    StringCchCopyW(g_directory, ARRAYSIZE(g_directory), candidate);
    return true;
}

BOOL CALLBACK InitializeOnce(PINIT_ONCE, PVOID, PVOID*)
{
    ClientDiagnostics::Initialize();
    FILETIME created{}, exited{}, kernel{}, user{};
    SYSTEMTIME started{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        FileTimeToSystemTime(&created, &started);
    else
        GetSystemTime(&started);
    StringCchPrintfW(g_session, ARRAYSIZE(g_session), L"%04u%02u%02u-%02u%02u%02u%03uZ-pid%lu",
        started.wYear, started.wMonth, started.wDay, started.wHour, started.wMinute,
        started.wSecond, started.wMilliseconds, GetCurrentProcessId());

    wchar_t parent[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, parent, ARRAYSIZE(parent));
    if (length > 0 && length < ARRAYSIZE(parent)) {
        wchar_t* slash = wcsrchr(parent, L'\\');
        if (slash) {
            *slash = L'\0';
            UseDirectory(parent);
        }
    }
    return TRUE;
}

DWORD BuildTimestamp(HMODULE module)
{
    if (!module) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}
}

void ClientLog::Initialize()
{
    InitOnceExecuteOnce(&g_initialized, InitializeOnce, nullptr, nullptr);
}

const wchar_t* ClientLog::Directory() { Initialize(); return g_directory; }
const wchar_t* ClientLog::SessionId() { Initialize(); return g_session; }

HANDLE ClientLog::Open(Component component)
{
    Initialize();
    const size_t index = static_cast<size_t>(component);
    if (!g_directory[0] || index >= ARRAYSIZE(kNames)) return INVALID_HANDLE_VALUE;
    wchar_t path[MAX_PATH]{};
    if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\ijl15-%s-%s.log",
        g_directory, kNames[index], g_session))) return INVALID_HANDLE_VALUE;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return file;
    AcquireSRWLockExclusive(&g_writeLock);
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart == 0) {
        char header[384]{};
        StringCchPrintfA(header, ARRAYSIZE(header),
            "format=beidou-client-log-v1 session=%ls clientRunId=%s pid=%lu exeBuild=0x%08lX dllBuild=0x%08lX compiled=%s %s timestamps=UTC maxBytes=%lld\r\n",
            g_session, ClientDiagnostics::RunId(), GetCurrentProcessId(), BuildTimestamp(GetModuleHandleW(nullptr)),
            BuildTimestamp(reinterpret_cast<HMODULE>(&__ImageBase)), __DATE__, __TIME__, kMaximumLogBytes);
        DWORD written = 0;
        WriteFile(file, header, lstrlenA(header), &written, nullptr);
    }
    ReleaseSRWLockExclusive(&g_writeLock);
    return file;
}

void ClientLog::Write(HANDLE file, const char* text)
{
    if (file == INVALID_HANDLE_VALUE || !text) return;
    AcquireSRWLockExclusive(&g_writeLock);
    LARGE_INTEGER size{};
    const DWORD length = static_cast<DWORD>(lstrlenA(text));
    if (GetFileSizeEx(file, &size) && size.QuadPart + length <= kMaximumLogBytes) {
        DWORD written = 0;
        WriteFile(file, text, length, &written, nullptr);
    }
    ReleaseSRWLockExclusive(&g_writeLock);
}

void ClientLog::Append(Component component, const char* format, ...)
{
    const DWORD savedError = GetLastError();
    char message[2048]{};
    va_list args;
    va_start(args, format);
    StringCchVPrintfA(message, ARRAYSIZE(message), format ? format : "", args);
    va_end(args);
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char line[2304]{};
    StringCchPrintfA(line, ARRAYSIZE(line), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId(), message);
    HANDLE file = Open(component);
    Write(file, line);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SetLastError(savedError);
}
