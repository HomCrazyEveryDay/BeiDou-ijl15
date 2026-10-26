#include "stdafx.h"
#include "ModuleAudit.h"
#include "ModuleAuditCrypto.h"
#include "ClientLog.h"
#include "ClientDiagnostics.h"
#include "../out/module-audit/Key.h"
#include <tlhelp32.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <intrin.h>
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "version.lib")

namespace {
constexpr unsigned kQueueSize = 512;
constexpr unsigned kPathChars = 1024;
constexpr DWORD kFileLimit = 4 * 1024 * 1024;
constexpr size_t kRetainedFiles = 16;
constexpr size_t kMetadataLimit = 2048;
struct NativeString { USHORT length, maximum; const wchar_t* buffer; };
// Loaded and unloaded notification structures have the same layout.
struct NativeNotification {
    ULONG flags;
    const NativeString* fullName;
    const NativeString* baseName;
    void* base;
    ULONG size;
};
using RegisterNotification = LONG (NTAPI*)(ULONG, void (CALLBACK*)(ULONG, const NativeNotification*, void*), void*, void**);
using UnregisterNotification = LONG (NTAPI*)(void*);
struct Event {
    ULONG reason;
    ULONG_PTR base;
    ULONG size;
    bool truncated;
    wchar_t path[kPathChars];
};
Event g_queue[kQueueSize];
volatile LONG g_write = 0, g_read = 0, g_dropped = 0, g_started = 0;
HANDLE g_stop = nullptr, g_thread = nullptr;
void* g_cookie = nullptr;
UnregisterNotification g_unregister = nullptr;
DWORD g_snapshotSeconds = 10;
#ifdef MODULE_AUDIT_TEST
volatile LONG g_failSnapshots = 0;
#endif

// Loader-lock callback: no Win32/CRT calls, allocation, locks, hashing or I/O.
// Windows serializes loader notifications. Only this producer writes each slot;
// the worker releases it after copying. Interlocked operations are x86 intrinsics.
void CALLBACK OnNotification(ULONG reason, const NativeNotification* data, void*) {
    if ((reason != 1 && reason != 2) || !data) return;
    const LONG write = _InterlockedCompareExchange(&g_write, 0, 0);
    const LONG next = (write + 1) % kQueueSize;
    if (next == _InterlockedCompareExchange(&g_read, 0, 0)) {
        _InterlockedIncrement(&g_dropped);
        return;
    }
    Event& event = g_queue[write];
    event.reason = reason; event.base = reinterpret_cast<ULONG_PTR>(data->base); event.size = data->size;
    const NativeString* name = data->fullName ? data->fullName : data->baseName;
    unsigned length = name && name->buffer ? name->length / sizeof(wchar_t) : 0;
    event.truncated = length >= kPathChars;
    if (length >= kPathChars) length = kPathChars - 1;
    // Volatile prevents the optimizer from replacing this with an imported memcpy.
    volatile wchar_t* destination = event.path;
    for (unsigned i = 0; i < length; ++i) destination[i] = name->buffer[i];
    destination[length] = 0;
    _InterlockedExchange(&g_write, next);
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string text(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &text[0], length, nullptr, nullptr);
    return text;
}
std::string Json(const std::string& value) {
    std::string text = "\"";
    const char digits[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { text += '\\'; text += c; }
        else if (c < 32) { text += "\\u00"; text += digits[c >> 4]; text += digits[c & 15]; }
        else text += c;
    }
    return text + '"';
}
std::string Json(const std::wstring& value) { return Json(Utf8(value)); }
std::string Hex(const unsigned char* bytes, size_t count) {
    const char digits[] = "0123456789abcdef";
    std::string value;
    for (size_t i = 0; i < count; ++i) { value += digits[bytes[i] >> 4]; value += digits[bytes[i] & 15]; }
    return value;
}
std::string Address(ULONG_PTR value) {
    std::ostringstream text; text << "0x" << std::hex << value; return text.str();
}
std::string Timestamp() {
    SYSTEMTIME now{}; GetSystemTime(&now);
    char text[40];
    sprintf_s(text, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return text;
}
bool Under(const std::wstring& path, const std::wstring& directory) {
    return !directory.empty() && path.size() > directory.size()
        && _wcsnicmp(path.c_str(), directory.c_str(), directory.size()) == 0
        && path[directory.size()] == L'\\';
}
ULONGLONG Stamp(FILETIME value) { return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};
struct Module { ULONG_PTR base; DWORD size; std::wstring path; bool truncated; };
using Modules = std::map<ULONG_PTR, Module>;

bool Snapshot(Modules& modules, DWORD& error) {
#ifdef MODULE_AUDIT_TEST
    if (InterlockedCompareExchange(&g_failSnapshots, 0, 0)) { error = ERROR_BAD_LENGTH; return false; }
#endif
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 5; ++attempt) {
        raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (raw != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    Handle snapshot(raw);
    if (raw == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Module32FirstW(raw, &entry)) { error = GetLastError(); return false; }
    do {
        const ULONG_PTR base = reinterpret_cast<ULONG_PTR>(entry.modBaseAddr);
        modules.emplace(base, Module{base, entry.modBaseSize, entry.szExePath, wcslen(entry.szExePath) >= MAX_PATH - 1});
        if (modules.size() > 2048) { error = ERROR_BUFFER_OVERFLOW; return false; }
    } while (Module32NextW(raw, &entry));
    error = GetLastError();
    return error == ERROR_NO_MORE_FILES;
}

void Prune(const std::wstring& directory) {
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW((directory + L"\\ijl15-modules-*.bda").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return;
    std::vector<std::pair<ULONGLONG, std::wstring>> files;
    do {
        if (!(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            files.emplace_back(Stamp(data.ftLastWriteTime), directory + L"\\" + data.cFileName);
    } while (FindNextFileW(search, &data));
    FindClose(search);
    std::sort(files.begin(), files.end());
    // Live writers deny FILE_SHARE_DELETE and are therefore never removed.
    size_t remaining = files.size();
    for (const auto& file : files) {
        if (remaining <= kRetainedFiles) break;
        if (DeleteFileW(file.second.c_str())) --remaining;
    }
}

struct Metadata { std::wstring path; std::string reference; ULONGLONG size, modified; };
class Audit {
    ModuleAuditCrypto::Writer writer_;
    std::wstring directory_, gameDirectory_, windowsDirectory_;
    std::deque<Metadata> pending_;
    std::set<std::string> scheduled_;
    Modules previous_;
    unsigned part_ = 0;
    unsigned long long record_ = 0;
    bool baseline_ = false;
    bool rebaseline_ = false;
    bool scanFailed_ = false;
    ULONGLONG lastSnapshot_ = 0;
    size_t lastPending_ = 0;
    unsigned unchangedScans_ = 0;
    std::string lastConnection_;
    ULONG_PTR exe_ = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
    void OpenPart() {
        const std::wstring path = directory_ + L"\\ijl15-modules-" + ClientLog::SessionId()
            + L"-part" + std::to_wstring(++part_) + L".bda";
        writer_.Open(path);
        if (part_ > 1) rebaseline_ = true;
        lastConnection_.clear();
        writer_.Append("{\"event\":\"session\",\"observedAtUtc\":" + Json(Timestamp())
            + ",\"clientRunId\":" + Json(std::string(ClientDiagnostics::RunId()))
            + ",\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"part\":" + std::to_string(part_)
            + ",\"notifications\":" + (g_cookie ? "true" : "false")
            + ",\"snapshotSeconds\":" + std::to_string(g_snapshotSeconds)
            + ",\"scope\":\"loaded_modules_only\",\"signatureScope\":\"embedded_offline\"}");
        Prune(directory_);
    }
    std::string Origin(const std::wstring& path) const {
        if (Under(path, gameDirectory_)) return "game_directory";
        if (Under(path, windowsDirectory_)) return "windows_directory";
        return "other";
    }
public:
    void Initialize() {
        directory_ = ClientLog::Directory();
        if (directory_.empty()) throw std::runtime_error("module audit directory unavailable");
        wchar_t path[32768];
        const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (length && length < ARRAYSIZE(path)) {
            gameDirectory_.assign(path, length);
            gameDirectory_ = gameDirectory_.substr(0, gameDirectory_.find_last_of(L"\\"));
        }
        const UINT count = GetWindowsDirectoryW(path, ARRAYSIZE(path));
        if (count && count < ARRAYSIZE(path)) windowsDirectory_.assign(path, count);
        writer_.Initialize(ModuleAuditKey::Secret, ModuleAuditKey::Id);
        OpenPart();
    }
    void Write(const std::string& event, const std::string& fields = {}) {
        if (writer_.Bytes() + fields.size() + 512 > kFileLimit) OpenPart();
        writer_.Append("{\"event\":" + Json(event) + ",\"record\":" + std::to_string(++record_)
            + ",\"observedAtUtc\":" + Json(Timestamp()) + fields + "}");
    }
    void Record(const char* event, const Module& module, bool inspectFile) {
        const size_t slash = module.path.find_last_of(L"\\/");
        std::string fields = ",\"base\":" + Json(Address(module.base)) + ",\"imageSize\":" + std::to_string(module.size)
            + ",\"name\":" + Json(module.path.substr(slash == std::wstring::npos ? 0 : slash + 1))
            + ",\"path\":" + Json(module.path) + ",\"origin\":" + Json(Origin(module.path))
            + ",\"isExe\":" + (module.base == exe_ ? "true" : "false")
            + ",\"pathTruncated\":" + (module.truncated ? "true" : "false");
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (inspectFile && !module.truncated && !module.path.empty()
            && GetFileAttributesExW(module.path.c_str(), GetFileExInfoStandard, &attributes)) {
            const ULONGLONG size = (static_cast<ULONGLONG>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
            const ULONGLONG modified = Stamp(attributes.ftLastWriteTime);
            const std::string reference = Utf8(module.path) + "|" + std::to_string(size) + "|" + std::to_string(modified);
            fields += ",\"fileRef\":" + Json(reference);
            if (scheduled_.count(reference) == 0) {
                if (scheduled_.size() < kMetadataLimit && pending_.size() < 256) {
                    scheduled_.insert(reference);
                    pending_.push_back(Metadata{module.path, reference, size, modified});
                } else fields += ",\"metadataStatus\":\"capacity_limit\"";
            }
        } else if (inspectFile) fields += ",\"metadataStatus\":\"path_unavailable\"";
        Write(event, fields);
    }
    void Drain() {
        bool recorded = false;
        // Bound work even if notifications arrive continuously.
        for (unsigned count = 0; count < kQueueSize; ++count) {
            const LONG read = _InterlockedCompareExchange(&g_read, 0, 0);
            if (read == _InterlockedCompareExchange(&g_write, 0, 0)) break;
            const Event event = g_queue[read];
            _InterlockedExchange(&g_read, (read + 1) % kQueueSize);
            Record(event.reason == 1 ? "load_notification" : "unload_notification",
                Module{event.base, event.size, event.path, event.truncated}, event.reason == 1);
            recorded = true;
        }
        const LONG dropped = _InterlockedExchange(&g_dropped, 0);
        if (dropped) Write("coverage_gap", ",\"reason\":\"notification_queue_overflow\",\"dropped\":"
            + std::to_string(static_cast<unsigned long>(dropped)));
        // Preserve the old WriteFile visibility for crash-adjacent module changes.
        // Compression batches the drained notifications; disk flush still runs every 5s.
        if (recorded || dropped) writer_.Commit();
    }
    void Scan() {
        Modules current; DWORD error = 0;
        if (!Snapshot(current, error)) {
            scanFailed_ = true;
            Write("snapshot_failed", ",\"win32Error\":" + std::to_string(error));
            writer_.Commit();
            return; // An incomplete scan must never synthesize mass unloads.
        }
        if (rebaseline_) {
            // Renew the inventory and metadata after rotation, so old retained
            // parts can expire without permanently losing long-lived modules.
            previous_.clear(); scheduled_.clear(); pending_.clear();
            baseline_ = false; rebaseline_ = false;
        }
        bool changed = !baseline_ || scanFailed_;
        size_t dlls = 0, other = 0;
        for (const auto& item : current) {
            const Module& module = item.second;
            if (module.base != exe_) { ++dlls; if (Origin(module.path) == "other") ++other; }
            const auto old = previous_.find(item.first);
            if (old == previous_.end() || old->second.path != module.path || old->second.size != module.size) {
                changed = true;
                Record(baseline_ ? "snapshot_added" : "initial_module", module, true);
            }
        }
        for (const auto& item : previous_) {
            const auto now = current.find(item.first);
            if (now == current.end() || now->second.path != item.second.path || now->second.size != item.second.size) {
                changed = true;
                Record("snapshot_removed", item.second, false);
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (changed || pending_.size() != lastPending_ || now - lastSnapshot_ >= 300000) {
            Write("snapshot", ",\"moduleCount\":" + std::to_string(current.size()) + ",\"dllCount\":" + std::to_string(dlls)
                + ",\"outsideGameAndWindows\":" + std::to_string(other)
                + ",\"metadataPending\":" + std::to_string(pending_.size())
                + ",\"unchangedScans\":" + std::to_string(unchangedScans_) + ",\"complete\":true");
            lastSnapshot_ = now; lastPending_ = pending_.size(); unchangedScans_ = 0;
            writer_.Commit();
        } else ++unchangedScans_;
        scanFailed_ = false;
        previous_ = std::move(current); baseline_ = true;
    }
    void Connection() {
        ClientDiagnostics::Snapshot snapshot;
        if (ClientDiagnostics::TrySnapshot(snapshot)) {
            const std::string value = std::string(snapshot.connectionId) + "/" + std::to_string(snapshot.diagnosticAttempt)
                + "/" + ClientDiagnostics::StateName(snapshot.connectionState);
            if (value != lastConnection_) {
                Write("connection", ",\"connectionId\":" + Json(std::string(snapshot.connectionId))
                    + ",\"state\":" + Json(std::string(ClientDiagnostics::StateName(snapshot.connectionState)))
                    + ",\"attempt\":" + std::to_string(snapshot.diagnosticAttempt));
                lastConnection_ = value;
                writer_.Commit();
            }
        }
    }
    void InspectOne() {
        if (pending_.empty()) return;
        const Metadata item = pending_.front(); pending_.pop_front();
        std::string fields = ",\"fileRef\":" + Json(item.reference) + ",\"path\":" + Json(item.path);
        // Inspect the disk file, not live image pages. Deny writes/deletes during inspection.
        Handle file(CreateFileW(item.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (file.value == INVALID_HANDLE_VALUE) {
            Write("file_metadata", fields + ",\"status\":\"open_failed\",\"win32Error\":" + std::to_string(GetLastError())); return;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.value, &info)
            || ((static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != item.size
            || Stamp(info.ftLastWriteTime) != item.modified) {
            Write("file_metadata", fields + ",\"status\":\"changed_before_read\""); return;
        }
        fields += ",\"fileSize\":" + std::to_string(item.size) + ",\"fileLastWriteTime\":" + std::to_string(item.modified);
        if (item.size > 256ULL * 1024 * 1024) {
            Write("file_metadata", fields + ",\"status\":\"size_limit\""); return;
        }
        BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status >= 0) status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
        unsigned char buffer[64 * 1024], digest[32];
        DWORD read = 0; ULONGLONG total = 0; const ULONGLONG started = GetTickCount64();
        bool readable = status >= 0;
        while (readable) {
            if (!ReadFile(file.value, buffer, sizeof(buffer), &read, nullptr)) { readable = false; break; }
            if (!read) break;
            total += read;
            if (BCryptHashData(hash, buffer, read, 0) < 0 || GetTickCount64() - started > 2000) { readable = false; break; }
        }
        readable = readable && total == item.size && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if (!readable) { Write("file_metadata", fields + ",\"status\":\"hash_failed_or_time_limit\""); return; }
        fields += ",\"sha256\":" + Json(Hex(digest, sizeof(digest))) + ",\"status\":\"ok\"";

        DWORD ignored = 0; const DWORD versionSize = GetFileVersionInfoSizeW(item.path.c_str(), &ignored);
        if (versionSize && versionSize <= 1024 * 1024) {
            std::vector<unsigned char> version(versionSize);
            VS_FIXEDFILEINFO* fixed = nullptr; UINT size = 0;
            if (GetFileVersionInfoW(item.path.c_str(), 0, versionSize, version.data())
                && VerQueryValueW(version.data(), L"\\", reinterpret_cast<void**>(&fixed), &size)
                && size >= sizeof(*fixed) && fixed->dwSignature == 0xFEEF04BD) {
                fields += ",\"version\":" + Json(std::to_string(HIWORD(fixed->dwFileVersionMS)) + "."
                    + std::to_string(LOWORD(fixed->dwFileVersionMS)) + "." + std::to_string(HIWORD(fixed->dwFileVersionLS))
                    + "." + std::to_string(LOWORD(fixed->dwFileVersionLS)));
            }
        }
        WINTRUST_FILE_INFO trustFile{}; trustFile.cbStruct = sizeof(trustFile);
        trustFile.pcwszFilePath = item.path.c_str(); trustFile.hFile = file.value;
        WINTRUST_DATA trust{}; trust.cbStruct = sizeof(trust); trust.dwUIChoice = WTD_UI_NONE;
        trust.dwUnionChoice = WTD_CHOICE_FILE; trust.pFile = &trustFile; trust.dwStateAction = WTD_STATEACTION_VERIFY;
        trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
        GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const LONG result = WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &trust);
        fields += ",\"signatureStatus\":" + Json(Address(static_cast<ULONG>(result)))
            + ",\"embeddedSignatureValidOffline\":" + (result == ERROR_SUCCESS ? "true" : "false");
        CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
        CRYPT_PROVIDER_SGNR* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain && signer->pasCertChain[0].pCert) {
            wchar_t publisher[512]{};
            if (CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, publisher, ARRAYSIZE(publisher)) > 1)
                fields += ",\"publisher\":" + Json(std::wstring(publisher));
        }
        trust.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &trust);
        Write("file_metadata", fields);
    }
    void Flush() { writer_.Flush(); }
};

void Unregister() {
    if (g_cookie && g_unregister) { g_unregister(g_cookie); g_cookie = nullptr; }
}
DWORD WINAPI Worker(void*) {
    try {
        Audit audit; audit.Initialize();
        audit.Scan(); audit.Connection(); audit.Flush();
        ULONGLONG scanned = GetTickCount64(), flushed = scanned;
        while (WaitForSingleObject(g_stop, 250) == WAIT_TIMEOUT) {
            audit.Drain(); audit.Connection();
            if (GetTickCount64() - scanned >= g_snapshotSeconds * 1000ULL) { audit.Scan(); scanned = GetTickCount64(); }
            audit.InspectOne();
            if (GetTickCount64() - flushed >= 5000) { audit.Flush(); flushed = GetTickCount64(); }
        }
        Unregister(); audit.Drain(); audit.Write("stopped"); audit.Flush();
    } catch (...) {
        Unregister();
        // Only operational status enters the ordinary log, never module names/paths.
        ClientLog::Emergency("module_audit_failed recordingStopped=1 plaintextFallback=0");
    }
    return 0;
}
}

void ModuleAudit::Start() {
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0) return;
    wchar_t setting[16]{};
    if (GetEnvironmentVariableW(L"BEIDOU_MODULE_AUDIT", setting, ARRAYSIZE(setting)) == 1 && setting[0] == L'0') {
        ClientLog::Emergency("module_audit_config enabled=0"); return;
    }
    const DWORD length = GetEnvironmentVariableW(L"BEIDOU_MODULE_AUDIT_SECONDS", setting, ARRAYSIZE(setting));
    if (length && length < ARRAYSIZE(setting)) {
        wchar_t* end = nullptr; const unsigned long seconds = wcstoul(setting, &end, 10);
        if (end != setting && !*end && seconds >= 5 && seconds <= 300) g_snapshotSeconds = seconds;
    }
#ifndef MODULE_AUDIT_TEST
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&ModuleAudit::Start), &pinned)) {
        ClientLog::Emergency("module_audit_failed stage=pin"); return;
    }
#endif
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stop) { ClientLog::Emergency("module_audit_failed stage=event"); return; }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto registration = reinterpret_cast<RegisterNotification>(GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    g_unregister = reinterpret_cast<UnregisterNotification>(GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
    if (registration && g_unregister && registration(0, OnNotification, nullptr, &g_cookie) < 0) g_cookie = nullptr;
    const bool notifications = g_cookie != nullptr;
    g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!g_thread) { Unregister(); CloseHandle(g_stop); g_stop = nullptr; }
    else SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
    ClientLog::Emergency("module_audit_config enabled=1 started=%d notifications=%d snapshotSec=%lu encrypted=AES256GCM maxFileMiB=4 retainedFiles=16",
        g_thread != nullptr, notifications, g_snapshotSeconds);
}

#ifdef MODULE_AUDIT_TEST
void ModuleAudit::StopForTest() {
    if (g_thread) { SetEvent(g_stop); WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); CloseHandle(g_stop); g_thread = nullptr; }
}
void ModuleAudit::FailSnapshotsForTest(bool fail) { InterlockedExchange(&g_failSnapshots, fail ? 1 : 0); }
void ModuleAudit::ExerciseBoundsForTest() {
    Audit audit; audit.Initialize();
    std::wstring path(1100, L'x');
    NativeString name{static_cast<USHORT>(path.size() * 2), static_cast<USHORT>(path.size() * 2), path.c_str()};
    NativeNotification data{0, &name, &name, reinterpret_cast<void*>(0x12340000), 4096};
    for (unsigned i = 0; i < kQueueSize + 17; ++i) OnNotification(1, &data, nullptr);
    audit.Drain();
    // Unique random bytes keep the rotation test meaningful with compression enabled.
    for (unsigned i = 0; i < 1200; ++i) {
        unsigned char random[4096];
        ModuleAuditCrypto::Check(BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        audit.Write("stress", ",\"padding\":" + Json(Hex(random, sizeof(random))));
    }
    audit.Scan(); audit.Flush();
}
#endif
