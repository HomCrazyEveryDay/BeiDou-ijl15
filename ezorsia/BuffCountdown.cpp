#include "stdafx.h"
#include "BuffCountdown.h"
#include "BuffCountdownCanvas.h"
#include "ClientLog.h"
#include <oleauto.h>
#include <cstring>
#include <unordered_map>
#pragma comment(lib, "oleaut32.lib")

namespace {
using namespace BuffCountdownCanvas;
constexpr DWORD kUpdateShadow = 0x007B44F4;
constexpr DWORD kInsertShadowCall = 0x007B4748;
constexpr DWORD kInsertCanvas = 0x00426BAB;
constexpr DWORD kResMan = 0x00BF14E8;
constexpr DWORD kFactory = 0x00BF0CC0;
constexpr int kBowExpert = 3120005;
// Verified in the shipped v83 executable: TEMPORARY_STAT +28 icon layer,
// +2C shadow layer, +30 shadow index, +34 no-shadow flag, +38 remaining ms.
// +3C is the shadow-frame interval, NOT the buff's total duration.
using UpdateShadow = void(__fastcall*)(void*, void*);
using InsertCanvas = void*(__fastcall*)(void*, void*, void*, void*, void*, void*, void*, void*, void*);
auto g_updateShadow = reinterpret_cast<UpdateShadow>(kUpdateShadow);
auto g_insertCanvas = reinterpret_cast<InsertCanvas>(kInsertCanvas);
volatile LONG* g_focusStacks = nullptr;
bool g_installed = false, g_log = false, g_failureLogged = false;
Sprite g_digits[10]{};
bool g_digitsReady = false;
DWORD g_lastDigitAttempt = 0;
bool g_digitAttempted = false;

struct CouponLifetime {
    long long remainingMs;
    ULONGLONG receivedAt;
};
std::unordered_map<int, CouponLifetime> g_couponTimes;

long long CouponRemaining(int iconId, ULONGLONG now) {
    if (iconId < 0) iconId = -iconId;
    const auto found = g_couponTimes.find(iconId);
    if (found == g_couponTimes.end()) return -2;
    if (found->second.remainingMs < 0) return -1;
    const ULONGLONG elapsed = now - found->second.receivedAt;
    return elapsed >= static_cast<ULONGLONG>(found->second.remainingMs)
        ? 0 : found->second.remainingMs - elapsed;
}

bool ReadCouponSnapshot(const unsigned char* data, unsigned size, ULONGLONG now) {
    if (!data || size < 2) return false;
    const unsigned count = data[0] | (data[1] << 8);
    if (count > 64 || size != 2 + count * 12) return false;
    std::unordered_map<int, CouponLifetime> next;
    for (unsigned i = 0; i < count; ++i) {
        int itemId = 0;
        long long remaining = 0;
        std::memcpy(&itemId, data + 2 + i * 12, 4);
        std::memcpy(&remaining, data + 6 + i * 12, 8);
        if ((itemId / 1000 != 5211 && itemId / 1000 != 5360)
            || (remaining <= 0 && remaining != -1) || next.count(itemId)) return false;
        next.emplace(itemId, CouponLifetime{remaining, now});
    }
    g_couponTimes.swap(next);
    return true;
}

struct Painted {
    void* layer = nullptr;
    Display display;
};
std::unordered_map<void*, Painted> g_painted;
struct PaintContext {
    void* layer = nullptr;
    Display display;
};
thread_local PaintContext g_paintContext;

void ReportFailure(const char* stage) {
    if (g_log && !g_failureLogged) {
        g_failureLogged = true;
        ClientLog::Append(ClientLog::Component::BuffIcons, "native_countdown fallback stage=%s", stage);
    }
}

void* LoadDigit(int digit) {
    void* resMan = *reinterpret_cast<void**>(kResMan);
    if (!resMan) return nullptr;
    wchar_t path[64]{};
    swprintf_s(path, L"UI/Basic.img/ItemNo/%d", digit);
    BSTR name = SysAllocString(path);
    if (!name) return nullptr;
    VARIANT missing{}, value{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    using GetObject = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
    const HRESULT hr = Method<GetObject>(resMan, 0x1c)(resMan, name, missing, missing, &value);
    SysFreeString(name);
    void* canvas = nullptr;
    IUnknown* object = value.vt == VT_UNKNOWN ? value.punkVal
        : value.vt == VT_DISPATCH ? value.pdispVal : nullptr;
    if (SUCCEEDED(hr) && object) object->QueryInterface(CanvasIID(), &canvas);
    VariantClear(&value);
    return canvas;
}

bool EnsureDigits() {
    if (g_digitsReady) return true;
    const DWORD now = GetTickCount();
    if (g_digitAttempted && now - g_lastDigitAttempt < 1000) return false;
    g_digitAttempted = true;
    g_lastDigitAttempt = now;
    for (int i = 0; i < 10; ++i) {
        if (!g_digits[i].canvas) g_digits[i].canvas = LoadDigit(i);
        if (!ReadSprite(g_digits[i])) {
            for (auto& digit : g_digits) { DreamCanvas::Release(digit.canvas); digit = {}; }
            ReportFailure("digit_resource");
            return false;
        }
    }
    g_digitsReady = true;
    return true;
}

// Only this call site in UpdateShadowIndex is redirected. Other InsertCanvas
// users (including the existing dream/incubation hooks) keep their behavior.
void* __fastcall InsertShadowWithCountdown(void* layer, void*, void* result, void* shadow,
    void* delay, void* alpha0, void* alpha1, void* zoom0, void* zoom1) {
    CanvasOwner composed;
    if (layer == g_paintContext.layer && g_paintContext.display.kind != Kind::None && EnsureDigits()) {
        auto factory = *reinterpret_cast<DreamCanvas::Factory*>(kFactory);
        composed.value = Compose(shadow, g_paintContext.display, g_digits, factory);
        if (!composed.value) ReportFailure("compose");
    }
    // The native layer owns the new canvas. Its position, alpha, visibility,
    // animation and lifetime are unchanged; no independent D3D drawing remains.
    return g_insertCanvas(layer, nullptr, result, composed.value ? composed.value : shadow,
        delay, alpha0, alpha1, zoom0, zoom1);
}

template<class T> T& Field(void* entry, unsigned offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(entry) + offset);
}

void __fastcall UpdateShadowWithCountdown(void* entry, void* edx) {
    const bool focus = Field<int>(entry, 0x1c) == 2 && Field<int>(entry, 0x20) == kBowExpert;
    const int stacks = focus && g_focusStacks ? InterlockedCompareExchange(g_focusStacks, 0, 0) : 0;
    const Display display = Select(Field<int>(entry, 0x38), Field<int>(entry, 0x34) != 0, stacks,
        Field<int>(entry, 0x1c), Field<int>(entry, 0x20), Field<int>(entry, 0x3c),
        CouponRemaining(Field<int>(entry, 0x20), GetTickCount64()));
    void* layer = Field<void*>(entry, 0x2c);
    const auto previous = g_painted.find(entry);
    if (layer && Field<int>(entry, 0x34) == 0
        && (previous == g_painted.end() || previous->second.layer != layer || previous->second.display != display)) {
        // Native UpdateShadowIndex skips equal shadow frames. A number change
        // (including removing a number) also requires a fresh, clean canvas.
        Field<int>(entry, 0x30) = -1;
    }
    struct RestoreContext {
        PaintContext previous;
        ~RestoreContext() { g_paintContext = previous; }
    } restore{g_paintContext};
    g_paintContext = {layer, display};
    g_updateShadow(entry, edx);
    // No COM references or native entries are owned here. Bound retired keys
    // within a field; new/reused entries still run their native initial draw.
    if (g_painted.size() >= 128 && g_painted.find(entry) == g_painted.end()) g_painted.clear();
    g_painted[entry] = {layer, display};
}

bool WriteCall(const BYTE (&bytes)[5]) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(kInsertShadowCall), 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    std::memcpy(reinterpret_cast<void*>(kInsertShadowCall), bytes, 5);
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(kInsertShadowCall), 5, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(kInsertShadowCall), 5);
    return true;
}
}

