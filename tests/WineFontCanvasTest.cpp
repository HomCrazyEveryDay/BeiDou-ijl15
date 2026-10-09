#include "../ezorsia/WineCompatibility.cpp"
#include "../ezorsia/DreamCanvas.h"
#include <oleauto.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

void ClientLog::Append(Component, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::puts("");
}
static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL %s error=%lu\n", message, GetLastError()); std::exit(1); }
}
static void Success(HRESULT result, const char* message) {
    if (FAILED(result)) { std::fprintf(stderr, "FAIL %s HRESULT=%08lX\n", message, result); std::exit(1); }
}
template<class F> F Method(void* object, unsigned slot) {
    return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[slot / 4]);
}
int wmain(int argc, wchar_t** argv) {
    Check(argc >= 4 && argc <= 6, "arguments: client directory, baseline|patched, output PPM, optional family, optional runtime profile");
    const bool runtimeProfile = argc == 6 && wcscmp(argv[5], L"runtime") == 0;
    const bool smallUiProfile = argc == 6 && wcscmp(argv[5], L"small-ui") == 0;
    const bool chatTargetProfile = argc == 6 && wcscmp(argv[5], L"chat-target") == 0;
    const bool patched = wcscmp(argv[2], L"patched") == 0;
    if (patched) {
        Check(WineCompatibility::IsWine(), "patched run requires Wine");
        Check(WineCompatibility::Install(), "install production Wine hooks");
    }
    Check(SetDllDirectoryW(argv[1]) != FALSE, "client DLL directory");
    const HMODULE pcom = LoadLibraryW(L"PCOM.dll");
    Check(pcom != nullptr, "load original PCOM");
    auto init = reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pcom, "PcInitModule"));
    auto factory = reinterpret_cast<DreamCanvas::Factory>(GetProcAddress(pcom, "PcCreateObject"));
    Check(init && factory, "PCOM exports");
    Success(init(), "PCOM init");
    const GUID fontId = {0x2bef046d,0xccd6,0x445a,{0x88,0xc4,0x92,0x9f,0xc3,0x5d,0x30,0xac}};
    const GUID canvasId = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void* canvas = nullptr;
    Success(factory(L"Canvas", &canvasId, &canvas, nullptr), "create native canvas");
    VARIANT zero{}, format{};
    zero.vt = VT_I4; format.vt = VT_I4; format.lVal = smallUiProfile ? 1 : 2;
    constexpr int width = 560, height = 160;
    using CreateCanvas = HRESULT(__stdcall*)(void*, int, int, VARIANT, VARIANT);
    Success(Method<CreateCanvas>(canvas, 0x2c)(canvas, width, height, zero, format), "canvas storage");
    const wchar_t* rows[] = {
        L"[\x6d3b\x52a8\x70b9\x6570] \x62fe\x53d6 +100, \x4eca\x65e5 1,000/600,000",
        L"\x795e\x7684\x5192\x9669  \x795e\x5c04\x624b  \x4efb\x52a1\x5f00\x59cb  \x83b7\x5f97\x7ecf\x9a8c\x52a0\x6210",
        L"\x88c5\x5907\x7cbe\x7075\x5760\x540e 1 \x5c0f\x65f6\x5185\xff0c\x60a8\x5c06\x83b7\x5f97 10% \x989d\x5916\x7ecf\x9a8c\x52a0\x6210\x3002"
    };
    using FontCreate = HRESULT(__stdcall*)(void*, BSTR, LONG, ULONG, VARIANT);
    using Draw = HRESULT(__stdcall*)(void*, int, int, BSTR, void*, VARIANT, VARIANT, LONG*);
    int y = 8;
    for (int row = 0; row < 6; ++row) {
        void* font = nullptr;
        Success(factory(L"Canvas#Font", &fontId, &font, nullptr), "native font");
        VARIANT style{}; style.vt = VT_BSTR;
        const wchar_t* runtimeStyles[] = {L"b",L"ba",L"",L"ba",L"b",L""};
        const int runtimeSizes[] = {12,12,12,14,15,11};
        const int smallSizes[] = {12,12,11,11,10,9};
        style.bstrVal = SysAllocString(chatTargetProfile ? L"" : smallUiProfile ? (row % 2 ? L"B" : L"BA")
            : runtimeProfile ? runtimeStyles[row] : row == 1 || row == 4 ? L"bn" : L"n");
        BSTR face = SysAllocString(argc >= 5 ? argv[4] : L"SimSun");
        const int size = chatTargetProfile ? (row % 2 ? 12 : 11)
            : smallUiProfile ? smallSizes[row] : runtimeProfile ? runtimeSizes[row] : row < 3 ? 12 : 14;
        const ULONG colors[] = {0xff88ddff, 0xffffffff, 0xffffff66};
        Success(Method<FontCreate>(font, 0x0c)(font, face, size, chatTargetProfile ? 0xffffffff : colors[row % 3], style), "font Create");
        SysFreeString(face); VariantClear(&style);
        BSTR text = SysAllocString(chatTargetProfile ? L"\u5bf9\u6240\u6709\u4eba" : smallUiProfile
            ? L"\u7ae5\u68a6\u65b0\u5fc6  \u5bf9\u6240\u6709\u4eba  \u91d1\u94f6\u5c9b  \u5c04\u624b\u8bad\u7ec3\u573a I"
            : rows[row % 3]);
        VARIANT empty{}; LONG fontHeight = 0;
        Success(Method<Draw>(font, 0x30)(font, 8, y, text, canvas, empty, empty, &fontHeight), "native font draw");
        Check(fontHeight == size, "line height must not change");
        std::printf("row=%d height=%ld\n", row, fontHeight);
        SysFreeString(text); DreamCanvas::Release(font);
        y += 24;
    }
    FILE* file = nullptr;
    Check(_wfopen_s(&file, argv[3], L"wb") == 0, "open preview");
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    unsigned partial = 0, opaque = 0, pixels = 0;
    using Pixel = HRESULT(__stdcall*)(void*, int, int, unsigned*);
    for (int yPos = 0; yPos < height; ++yPos) for (int x = 0; x < width; ++x) {
        unsigned pixel = 0;
        Success(Method<Pixel>(canvas, 0x88)(canvas, x, yPos, &pixel), "read canvas pixel");
        const unsigned alpha = pixel >> 24;
        if (alpha) ++pixels;
        if (alpha && alpha < 255) ++partial;
        if (alpha == 255) ++opaque;
        unsigned char rgb[3];
        const unsigned background[3] = {25, 35, 40};
        for (int channel = 0; channel < 3; ++channel)
            rgb[channel] = static_cast<unsigned char>((((pixel >> (16 - channel * 8)) & 255) * alpha
                + background[channel] * (255 - alpha)) / 255);
        std::fwrite(rgb, 1, 3, file);
    }
    std::fclose(file); DreamCanvas::Release(canvas);
    std::printf("%s pixels=%u partialAlpha=%u opaque=%u ACP=%u\n", patched ? "patched" : "baseline",
        pixels, partial, opaque, GetACP());
    Check(pixels > 100, "Chinese and numeric text rendered");
    if (patched && smoothFonts) Check(partial > 100, "native Canvas text must contain grayscale edges");
    if (patched && !smoothFonts) Check(opaque > 100, "Windows font profile preserves native solid strokes");
    return 0;
}
