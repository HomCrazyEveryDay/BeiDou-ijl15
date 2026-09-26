#pragma once
#include "DreamCanvas.h"
#include <algorithm>
#include <cstdio>
#include <climits>

// Display policy and IWzCanvas composition shared with standalone Canvas.dll tests.
namespace BuffCountdownCanvas {
using DreamCanvas::Method;
using DreamCanvas::Variant;
enum class Kind { None, Minutes, Seconds, Stacks };
struct Display {
    Kind kind = Kind::None;
    int value = 0;
    bool operator==(const Display& other) const { return kind == other.kind && value == other.value; }
    bool operator!=(const Display& other) const { return !(*this == other); }
};
inline Display TimeDisplay(long long remainingMs) {
    if (remainingMs <= 0) return {};
    if (remainingMs >= 60000) return {Kind::Minutes,
        static_cast<int>((std::min)(1 + (remainingMs - 1) / 60000, static_cast<long long>(INT_MAX)))};
    return {Kind::Seconds, static_cast<int>(1 + (remainingMs - 1) / 1000)};
}
inline Display Select(int remainingMs, bool noShadow, int stacks,
    int nativeType = 0, int iconId = 0, int shadowInterval = 0, long long couponRemaining = -2) {
    if (stacks > 0 && stacks <= 5) return {Kind::Stacks, stacks};
    // These cash coupons represent an active rate entitlement. Their native
    // buff timer is not the cash item's expiry time (Character.refreshCouponBuffs
    // and ItemConstants.isRateCoupon); displaying it produced "35790" minutes.
    const int absoluteIconId = iconId < 0 ? -iconId : iconId;
    const bool rateCoupon = nativeType == 1
        && (absoluteIconId / 1000 == 5211 || absoluteIconId / 1000 == 5360);
    // The server supplies the actual cash-item lifetime separately. Missing
    // metadata (-2) and permanent coupons (-1) keep the icon without fake time.
    if (rateCoupon) return noShadow ? Display{} : TimeDisplay(couponRemaining);
    // Skill entries retain original duration / 16 at +3C, even after +38 ticks
    // down. Do not depend on remaining time staying exactly INT_MAX.
    const bool indefinite = shadowInterval == INT_MAX / 16 || remainingMs == INT_MAX;
    if (noShadow || indefinite) return {};
    return TimeDisplay(remainingMs);
}
struct Sprite {
    void* canvas = nullptr;
    LONG width = 0, height = 0;
};
using Get = HRESULT(__stdcall*)(void*, LONG*);
using Put = HRESULT(__stdcall*)(void*, LONG);
using Copy = HRESULT(__stdcall*)(void*, int, int, void*, Variant);
using CopyEx = HRESULT(__stdcall*)(void*, int, int, void*, int, int, int, int, int, int, int, Variant);
inline const GUID& CanvasIID() {
    static const GUID iid = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    return iid;
}
inline Variant Integer(int value) { Variant v; v.type = 3; v.value = value; return v; }
struct CanvasOwner {
    void* value = nullptr;
    ~CanvasOwner() { DreamCanvas::Release(value); }
    CanvasOwner() = default;
    CanvasOwner(const CanvasOwner&) = delete;
    CanvasOwner& operator=(const CanvasOwner&) = delete;
    void* Detach() { void* result = value; value = nullptr; return result; }
};
inline bool ReadSprite(Sprite& sprite) {
    return sprite.canvas
        && SUCCEEDED(Method<Get>(sprite.canvas, 0x40)(sprite.canvas, &sprite.width))
        && SUCCEEDED(Method<Get>(sprite.canvas, 0x48)(sprite.canvas, &sprite.height))
        && sprite.width > 2 && sprite.width <= 32 && sprite.height > 0 && sprite.height <= 32;
}

// Always copy the clean resource. Never draw into shared WZ canvases or copy
// previously painted digits: different buffs and 10 -> 9 must stay independent.
inline void* Compose(void* shadow, const Display& display, const Sprite (&digits)[10], DreamCanvas::Factory factory) {
    if (!shadow || !factory || display.kind == Kind::None || display.value <= 0) return nullptr;
    LONG width = 0, height = 0, originX = 0, originY = 0;
    if (FAILED(Method<Get>(shadow, 0x40)(shadow, &width))
        || FAILED(Method<Get>(shadow, 0x48)(shadow, &height))
        || FAILED(Method<Get>(shadow, 0x6c)(shadow, &originX))
        || FAILED(Method<Get>(shadow, 0x74)(shadow, &originY))
        || width < 16 || width > 64 || height < 16 || height > 64) return nullptr;
    char text[16]{};
    std::snprintf(text, sizeof(text), "%d", display.value);
    int naturalWidth = 0, naturalHeight = 0;
    for (const char* c = text; *c; ++c) {
        const Sprite& digit = digits[*c - '0'];
        if (!digit.canvas || digit.width <= 2 || digit.height <= 0) return nullptr;
        // Share only the outermost outline column; a wider overlap erases narrow 1s.
        naturalWidth += digit.width - 1;
        naturalHeight = (std::max)(naturalHeight, static_cast<int>(digit.height));
    }
    naturalWidth += 1;
    // Keep the existing pixel font at its native size. Enlarging it to 15px
    // overwhelmed the 32px buff art and made the outline look uneven in-game.
    constexpr int margin = 2;
    int drawHeight = (std::min)(naturalHeight, static_cast<int>(height) - margin * 2);
    int drawWidth = MulDiv(naturalWidth, drawHeight, naturalHeight);
    if (drawWidth > width - margin * 2) {
        drawWidth = width - margin * 2;
        drawHeight = (std::max)(1, MulDiv(naturalHeight, drawWidth, naturalWidth));
    }
    const int x = display.kind == Kind::Seconds ? (width - drawWidth) / 2
        : display.kind == Kind::Stacks ? width - drawWidth - margin : margin;
    const int y = display.kind == Kind::Seconds ? (height - drawHeight) / 2 : height - drawHeight - margin;
    using Create = HRESULT(__stdcall*)(void*, int, int, Variant, Variant);
    using Rect = HRESULT(__stdcall*)(void*, int, int, int, int, unsigned);
    CanvasOwner number, scaled, target;
    // Canvas.dll DrawRectangle uses 0x00FFFFFF as its erase sentinel.
    // Color 0 blends zero alpha and leaves uninitialized pixels untouched.
    // Verified in Canvas.dll +9B8C/+9BE2; Create alone does not clear pixels.
    if (FAILED(factory(L"Canvas", &CanvasIID(), &number.value, nullptr)) || !number.value
        || FAILED(Method<Create>(number.value, 0x2c)(number.value, naturalWidth, naturalHeight, Integer(0), Integer(2)))
        || FAILED(Method<Rect>(number.value, 0x8c)(number.value, 0, 0, naturalWidth, naturalHeight, 0x00ffffff))) return nullptr;
    int cursor = 0;
    for (const char* c = text; *c; ++c) {
        const Sprite& digit = digits[*c - '0'];
        // Blend transparent outline margins at native resolution, then scale
        // the whole number so narrow digits keep consistent spacing.
        if (FAILED(Method<Copy>(number.value, 0x80)(number.value, cursor, 0, digit.canvas, Integer(255)))) return nullptr;
        cursor += digit.width - 1;
    }
    void* numberToDraw = number.value;
    if (drawWidth != naturalWidth || drawHeight != naturalHeight) {
        if (FAILED(factory(L"Canvas", &CanvasIID(), &scaled.value, nullptr)) || !scaled.value
            || FAILED(Method<Create>(scaled.value, 0x2c)(scaled.value, drawWidth, drawHeight, Integer(0), Integer(2)))
            || FAILED(Method<CopyEx>(scaled.value, 0x84)(scaled.value, 0, 0, number.value,
                -1, drawWidth, drawHeight, 0, 0, naturalWidth, naturalHeight, Variant{}))) return nullptr;
        numberToDraw = scaled.value;
    }
    if (FAILED(factory(L"Canvas", &CanvasIID(), &target.value, nullptr)) || !target.value
        || FAILED(Method<Create>(target.value, 0x2c)(target.value, width, height, Integer(0), Integer(2)))
        || FAILED(Method<Copy>(target.value, 0x80)(target.value, 0, 0, shadow, Variant{}))
        || FAILED(Method<Copy>(target.value, 0x80)(target.value, x, y, numberToDraw, Integer(255)))) return nullptr;
    if (FAILED(Method<Put>(target.value, 0x70)(target.value, originX))
        || FAILED(Method<Put>(target.value, 0x78)(target.value, originY))) return nullptr;
    return target.Detach();
}
}
