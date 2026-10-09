#include "../ezorsia/WineCompatibility.cpp"
#include "../ezorsia/D3D8DisplayModeHook.h"
#include <cstdio>
#include <cstdlib>

int Client::m_nGameWidth = 1280;
int Client::m_nGameHeight = 720;
bool Client::enableStartupLog = false;
void ClientLog::Append(Component, const char*, ...) {}

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
static DWORD adapterResult = ERROR_NOT_SUPPORTED;
static DWORD WINAPI MockAdapters(AdapterInfo*, PULONG size) {
    if (size && adapterResult == ERROR_BUFFER_OVERFLOW) *size = 0x4000;
    return adapterResult;
}
static BYTE observedCharset;
static BYTE observedQuality;
static LONG observedHeight, observedWeight;
static char observedFace[LF_FACESIZE];
static HFONT WINAPI CaptureFont(const LOGFONTA* font) {
    observedCharset = font->lfCharSet;
    observedQuality = font->lfQuality;
    observedHeight = font->lfHeight;
    observedWeight = font->lfWeight;
    strcpy_s(observedFace, font->lfFaceName);
    return reinterpret_cast<HFONT>(1);
}
struct NativeNameStub {
    void* __thiscall Construct(const char* text) {
        Require(strcmp(text, "SimSun") == 0, "nameplate constructs only the verified face");
        ++calls;
        return this;
    }
    unsigned calls = 0;
};
static void* __fastcall ConstructNameStub(void* self, void*, const char* text) {
    return static_cast<NativeNameStub*>(self)->Construct(text);
}

