#pragma once
#include "DreamCanvas.h"
#include <cwchar>

namespace IncubationCanvas {
enum class Part { None, Background, Foreground };

inline bool Matches(const wchar_t* path, const wchar_t* root) {
    const size_t length = std::wcslen(root);
    // Effect_Screen may resolve the original animation group /0 first.
    return !std::wcsncmp(path, root, length)
        && (!path[length] || !std::wcscmp(path + length, L"/0"));
}

inline Part Identify(const wchar_t* path) {
    if (!path) return Part::None;
    if (Matches(path, L"Effect/Direction4.img/effect/incubation/back"))
        return Part::Background;
    if (Matches(path, L"Effect/Direction4.img/effect/incubation/babyDragon")
        || Matches(path, L"Effect/Direction4.img/effect/incubation/eggshell")
        || Matches(path, L"Effect/Direction4.img/effect/incubation/light"))
        return Part::Foreground;
    return Part::None;
}

// All cropped foreground frames share the original 800x600 scene coordinates.
// Use one scale for their dimensions AND signed origins, never fit each sprite.
inline int Coordinate(int value, int width, int height) {
    return width * 600 <= height * 800
        ? MulDiv(value, width, 800) : MulDiv(value, height, 600);
}

inline void* Scale(void* source, int width, int height, Part part, DreamCanvas::Factory create) {
    using namespace DreamCanvas;
    if (part == Part::None || !source || !create || width < 800 || height < 600
        || width > 7680 || height > 4320 || (width == 800 && height == 600)) return nullptr;
    // The plain backdrop fills the viewport; foreground figures retain aspect ratio.
    if (part == Part::Background) return DreamCanvas::Scale(source, width, height, create);
    using Get = HRESULT(__stdcall*)(void*, LONG*);
    LONG oldWidth = 0, oldHeight = 0, x = 0, y = 0;
    if (FAILED(Method<Get>(source, 0x40)(source, &oldWidth))
        || FAILED(Method<Get>(source, 0x48)(source, &oldHeight))
        || oldWidth <= 0 || oldWidth > 800 || oldHeight <= 0 || oldHeight > 600
        || FAILED(Method<Get>(source, 0x6c)(source, &x))
        || FAILED(Method<Get>(source, 0x74)(source, &y))) return nullptr;
    const int newWidth = Coordinate(oldWidth, width, height);
    const int newHeight = Coordinate(oldHeight, width, height);
    if (newWidth == oldWidth && newHeight == oldHeight) return nullptr;
    static const GUID iid = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void* target = nullptr;
    if (FAILED(create(L"Canvas", &iid, &target, nullptr)) || !target) return nullptr;
    Variant magnification, format;
    magnification.type = 3; format.type = 3; format.value = 2;
    using Create = HRESULT(__stdcall*)(void*, int, int, Variant, Variant);
    using Copy = HRESULT(__stdcall*)(void*, int, int, void*, int, int, int, int, int, int, int, Variant);
    using Put = HRESULT(__stdcall*)(void*, LONG);
    HRESULT hr = Method<Create>(target, 0x2c)(target, newWidth, newHeight, magnification, format);
    if (SUCCEEDED(hr)) hr = Method<Copy>(target, 0x84)(target, 0, 0, source, -1,
        newWidth, newHeight, 0, 0, oldWidth, oldHeight, Variant{});
    if (SUCCEEDED(hr)) hr = Method<Put>(target, 0x70)(target, Coordinate(x, width, height));
    if (SUCCEEDED(hr)) hr = Method<Put>(target, 0x78)(target, Coordinate(y, width, height));
    if (FAILED(hr)) { Release(target); return nullptr; }
    return target;
}
}
