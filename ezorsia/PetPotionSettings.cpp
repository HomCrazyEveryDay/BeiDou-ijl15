#include "stdafx.h"
#include "PetPotionSettings.h"
#include "ClientLog.h"
#include <cstring>

namespace {
constexpr wchar_t kSettingsKey[] = L"Software\\BeiDou\\MapleStory\\PetPotion";
constexpr wchar_t kSettingsValue[] = L"AlertStepsV1";
constexpr DWORD kVersion = 0x01000000;
constexpr int kDefaultStep = 10; // Native steps are 5%; use 50% on first migration.
struct Alerts { int hp = kDefaultStep, mp = kDefaultStep; };
enum class ReadResult { Valid, Missing, Invalid, Unavailable };
BYTE* installedClient = nullptr;
using LoadGlobal = void(__thiscall*)(void*);
LoadGlobal originalLoad = nullptr;
DWORD lastSaved = 0;
bool haveSaved = false, persistenceAvailable = true;
unsigned logCount = 0;

struct PreserveLastError {
    DWORD value = GetLastError();
    ~PreserveLastError() { SetLastError(value); }
};
bool ValidStep(int step) { return step >= 0 && step <= 19; }
DWORD Encode(Alerts alerts) {
    return kVersion | static_cast<DWORD>(alerts.hp) | (static_cast<DWORD>(alerts.mp) << 8);
}
void Log(const char* phase, const char* source, Alerts alerts, LSTATUS status) {
    if (logCount++ < 32) ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=pet_potion_settings phase=%s source=%s hpStep=%d mpStep=%d status=%ld",
        phase, source, alerts.hp, alerts.mp, status);
}
ReadResult ReadSettings(Alerts& alerts, LSTATUS& status) {
    HKEY key = nullptr;
    status = RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return ReadResult::Missing;
    if (status != ERROR_SUCCESS) return ReadResult::Unavailable;
    DWORD type = 0, size = sizeof(DWORD), value = 0;
    status = RegQueryValueExW(key, kSettingsValue, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    if (status == ERROR_FILE_NOT_FOUND) return ReadResult::Missing;
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) return ReadResult::Unavailable;
    if (status != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(DWORD)
        || (value & 0xffff0000) != kVersion || !ValidStep(value & 0xff)
        || !ValidStep((value >> 8) & 0xff)) return ReadResult::Invalid;
    alerts = {static_cast<int>(value & 0xff), static_cast<int>((value >> 8) & 0xff)};
    return ReadResult::Valid;
}
int LegacyStep(void* config, const char* name) {
    // Reuse the exact handle/view opened by the native client. Never write it.
    const HKEY key = *reinterpret_cast<HKEY*>(static_cast<BYTE*>(config) + 0xc0);
    if (!key) return kDefaultStep;
    DWORD type = 0, size = sizeof(DWORD), value = 0;
    const LSTATUS result = RegQueryValueExA(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size);
    // Only the one-time legacy import repairs zero. An explicit zero saved
    // in BeiDou's own setting remains disabled on all subsequent launches.
    if (result != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(DWORD)
        || value == 0 || value > 19) return kDefaultStep;
    return static_cast<int>(value);
}
void Persist(Alerts alerts) {
    if (!persistenceAvailable) return;
    const DWORD value = Encode(alerts);
    if (haveSaved && value == lastSaved) return;
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status == ERROR_SUCCESS) {
        // One versioned DWORD commits both thresholds together; no partial
        // HP/MP migration or separate migration marker can be left behind.
        status = RegSetValueExW(key, kSettingsValue, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
    if (status == ERROR_SUCCESS) { lastSaved = value; haveSaved = true; }
    Log("save", "beidou", alerts, status);
}
void LoadSettings(void* config) {
    PreserveLastError preserve;
    Alerts alerts;
    LSTATUS status = ERROR_SUCCESS;
    const ReadResult result = ReadSettings(alerts, status);
    haveSaved = result == ReadResult::Valid;
    persistenceAvailable = result != ReadResult::Unavailable;
    const char* source = "beidou";
    if (result == ReadResult::Missing) {
        alerts = {LegacyStep(config, "soHPFlash"), LegacyStep(config, "soMPFlash")};
        source = "legacy_or_default";
    } else if (result != ReadResult::Valid) {
        // A corrupt/unreadable private setting must not re-import another
        // server's shared values. Keep usable defaults in this process.
        source = "private_unavailable_or_invalid";
    }
    if (haveSaved) lastSaved = Encode(alerts);
    *reinterpret_cast<int*>(static_cast<BYTE*>(config) + 0x80) = alerts.hp;
    *reinterpret_cast<int*>(static_cast<BYTE*>(config) + 0x84) = alerts.mp;
    Log("load", source, alerts, status);
    Persist(alerts);
}
void __fastcall LoadGlobalHook(void* config, void*) {
    originalLoad(config);
    // Native LoadGlobal returns early when HKLM cannot be opened. Our HKCU
    // values/defaults still need to initialize CConfig in that case.
    LoadSettings(config);
}
void __fastcall SkipLegacyAlertWrite(void*, void*, int, const char*, int) {
    // Only the two HP/MP calls in SaveGlobal are redirected here. Other
    // original settings keep their native persistence and behavior.
}
}

void PetPotionSettings::Save(void* config) {
    if (!installedClient || !config) return;
    PreserveLastError preserve;
    Alerts alerts{*reinterpret_cast<int*>(static_cast<BYTE*>(config) + 0x80),
        *reinterpret_cast<int*>(static_cast<BYTE*>(config) + 0x84)};
    if (ValidStep(alerts.hp) && ValidStep(alerts.mp)) Persist(alerts);
}

bool PetPotionSettings::Install(HMODULE client) {
    auto base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return installedClient == base;
    if (!base) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const DWORD sites[] = {0x9c2ff, 0x9cbe2, 0x9cc23};
    const BYTE expected[][5] = {
        {0xe8,0x3d,0x01,0,0}, {0xe8,0xce,0x23,0,0}, {0xe8,0x8d,0x23,0,0}
    };
    for (unsigned i = 0; i < 3; ++i)
        if (std::memcmp(base + sites[i], expected[i], 5)) return false;
    // All three sites are on one page. Validate and acquire before modifying any.
    constexpr SIZE_T span = 0x9cc28 - 0x9c2ff;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(base + sites[0], span, PAGE_EXECUTE_READWRITE, &protection)) return false;
    originalLoad = reinterpret_cast<LoadGlobal>(base + 0x9c441);
    void* targets[] = {reinterpret_cast<void*>(LoadGlobalHook),
        reinterpret_cast<void*>(SkipLegacyAlertWrite), reinterpret_cast<void*>(SkipLegacyAlertWrite)};
    for (unsigned i = 0; i < 3; ++i) {
        const DWORD relative = reinterpret_cast<DWORD>(targets[i]) - reinterpret_cast<DWORD>(base + sites[i]) - 5;
        std::memcpy(base + sites[i] + 1, &relative, 4);
    }
    const bool ok = FlushInstructionCache(GetCurrentProcess(), base + sites[0], span) != FALSE;
    if (!ok) {
        for (unsigned i = 0; i < 3; ++i) std::memcpy(base + sites[i], expected[i], 5);
        FlushInstructionCache(GetCurrentProcess(), base + sites[0], span);
    }
    VirtualProtect(base + sites[0], span, protection, &ignored);
    if (ok) installedClient = base;
    return ok;
}