int main() {
    static_assert(sizeof(void*) == 4, "test uses the client x86 ABI");
    std::printf("Environment: Wine=%d ACP=%u\n", WineCompatibility::IsWine(), GetACP());
    Require(GetCPInfo(936, &chineseCodePageInfo) != FALSE, "GBK code page exists");
    wchar_t wide[8]{};
    char bytes[16]{};
    Require(ConvertToWide(CP_ACP, 0, "\xD6\xD0\xCE\xC4", 4, wide, 8) == 2
        && wide[0] == 0x4E2D && wide[1] == 0x6587, "ANSI GBK text decodes to Chinese");
    Require(ConvertFromWide(CP_THREAD_ACP, 0, wide, 2, bytes, 16, nullptr, nullptr) == 4
        && memcmp(bytes, "\xD6\xD0\xCE\xC4", 4) == 0, "Chinese text round trips to GBK");
    Require(ConvertToWide(CP_UTF8, MB_ERR_INVALID_CHARS, "\xE4\xB8\xAD", 3, wide, 8) == 1
        && wide[0] == 0x4E2D, "explicit UTF-8 settings and logs stay UTF-8");
    Require(ConvertToWide(1252, 0, "\xE9", 1, wide, 8) == 1 && wide[0] == 0xE9,
        "explicit legacy code page is preserved");
    Require(ChineseLeadByte(0xD6) && !ChineseLeadByte('A')
        && ChineseLeadByteEx(CP_ACP, 0xD6), "Chinese cursor uses double byte boundaries");
    auto realFont = originalCreateFontIndirectA;
    originalCreateFontIndirectA = CaptureFont;
    LOGFONTA font{}; font.lfCharSet = DEFAULT_CHARSET;
    forceChineseCharset = true;
    ChineseFontA(&font);
    Require(observedCharset == GB2312_CHARSET && font.lfCharSet == DEFAULT_CHARSET,
        "default font charset is localized without mutating caller data");
    forceChineseCharset = false;
    strcpy_s(font.lfFaceName, "Arial");
    ChineseFontA(&font);
    Require(observedCharset == DEFAULT_CHARSET && strcmp(observedFace, "Arial") == 0,
        "Chinese prefix preserves default Arial matching instead of forcing a CJK-only face");
    font.lfCharSet = SYMBOL_CHARSET; ChineseFontA(&font);
    Require(observedCharset == SYMBOL_CHARSET, "explicit symbol font is preserved");
    font.lfCharSet = GB2312_CHARSET; font.lfQuality = NONANTIALIASED_QUALITY;
    font.lfHeight = -12; font.lfWeight = FW_BOLD;
    ChineseFontA(&font);
    Require(observedCharset == GB2312_CHARSET && observedQuality == ANTIALIASED_QUALITY
        && observedHeight == -12 && observedWeight == FW_BOLD
        && font.lfQuality == NONANTIALIASED_QUALITY,
        "small text receives grayscale smoothing without resizing, restyling or mutating the request");
    font.lfCharSet = SYMBOL_CHARSET; ChineseFontA(&font);
    Require(observedQuality == NONANTIALIASED_QUALITY, "symbol bitmap remains unchanged");
    smoothFonts = false;
    font.lfCharSet = GB2312_CHARSET; ChineseFontA(&font);
    Require(observedQuality == NONANTIALIASED_QUALITY && observedWeight == FW_BOLD,
        "Windows font profile preserves native pixel glyph quality and weight");
    strcpy_s(font.lfFaceName, "Arial Narrow");
    ChineseFontA(&font);
    Require(strcmp(observedFace, "SimSun") == 0 && strcmp(font.lfFaceName, "Arial Narrow") == 0,
        "Windows profile reproduces Arial Narrow fallback without mutating caller");
    strcpy_s(font.lfFaceName, "Tahoma"); ChineseFontA(&font);
    Require(strcmp(observedFace, "Tahoma") == 0, "native Tahoma family is retained");
    strcpy_s(font.lfFaceName, "\xB5\xB8\xBF\xF2"); ChineseFontA(&font);
    Require(strcmp(observedFace, "SimSun") == 0 && observedWeight == FW_BOLD,
        "the exact legacy nameplate face uses Windows fallback without changing weight");
    strcpy_s(font.lfFaceName, "Arial"); ChineseFontA(&font);
    Require(strcmp(observedFace, "Arial") == 0, "normal Arial and chat are unchanged");
    font.lfQuality = ANTIALIASED_QUALITY;
    for (const char* face : {"Arial", "Tahoma", "SimSun"}) {
        strcpy_s(font.lfFaceName, face);
        for (LONG height : {-11L, -12L}) {
            font.lfHeight = height;
            ChineseFontA(&font);
            Require(observedQuality == NONANTIALIASED_QUALITY && observedWeight == FW_BOLD
                && observedHeight == height && strcmp(observedFace, face) == 0,
                "observed small bold label requests retain family, metrics and weight with bitmap glyphs");
        }
    }
    font.lfWeight = FW_NORMAL; ChineseFontA(&font);
    Require(observedQuality == ANTIALIASED_QUALITY, "normal role/chat text is not changed by the bold rule");
    font.lfWeight = FW_BOLD; font.lfHeight = -14; ChineseFontA(&font);
    Require(observedQuality == ANTIALIASED_QUALITY, "larger text keeps its requested quality");
    font.lfHeight = -12; strcpy_s(font.lfFaceName, "Microsoft YaHei UI"); ChineseFontA(&font);
    Require(observedQuality == ANTIALIASED_QUALITY, "other font families keep their requested quality");
    strcpy_s(font.lfFaceName, "Arial"); font.lfQuality = DEFAULT_QUALITY; ChineseFontA(&font);
    Require(observedQuality == DEFAULT_QUALITY, "default quality text remains unchanged");
    auto realConstructor = constructNameplateFace;
    constructNameplateFace = reinterpret_cast<NativeBstrConstructor>(ConstructNameStub);
    NativeNameStub output;
    Require(GuildFontFace(nullptr, nullptr, &output, 5527) == &output && output.calls == 1,
        "scoped guild face forwards caller output to the native ownership path");
    constructNameplateFace = realConstructor;
    smoothFonts = true;
    font.lfCharSet = GB2312_CHARSET; font.lfQuality = CLEARTYPE_QUALITY; ChineseFontA(&font);
    Require(observedQuality == CLEARTYPE_QUALITY, "explicit existing smoothing remains unchanged");
    originalCreateFontIndirectA = realFont;

    originalGetAdaptersInfo = MockAdapters;
    Require(BuildStableAddress(), "Wine host identity is available");
    BYTE firstAddress[6]; memcpy(firstAddress, stableAddress, sizeof(firstAddress));
    Require(BuildStableAddress() && memcmp(firstAddress, stableAddress, 6) == 0
        && (stableAddress[0] & 3) == 2, "fallback MAC is stable, local and unicast");
    struct { AdapterInfo adapter; BYTE guard[16]; } buffer;
    memset(&buffer, 0xCC, sizeof(buffer));
    ULONG size = sizeof(buffer.adapter);
    Require(ReadAdapters(&buffer.adapter, &size) == ERROR_SUCCESS
        && buffer.adapter.next == nullptr && buffer.adapter.type == 6
        && buffer.adapter.addressLength == 6 && size == sizeof(AdapterInfo),
        "failed enumeration returns a traversable single adapter");
    for (BYTE value : buffer.guard) Require(value == 0xCC, "adapter write respects buffer boundary");
    memset(&buffer, 0xCC, sizeof(buffer)); size = sizeof(AdapterInfo) - 1;
    Require(ReadAdapters(&buffer.adapter, &size) == ERROR_NOT_SUPPORTED
        && reinterpret_cast<BYTE*>(&buffer)[0] == 0xCC, "short buffer remains untouched");
    adapterResult = ERROR_BUFFER_OVERFLOW; size = 0;
    Require(ReadAdapters(nullptr, &size) == ERROR_BUFFER_OVERFLOW && size == 0x4000,
        "API size query contract is preserved");
    size = sizeof(AdapterInfo);
    Require(ReadAdapters(&buffer.adapter, &size) == ERROR_SUCCESS && size == sizeof(AdapterInfo),
        "client fixed buffer is repaired using original capacity");
    adapterResult = ERROR_SUCCESS; size = sizeof(AdapterInfo); buffer.adapter.index = 42;
    Require(ReadAdapters(&buffer.adapter, &size) == ERROR_SUCCESS && buffer.adapter.index == 42,
        "successful enumeration is unchanged");

    Require(WineCompatibility::Install(), "Wine hooks install as one transaction");
    if (WineCompatibility::IsWine()) {
        Require(GetACP() == 936, "installed code page hook is active");
        LOGFONTW request{};
        request.lfCharSet = GB2312_CHARSET;
        request.lfHeight = -12;
        request.lfWeight = FW_NORMAL;
        request.lfQuality = NONANTIALIASED_QUALITY;
        wcscpy_s(request.lfFaceName, L"SimSun");
        HFONT installedFont = CreateFontIndirectW(&request);
        LOGFONTW actual{};
        Require(installedFont && GetObjectW(installedFont, sizeof(actual), &actual) == sizeof(actual)
            && actual.lfQuality == (smoothFonts ? ANTIALIASED_QUALITY : NONANTIALIASED_QUALITY)
            && actual.lfHeight == -12,
            "real wide font hook applies selected quality even when Wine ACP already equals 936");
        DeleteObject(installedFont);
        // Replay the real x86 CALL and its two stack arguments in an isolated page.
        auto nameplatePage = static_cast<BYTE*>(VirtualAlloc(nullptr,
            0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(nameplatePage != nullptr, "isolated nameplate call page");
        memset(nameplatePage, 0x90, 0x10000);
        const BYTE loadOutput[] = {0x8B,0x44,0x24,0x04};
        memcpy(nameplatePage + 0xFC0, loadOutput, sizeof(loadOutput));
        const BYTE stringIndex[] = {0x68,0x97,0x15,0x00,0x00};
        memcpy(nameplatePage + 0xFD1, stringIndex, sizeof(stringIndex));
        nameplatePage[0xFD6] = 0x50; // push output
        const BYTE nativeCall[] = {0xE8,0xAB,0x52,0xE1,0xFF};
        memcpy(nameplatePage + 0xFE2, nativeCall, sizeof(nativeCall));
        nameplatePage[0xFE7] = 0xC3;
        const BYTE chatFontSelection[] = {0x6A,0x01,0x59,0x6A,0x22,0x58,0xC3};
        memcpy(nameplatePage + 0x2000, chatFontSelection, sizeof(chatFontSelection));
        const bool previousSmoothing = smoothFonts;
        smoothFonts = false;
        auto previousConstructor = constructNameplateFace;
        constructNameplateFace = reinterpret_cast<NativeBstrConstructor>(ConstructNameStub);
        nameplatePage[0xFD2] ^= 1;
        Require(!InstallNameplateCall(nameplatePage + 0xFE2, nameplatePage + 0xFD1),
            "wrong native font index rejects patch");
        Require(memcmp(nameplatePage + 0xFE2, nativeCall, sizeof(nativeCall)) == 0,
            "rejected patch leaves call intact");
        nameplatePage[0xFD2] ^= 1;
        DWORD previousProtection;
        Require(VirtualProtect(nameplatePage, 0x10000, PAGE_EXECUTE_READ, &previousProtection) != FALSE,
            "protect isolated call as executable code");
        Require(InstallNameplateCall(nameplatePage + 0xFE2, nameplatePage + 0xFD1),
            "install scoped nameplate call");
        NativeNameStub nativeOutput;
        auto replay = reinterpret_cast<void*(__cdecl*)(void*)>(nameplatePage + 0xFC0);
        for (unsigned i = 0; i < 100; ++i)
            Require(replay(&nativeOutput) == &nativeOutput, "patched call preserves output and stack cleanup");
        Require(nativeOutput.calls == 100, "exactly one native ownership construction per call");
        MEMORY_BASIC_INFORMATION memory{};
        Require(VirtualQuery(nameplatePage, &memory, sizeof(memory)) == sizeof(memory)
            && memory.Protect == PAGE_EXECUTE_READ, "code page protection restored");
        auto replayChatFont = reinterpret_cast<unsigned(__cdecl*)()>(nameplatePage + 0x2000);
        Require(replayChatFont() == 34, "original chat target selects 11-pixel font");
        Require(!InstallChatTargetFont(nameplatePage + 0x2001), "wrong chat instruction rejects patch");
        Require(InstallChatTargetFont(nameplatePage + 0x2000), "patch isolated chat target font selection");
        for (unsigned i = 0; i < 100; ++i)
            Require(replayChatFont() == 0, "chat target selects normal 12-pixel font with balanced stack");
        for (unsigned i = 0; i < sizeof(chatFontSelection); ++i)
            Require(nameplatePage[0x2000 + i] == (i == 4 ? 0 : chatFontSelection[i]),
                "chat patch only changes its font index immediate");
        Require(VirtualQuery(nameplatePage + 0x2000, &memory, sizeof(memory)) == sizeof(memory)
            && memory.Protect == PAGE_EXECUTE_READ, "chat instruction protection restored");
        constructNameplateFace = previousConstructor;
        smoothFonts = previousSmoothing;
        VirtualFree(nameplatePage, 0, MEM_RELEASE);
        auto adapters = reinterpret_cast<GetAdaptersInfoFn>(
            GetProcAddress(GetModuleHandleW(L"iphlpapi.dll"), "GetAdaptersInfo"));
        AdapterInfo actualAdapters[16]{};
        ULONG actualSize = sizeof(actualAdapters);
        Require(adapters(actualAdapters, &actualSize) == ERROR_SUCCESS,
            "installed adapter hook accepts the client fixed buffer");
        D3D8DisplayModeHook::Install();
        auto create = reinterpret_cast<D3D8DisplayModeHook::Direct3DCreate8_t>(
            GetProcAddress(GetModuleHandleW(L"d3d8.dll"), "Direct3DCreate8"));
        Require(create != nullptr, "Wine supplies Direct3D8");
        void* d3d = create(220);
        Require(d3d != nullptr, "Wine creates Direct3D8 interface");
        void** table = *reinterpret_cast<void***>(d3d);
        auto count = reinterpret_cast<D3D8DisplayModeHook::GetAdapterModeCount_t>(table[6]);
        auto enumerate = reinterpret_cast<D3D8DisplayModeHook::EnumAdapterModes_t>(table[7]);
        bool found = false;
        for (UINT index = 0; index < count(d3d, 0); ++index) {
            D3D8DisplayModeHook::DisplayMode display{};
            if (enumerate(d3d, 0, index, &display) >= 0 && display.Width == 1280 && display.Height == 720)
                found = true;
        }
        Require(found, "Wine mode table includes configured game resolution");
        reinterpret_cast<ULONG(WINAPI*)(void*)>(table[2])(d3d);
    }
    std::puts("PASS GBK/UTF-8, fonts and adapter ABI/error tests");
    std::puts(WineCompatibility::IsWine() ? "PASS Wine hook installation and D3D8 mode enumeration"
        : "SKIP Wine-only hook and D3D8 smoke on native Windows");
}