namespace BuffCountdown {
void Install(volatile LONG* focusStacks, bool enableLog) {
    g_focusStacks = focusStacks;
    g_log = enableLog;
    if (g_installed) return;
    const BYTE updatePrologue[] = {0xb8, 0x30, 0x84, 0xab, 0x00};
    const BYTE originalCall[] = {0xe8, 0x5e, 0x24, 0xc7, 0xff};
    if (std::memcmp(reinterpret_cast<void*>(kUpdateShadow), updatePrologue, sizeof(updatePrologue))
        || std::memcmp(reinterpret_cast<void*>(kInsertShadowCall), originalCall, sizeof(originalCall))) {
        ReportFailure("v83_signature");
        return;
    }
    BYTE patchedCall[5] = {0xe8};
    const DWORD displacement = reinterpret_cast<DWORD>(&InsertShadowWithCountdown) - (kInsertShadowCall + 5);
    std::memcpy(patchedCall + 1, &displacement, sizeof(displacement));
    if (!WriteCall(patchedCall)) { ReportFailure("insert_hook"); return; }
    if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_updateShadow), UpdateShadowWithCountdown)) {
        WriteCall(originalCall);
        ReportFailure("update_hook");
        return;
    }
    g_installed = true;
    if (g_log) ClientLog::Append(ClientLog::Component::BuffIcons,
        "native_countdown installed update=%08X insert=%08X digits=UI/Basic.img/ItemNo", kUpdateShadow, kInsertShadowCall);
}
void Reset() {
    g_painted.clear();
    g_couponTimes.clear();
    // Do not retain layer pointers across a field change. Digit resources are
    // immutable and cached independently of all buff entries and their layers.
    g_paintContext = {};
}
bool UpdateCoupons(const unsigned char* data, unsigned size) {
    return ReadCouponSnapshot(data, size, GetTickCount64());
}
}
