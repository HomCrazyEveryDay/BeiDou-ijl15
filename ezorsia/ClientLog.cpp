#include "stdafx.h"
#include "ClientLog.h"
#include "ClientDiagnostics.h"

#include <cstdarg>
#include <strsafe.h>
#include <map>
#include <string>

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
            BuildTimestamp(reinterpret_cast<HMODULE>(&__ImageBase)), __DATE__, __TIME__,
            component == Component::Lifecycle ? -1LL : kMaximumLogBytes);
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
    HANDLE target = file;
    if (GetFileSizeEx(file, &size) && size.QuadPart + length > kMaximumLogBytes) {
        wchar_t path[1024]{};
        const DWORD count = GetFinalPathNameByHandleW(file, path, ARRAYSIZE(path), FILE_NAME_NORMALIZED);
        if (count > 0 && count < ARRAYSIZE(path)) {
            static std::map<std::wstring, unsigned> parts;
            const std::wstring base(path);
            unsigned& part = parts[base];
            if (part == 0) part = 1;
            for (;;) {
                const std::wstring next = base.substr(0, base.size() - 4) + L"-part" + std::to_wstring(part) + L".log";
                target = CreateFileW(next.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (target == INVALID_HANDLE_VALUE) break;
                if (!GetFileSizeEx(target, &size)) { CloseHandle(target); target = INVALID_HANDLE_VALUE; break; }
                if (size.QuadPart == 0) {
                    char header[256]{};
                    StringCchPrintfA(header, ARRAYSIZE(header),
                        "format=beidou-client-log-v1 session=%ls clientRunId=%s part=%u timestamps=UTC\r\n",
                        g_session, ClientDiagnostics::RunId(), part);
                    DWORD written = 0;
                    WriteFile(target, header, lstrlenA(header), &written, nullptr);
                    size.QuadPart += written;
                }
                if (size.QuadPart + length <= kMaximumLogBytes) break;
                CloseHandle(target);
                ++part;
            }
        } else target = INVALID_HANDLE_VALUE;
    }
    if (target != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(target, text, length, &written, nullptr);
    }
    if (target != file && target != INVALID_HANDLE_VALUE) CloseHandle(target);
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
    if (component == Component::Lifecycle && file != INVALID_HANDLE_VALUE) {
        // Disconnect evidence must continue after the ordinary diagnostic file cap.
        AcquireSRWLockExclusive(&g_writeLock);
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(lstrlenA(line)), &written, nullptr);
        ReleaseSRWLockExclusive(&g_writeLock);
    } else {
        Write(file, line);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SetLastError(savedError);
}

void ClientLog::Emergency(const char* format, ...)
{
    // Initialize() ran before diagnostic hooks were installed. Do not acquire
    // InitOnce, g_writeLock, or connection locks from a fault/exit callback.
    const DWORD savedError = GetLastError();
    wchar_t path[MAX_PATH]{};
    if (!g_directory[0] || FAILED(StringCchPrintfW(path, ARRAYSIZE(path),
        L"%s\\ijl15-emergency-%s.log", g_directory, g_session))) {
        SetLastError(savedError);
        return;
    }
    char message[1536]{};
    va_list args;
    va_start(args, format);
    StringCchVPrintfA(message, ARRAYSIZE(message), format, args);
    va_end(args);
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char line[1792]{};
    StringCchPrintfA(line, ARRAYSIZE(line),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), message);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(lstrlenA(line)), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
    SetLastError(savedError);
}
