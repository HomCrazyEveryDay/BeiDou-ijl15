#define NOMINMAX
#include <windows.h>
#include <oleauto.h>
#include <gdiplus.h>
#include "../ezorsia/BuffCountdownCanvas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "gdiplus.lib")
namespace ClientLog {
enum class Component { BuffIcons };
void Append(Component, const char*, ...) {}
}
#include "NativeCountdownUnderTest.h"

void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
using Rect = HRESULT(__stdcall*)(void*, int, int, int, int, unsigned);
using Pixel = HRESULT(__stdcall*)(void*, int, int, unsigned*);
DreamCanvas::Factory factory = nullptr;
void* NewCanvas(int width, int height) {
    void* result = nullptr;
    Check(SUCCEEDED(factory(L"Canvas", &CanvasIID(), &result, nullptr)) && result, "create canvas object");
    using Create = HRESULT(__stdcall*)(void*, int, int, Variant, Variant);
    Check(SUCCEEDED(Method<Create>(result, 0x2c)(result, width, height, Integer(0), Integer(2))), "initialize canvas pixels");
    Check(SUCCEEDED(Method<Rect>(result, 0x8c)(result, 0, 0, width, height, 0x00ffffff)), "clear newly created canvas");
    return result;
}
unsigned ReadPixel(void* canvas, int x, int y) {
    unsigned result = 0;
    Check(SUCCEEDED(Method<Pixel>(canvas, 0x88)(canvas, x, y, &result)), "read pixel");
    return result;
}
std::vector<unsigned> Pixels(void* canvas) {
    std::vector<unsigned> result;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) result.push_back(ReadPixel(canvas, x, y));
    return result;
}
void* LoadPng(const wchar_t* path) {
    Gdiplus::Bitmap bitmap(path);
    Check(bitmap.GetLastStatus() == Gdiplus::Ok, "read exported digit PNG");
    void* result = NewCanvas(bitmap.GetWidth(), bitmap.GetHeight());
    for (unsigned y = 0; y < bitmap.GetHeight(); ++y)
        for (unsigned x = 0; x < bitmap.GetWidth(); ++x) {
            Gdiplus::Color color;
            Check(bitmap.GetPixel(x, y, &color) == Gdiplus::Ok, "decode PNG pixel");
            Check(SUCCEEDED(Method<Rect>(result, 0x8c)(result, x, y, 1, 1, color.GetValue())), "load PNG into WZ canvas");
        }
    return result;
}
struct FakeLayer { void* canvas = nullptr; int inserts = 0; };
void* cleanShadow = nullptr;
void* __fastcall Insert(void* self, void*, void* result, void* canvas, void*, void*, void*, void*, void*) {
    auto& layer = *static_cast<FakeLayer*>(self);
    DreamCanvas::Release(layer.canvas);
    layer.canvas = canvas;
    Method<ULONG(__stdcall*)(void*)>(canvas, 4)(canvas);
    ++layer.inserts;
    return result;
}
void __fastcall Update(void* entry, void*) {
    if (Field<int>(entry, 0x34)) return;
    const int interval = Field<int>(entry, 0x3c);
    const int index = interval ? (std::max)(0, (std::min)(15, Field<int>(entry, 0x38) / interval)) : 0;
    if (Field<int>(entry, 0x30) == index) return;
    Variant result, delay = Integer(500), alpha0 = Integer(210), alpha1 = Integer(64), zoom;
    InsertShadowWithCountdown(Field<void*>(entry, 0x2c), nullptr, &result, cleanShadow,
        &delay, &alpha0, &alpha1, &zoom, &zoom);
    Field<int>(entry, 0x30) = index;
}
void SavePreview(const wchar_t* output) {
    const Display examples[] = {{Kind::Minutes, 12}, {Kind::Minutes, 1}, {Kind::Seconds, 59},
        {Kind::Seconds, 10}, {Kind::Seconds, 9}, {Kind::Stacks, 5}};
    Gdiplus::Bitmap strip(6 * 36, 36, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&strip);
    graphics.Clear(Gdiplus::Color(255, 35, 41, 53));
    for (int i = 0; i < 6; ++i) {
        CanvasOwner canvas; canvas.value = Compose(cleanShadow, examples[i], g_digits, factory);
        Check(canvas.value != nullptr, "preview composed");
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const unsigned p = ReadPixel(canvas.value, x, y);
                // Put the transparent shadow/number canvas over a neutral icon tile.
                const unsigned a = p >> 24;
                const unsigned r = (((p >> 16) & 255) * a + 85 * (255 - a)) / 255;
                const unsigned g = (((p >> 8) & 255) * a + 115 * (255 - a)) / 255;
                const unsigned b = ((p & 255) * a + 145 * (255 - a)) / 255;
                strip.SetPixel(i * 36 + x + 2, y + 2, Gdiplus::Color(255, r, g, b));
            }
    }
    const CLSID png = {0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Check(strip.Save(output, &png) == Gdiplus::Ok, "save offline preview");
}
int wmain(int argc, wchar_t** argv) {
    Check(argc == 4, "arguments: client directory, digit directory, preview PNG");
    SetDllDirectoryW(argv[1]);
    auto module = LoadLibraryW(L"PCOM.dll");
    Check(module != nullptr, "load client PCOM.dll");
    using Init = HRESULT(__cdecl*)();
    Check(SUCCEEDED(reinterpret_cast<Init>(GetProcAddress(module, "PcInitModule"))()), "initialize PCOM");
    factory = reinterpret_cast<DreamCanvas::Factory>(GetProcAddress(module, "PcCreateObject"));
    Check(factory != nullptr, "find canvas factory");
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR gdiplus = 0;
    Check(Gdiplus::GdiplusStartup(&gdiplus, &input, nullptr) == Gdiplus::Ok, "initialize PNG decoder");
    for (int i = 0; i < 10; ++i) {
        wchar_t path[MAX_PATH]{};
        swprintf_s(path, L"%ls/ItemNo_%d.png", argv[2], i);
        g_digits[i].canvas = LoadPng(path);
        Check(ReadSprite(g_digits[i]), "native digit dimensions");
    }
    g_digitsReady = true;
    cleanShadow = NewCanvas(32, 32);
    Check(SUCCEEDED(Method<Rect>(cleanShadow, 0x8c)(cleanShadow, 0, 0, 16, 32, 0x80000000)), "create alpha shadow");
    const auto sourcePixels = Pixels(cleanShadow);
    Check(Select(60000, false, 0) == Display{Kind::Minutes, 1}, "60 seconds is minute mode");
    Check(Select(59999, false, 0) == Display{Kind::Seconds, 60}, "below a minute uses seconds");
    Check(Select(59000, false, 0) == Display{Kind::Seconds, 59}, "second boundary");
    Check(Select(3600001, false, 0) == Display{Kind::Minutes, 61}, "long finite buff");
    Check(Select(0, false, 0).kind == Kind::None && Select(-1, false, 0).kind == Kind::None, "expired and sentinel duration");
    Check(Select(60000, true, 0).kind == Kind::None, "permanent buff has no timer");
    Check(Select(86400000, false, 5) == Display{Kind::Stacks, 5}, "focus is a count, not minutes");
    Check(Select(2147400000, false, 0, 1, 5211000).kind == Kind::None,
        "EXP coupon never displays its placeholder as 35790 minutes");
    Check(Select(2147400000, false, 0, 1, -5360000).kind == Kind::None,
        "drop coupon never displays its placeholder as 35790 minutes");
    Check(Select(3600000, false, 0, 1, 5211000).kind == Kind::None,
        "rate entitlement does not acquire a misleading timer after elapsed time");
    Check(Select(INT_MAX, false, 0, 1, 5211000, INT_MAX / 16, 3600000)
        == Display{Kind::Minutes, 60}, "actual coupon expiry replaces native placeholder");
    Check(Select(INT_MAX, false, 0, 1, 5360000, INT_MAX / 16, 30LL * 86400000)
        == Display{Kind::Minutes, 43200}, "30-day coupon does not overflow the native int timer");
    unsigned char coupons[26]{};
    coupons[0] = 2;
    int expId = 5211000, dropId = 5360000;
    long long expTime = 3600000, permanent = -1;
    std::memcpy(coupons + 2, &expId, 4);
    std::memcpy(coupons + 6, &expTime, 8);
    std::memcpy(coupons + 14, &dropId, 4);
    std::memcpy(coupons + 18, &permanent, 8);
    Check(ReadCouponSnapshot(coupons, sizeof(coupons), 100), "accept complete coupon snapshot");
    Check(CouponRemaining(expId, 1100) == 3599000 && CouponRemaining(dropId, 1100) == -1,
        "timed and permanent coupon clocks are independent");
    Check(CouponRemaining(-expId, 3600100) == 0, "coupon expiration clamps at zero");
    Check(!ReadCouponSnapshot(coupons, sizeof(coupons) - 1, 0)
        && CouponRemaining(dropId, 1100) == -1, "malformed snapshot cannot replace valid state");
    std::memcpy(coupons + 14, &expId, 4);
    Check(!ReadCouponSnapshot(coupons, sizeof(coupons), 0), "duplicate coupon ids are rejected");
    std::memcpy(coupons + 14, &dropId, 4);
    unsigned char emptyCoupons[2]{};
    Check(ReadCouponSnapshot(emptyCoupons, 2, 0) && CouponRemaining(expId, 100) == -2,
        "removing coupons clears countdown metadata");
    Check(Select(120000, false, 0, 1, 2022450) == Display{Kind::Minutes, 2},
        "ordinary timed EXP consumables keep their real countdown");
    Check(Select(120000, false, 0, 2, 5211000) == Display{Kind::Minutes, 2},
        "item category filtering never applies to skill ids");
    Check(Select(INT_MAX, false, 0).kind == Kind::None
        && Select(INT_MAX - 3600000, false, 0, 2, 1001003, INT_MAX / 16).kind == Kind::None,
        "everlasting skill sentinel stays hidden after the remaining timer ticks down");

    *reinterpret_cast<DreamCanvas::Factory*>(kFactory) = factory;
    volatile LONG focus = 0;
    g_focusStacks = &focus;
    g_updateShadow = Update;
    g_insertCanvas = Insert;
    DWORD entry[16]{};
    FakeLayer layer;
    Field<void*>(entry, 0x2c) = &layer;
    Field<int>(entry, 0x3c) = 60000; // Shadow frame remains zero while seconds change.
    Field<int>(entry, 0x38) = 10000;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.inserts == 1, "initial countdown is attached to native layer");
    const auto ten = Pixels(layer.canvas);
    int leftWhite = 0;
    for (int y = 8; y < 24; ++y)
        for (int x = 8; x < 14; ++x)
            if ((ReadPixel(layer.canvas, x, y) & 0x00ffffff) == 0x00ffffff) ++leftWhite;
    Check(leftWhite > 0, "the leading 1 is not erased by the following 0");
    CanvasOwner transparent; transparent.value = NewCanvas(32, 32);
    CanvasOwner digitMask; digitMask.value = Compose(transparent.value, {Kind::Seconds, 10}, g_digits, factory);
    int clearPixels = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if ((ReadPixel(digitMask.value, x, y) >> 24) == 0) {
                ++clearPixels;
                Check(ReadPixel(layer.canvas, x, y) == ReadPixel(cleanShadow, x, y),
                    "transparent digit margins preserve the underlying shadow");
            }
    Check(clearPixels > 700, "digits keep most of the icon unobscured");

    Field<int>(entry, 0x38) = 9500;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.inserts == 1, "same second and same shadow do not rebuild");
    Field<int>(entry, 0x38) = 9000;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.inserts == 2, "second change refreshes even with unchanged shadow index");
    CanvasOwner nine; nine.value = Compose(cleanShadow, {Kind::Seconds, 9}, g_digits, factory);
    Check(Pixels(layer.canvas) == Pixels(nine.value) && ten != Pixels(nine.value), "10 to 9 leaves no old digit pixels");
    Check(Pixels(cleanShadow) == sourcePixels, "shared shadow resource is unmodified");
    Field<int>(entry, 0x38) = 0;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.canvas == cleanShadow && Pixels(layer.canvas) == sourcePixels, "expiration removes numbers");
    Field<int>(entry, 0x1c) = 1;
    Field<int>(entry, 0x20) = 5211000;
    Field<int>(entry, 0x38) = 2147400000;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.canvas == cleanShadow, "coupon hook passes through the untouched native shadow");
    Check(BuffCountdown::UpdateCoupons(coupons, sizeof(coupons)), "load actual coupon lifetime");
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.canvas != cleanShadow, "finite coupon gets countdown on its native layer");
    BuffCountdown::Reset();
    Check(g_couponTimes.empty(), "field reset drops the prior character's coupon metadata");
    Field<int>(entry, 0x38) -= 60000;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.canvas == cleanShadow, "coupon timer stays absent after field cache reset");
    Field<int>(entry, 0x20) = 2001001;
    Field<int>(entry, 0x38) = 120000;
    UpdateShadowWithCountdown(entry, nullptr);
    Field<int>(entry, 0x38) = 60000;
    UpdateShadowWithCountdown(entry, nullptr);
    const auto minute = Pixels(layer.canvas);
    Field<int>(entry, 0x38) = 59000;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(minute != Pixels(layer.canvas), "minute to seconds changes placement");
    const int inserts = layer.inserts;
    Field<int>(entry, 0x30) = -1;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(layer.inserts == inserts + 1, "native shadow invalidation still refreshes");
    Field<int>(entry, 0x1c) = 2;
    Field<int>(entry, 0x20) = kBowExpert;
    Field<int>(entry, 0x38) = 86400000;
    focus = 1;
    UpdateShadowWithCountdown(entry, nullptr);
    const auto oneStack = Pixels(layer.canvas);
    focus = 5;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(oneStack != Pixels(layer.canvas), "focus stack updates refresh native number");
    BuffCountdown::Reset();
    Check(g_painted.empty() && !g_paintContext.layer, "field transition retains no layer references");
    // A reused address with a new layer must paint even if display text is identical.
    FakeLayer secondLayer;
    Field<void*>(entry, 0x2c) = &secondLayer;
    UpdateShadowWithCountdown(entry, nullptr);
    Check(secondLayer.inserts == 1, "new native layer receives its own canvas");
    Check(Pixels(cleanShadow) == sourcePixels, "multiple buffs never mutate shared WZ pixels");
    LONG origin = 0;
    Method<Put>(cleanShadow, 0x70)(cleanShadow, -3);
    CanvasOwner shifted; shifted.value = Compose(cleanShadow, {Kind::Minutes, 12}, g_digits, factory);
    Check(shifted.value && SUCCEEDED(Method<Get>(shifted.value, 0x6c)(shifted.value, &origin))
        && origin == -3, "signed canvas origin preserved");
    Method<Put>(cleanShadow, 0x70)(cleanShadow, 0);
    SavePreview(argv[3]);
    DreamCanvas::Release(layer.canvas);
    DreamCanvas::Release(secondLayer.canvas);
    std::puts("PASS: real Canvas.dll composition, alpha/source isolation, clean 10->9 refresh, minute/second boundary, focus stacks, native refresh and field lifecycle");
}
