#include "stdafx.h"
#include "WineCompatibility.h"
#include "ClientLog.h"
#include "detours.h"
#include <cstddef>
#include <intrin.h>

// Based on the failure paths documented in BeiDouMS/BeiDou-ijl15 PR #25.
namespace {
constexpr UINT ChineseCodePage = 936;
CPINFO chineseCodePageInfo{};
bool smoothFonts = true;
bool forceChineseCharset = false;
bool traceFonts = false;
SRWLOCK fontTraceLock = SRWLOCK_INIT;
LOGFONTW tracedFonts[32]{};
unsigned tracedFontCount = 0;

decltype(&MultiByteToWideChar) originalMultiByteToWideChar = MultiByteToWideChar;
decltype(&WideCharToMultiByte) originalWideCharToMultiByte = WideCharToMultiByte;
decltype(&GetACP) originalGetACP = GetACP;
decltype(&GetOEMCP) originalGetOEMCP = GetOEMCP;
decltype(&GetCPInfo) originalGetCPInfo = GetCPInfo;
decltype(&IsDBCSLeadByte) originalIsDBCSLeadByte = IsDBCSLeadByte;
decltype(&IsDBCSLeadByteEx) originalIsDBCSLeadByteEx = IsDBCSLeadByteEx;
decltype(&CreateFontIndirectA) originalCreateFontIndirectA = CreateFontIndirectA;
decltype(&CreateFontIndirectW) originalCreateFontIndirectW = CreateFontIndirectW;

UINT MapCodePage(UINT codePage) {
    // 只接管系统默认代码页；日志、启动器设置等显式 UTF-8 转换保持原样。
    return codePage == CP_ACP || codePage == CP_OEMCP || codePage == CP_THREAD_ACP
        ? ChineseCodePage : codePage;
}

int WINAPI ConvertToWide(UINT codePage, DWORD flags, LPCCH source, int sourceLength,
    LPWSTR destination, int destinationLength) {
    return originalMultiByteToWideChar(MapCodePage(codePage), flags,
        source, sourceLength, destination, destinationLength);
}

int WINAPI ConvertFromWide(UINT codePage, DWORD flags, LPCWCH source, int sourceLength,
    LPSTR destination, int destinationLength, LPCCH defaultChar, LPBOOL usedDefaultChar) {
    return originalWideCharToMultiByte(MapCodePage(codePage), flags,
        source, sourceLength, destination, destinationLength, defaultChar, usedDefaultChar);
}

UINT WINAPI ChineseACP() { return ChineseCodePage; }
UINT WINAPI ChineseOEMCP() { return ChineseCodePage; }
BOOL WINAPI ChineseCPInfo(UINT codePage, LPCPINFO info) {
    return originalGetCPInfo(MapCodePage(codePage), info);
}
BOOL WINAPI ChineseLeadByte(BYTE value) {
    for (int index = 0; index + 1 < MAX_LEADBYTES && chineseCodePageInfo.LeadByte[index]; index += 2) {
        if (value >= chineseCodePageInfo.LeadByte[index]
            && value <= chineseCodePageInfo.LeadByte[index + 1]) return TRUE;
    }
    return FALSE;
}
BOOL WINAPI ChineseLeadByteEx(UINT codePage, BYTE value) {
    return originalIsDBCSLeadByteEx(MapCodePage(codePage), value);
}

void TraceFont(const LOGFONTW& requested, HFONT font, void* caller) {
    if (!traceFonts || !font) return;
    const DWORD savedError = GetLastError();
    bool record = false;
    AcquireSRWLockExclusive(&fontTraceLock);
    unsigned index = 0;
    for (; index < tracedFontCount; ++index) {
        if (memcmp(&tracedFonts[index], &requested, sizeof(requested)) == 0) break;
    }
    if (index == tracedFontCount && tracedFontCount < _countof(tracedFonts)) {
        tracedFonts[tracedFontCount++] = requested;
        record = true;
    }
    ReleaseSRWLockExclusive(&fontTraceLock);
    if (!record) { SetLastError(savedError); return; }

    LOGFONTW effective{};
    GetObjectW(font, sizeof(effective), &effective);
    wchar_t actual[LF_FACESIZE]{};
    DWORD nameHash = 2166136261u, nameBytes = 0;
    WORD chineseGlyph = 0xffff;
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc) {
        HGDIOBJ previous = SelectObject(dc, font);
        GetTextFaceW(dc, _countof(actual), actual);
        // 不记录玩家文本，只识别实际字体文件和一个固定中文字的覆盖情况。
        GetGlyphIndicesW(dc, L"\u4e2d", 1, &chineseGlyph, GGI_MARK_NONEXISTING_GLYPHS);
        const DWORD size = GetFontData(dc, 0x656d616e, 0, nullptr, 0);
        if (size != GDI_ERROR && size <= 65536) {
            BYTE data[512];
            for (DWORD offset = 0; offset < size;) {
                const DWORD count = (size - offset < sizeof(data)) ? size - offset : sizeof(data);
                if (GetFontData(dc, 0x656d616e, offset, data, count) != count) break;
                for (DWORD i = 0; i < count; ++i) nameHash = (nameHash ^ data[i]) * 16777619u;
                offset += count;
                nameBytes = offset;
            }
        }
        SelectObject(dc, previous);
        DeleteDC(dc);
    }
    char requestUtf8[128]{}, effectiveUtf8[128]{}, actualUtf8[128]{};
    WideCharToMultiByte(CP_UTF8, 0, requested.lfFaceName, -1, requestUtf8, sizeof(requestUtf8), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, effective.lfFaceName, -1, effectiveUtf8, sizeof(effectiveUtf8), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, actual, -1, actualUtf8, sizeof(actualUtf8), nullptr, nullptr);
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "wine_font_sample index=%u requested=%s effective=%s realized=%s height=%ld weight=%ld charset=%u->%u quality=%u->%u nameBytes=%lu nameHash=%08lX chineseGlyph=%04X caller=%p",
        index + 1, requestUtf8, effectiveUtf8, actualUtf8, requested.lfHeight, requested.lfWeight,
        requested.lfCharSet, effective.lfCharSet, requested.lfQuality, effective.lfQuality,
        nameBytes, nameHash, chineseGlyph, caller);
    SetLastError(savedError);
}

bool UseSmallBoldBitmap(LONG height, LONG weight, BYTE quality) {
    // 运行样本中的 Arial 12、Tahoma 11、家族 SimSun 12 粗体均请求灰度 AA。
    // Wine 的中文链接/模拟粗体会走轮廓字形，丢掉原版小字号点阵的笔画。
    return !smoothFonts && (height == -11 || height == -12)
        && weight == FW_BOLD && quality == ANTIALIASED_QUALITY;
}

bool IsClassicUiFace(const char* face) {
    return _stricmp(face, "Arial") == 0 || _stricmp(face, "Tahoma") == 0 || _stricmp(face, "SimSun") == 0;
}
bool IsClassicUiFace(const wchar_t* face) {
    return _wcsicmp(face, L"Arial") == 0 || _wcsicmp(face, L"Tahoma") == 0 || _wcsicmp(face, L"SimSun") == 0;
}

HFONT WINAPI ChineseFontA(const LOGFONTA* font) {
    if (font && font->lfCharSet != SYMBOL_CHARSET) {
        LOGFONTA copy = *font;
        // 本机 Windows 缺少 Arial Narrow 时回退宋体；macOS 同名字体会抢先匹配，
        // 随后把中文交给另一套字形。Windows 字体模式显式复现已验证的回退。
        if (!smoothFonts && (_stricmp(copy.lfFaceName, "Arial Narrow") == 0
            || strcmp(copy.lfFaceName, "\xB5\xB8\xBF\xF2") == 0))
            strcpy_s(copy.lfFaceName, "SimSun");
        // 中文 ACP 已经正确时保留 DEFAULT_CHARSET，否则 Arial 会被换成
        // Arial Unicode MS；只为非中文环境模拟中文字符集。
        if (forceChineseCharset && copy.lfCharSet == DEFAULT_CHARSET) copy.lfCharSet = GB2312_CHARSET;
        // Canvas 的 n 样式会禁用抗锯齿；苹方在小字号下因此只剩硬边像素。
        // 使用灰度覆盖率，保留字形尺寸与颜色，避免透明贴图出现 ClearType 彩边。
        if (smoothFonts && copy.lfQuality == NONANTIALIASED_QUALITY) copy.lfQuality = ANTIALIASED_QUALITY;
        if (UseSmallBoldBitmap(copy.lfHeight, copy.lfWeight, copy.lfQuality) && IsClassicUiFace(copy.lfFaceName))
            copy.lfQuality = NONANTIALIASED_QUALITY;
        HFONT result = originalCreateFontIndirectA(&copy);
        if (traceFonts) {
            LOGFONTW requested{};
            memcpy(&requested, font, offsetof(LOGFONTA, lfFaceName));
            originalMultiByteToWideChar(ChineseCodePage, 0, font->lfFaceName, -1,
                requested.lfFaceName, _countof(requested.lfFaceName));
            TraceFont(requested, result, _ReturnAddress());
        }
        return result;
    }
    return originalCreateFontIndirectA(font);
}
HFONT WINAPI ChineseFontW(const LOGFONTW* font) {
    if (font && font->lfCharSet != SYMBOL_CHARSET) {
        LOGFONTW copy = *font;
        if (!smoothFonts && (_wcsicmp(copy.lfFaceName, L"Arial Narrow") == 0
            || wcscmp(copy.lfFaceName, L"\u8e48\u6846") == 0))
            wcscpy_s(copy.lfFaceName, L"SimSun");
        if (forceChineseCharset && copy.lfCharSet == DEFAULT_CHARSET) copy.lfCharSet = GB2312_CHARSET;
        if (smoothFonts && copy.lfQuality == NONANTIALIASED_QUALITY) copy.lfQuality = ANTIALIASED_QUALITY;
        if (UseSmallBoldBitmap(copy.lfHeight, copy.lfWeight, copy.lfQuality) && IsClassicUiFace(copy.lfFaceName))
            copy.lfQuality = NONANTIALIASED_QUALITY;
        HFONT result = originalCreateFontIndirectW(&copy);
        TraceFont(*font, result, _ReturnAddress());
        return result;
    }
    return originalCreateFontIndirectW(font);
}

// v83 的 x86 IP_ADAPTER_INFO 布局，避免宿主 CRT 的 time_t 宽度改变尾部 ABI。
struct AdapterInfo {
    AdapterInfo* next;
    DWORD comboIndex;
    char name[260];
    char description[132];
    UINT addressLength;
    BYTE address[8];
    DWORD index;
    UINT type;
    UINT dhcpEnabled;
    void* currentIpAddress;
    BYTE remaining[0x280 - 0x1AC];
};
static_assert(sizeof(AdapterInfo) == 0x280, "v83 adapter ABI is x86 only");
static_assert(offsetof(AdapterInfo, address) == 0x194, "v83 MAC address offset");
static_assert(offsetof(AdapterInfo, type) == 0x1A0, "v83 adapter type offset");
using GetAdaptersInfoFn = DWORD(WINAPI*)(AdapterInfo*, PULONG);
GetAdaptersInfoFn originalGetAdaptersInfo = nullptr;
BYTE stableAddress[6]{};

bool BuildStableAddress() {
    DWORD serial = 0;
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD length = _countof(computer);
    if (!GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)
        || !GetComputerNameW(computer, &length)) return false;
    unsigned __int64 hash = 14695981039346656037ULL;
    const auto mix = [&hash](const void* data, size_t size) {
        const auto bytes = static_cast<const BYTE*>(data);
        for (size_t index = 0; index < size; ++index) hash = (hash ^ bytes[index]) * 1099511628211ULL;
    };
    mix(&serial, sizeof(serial));
    mix(computer, length * sizeof(wchar_t));
    for (size_t index = 0; index < sizeof(stableAddress); ++index)
        stableAddress[index] = static_cast<BYTE>(hash >> (index * 8));
    stableAddress[0] = (stableAddress[0] & 0xFC) | 0x02;
    return true;
}

DWORD WINAPI ReadAdapters(AdapterInfo* adapters, PULONG size) {
    const ULONG capacity = size ? *size : 0;
    const DWORD result = originalGetAdaptersInfo(adapters, size);
    if (result == ERROR_SUCCESS || result == ERROR_INVALID_PARAMETER
        || !adapters || capacity < sizeof(AdapterInfo)) return result;

    // 原客户端不检查错误就遍历栈缓冲区；失败时提供合法单节点，阻止选人路径读野指针。
    ZeroMemory(adapters, sizeof(*adapters));
    adapters->comboIndex = adapters->index = 1;
    adapters->type = 6; // MIB_IF_TYPE_ETHERNET，原客户端只接受此类型。
    adapters->addressLength = sizeof(stableAddress);
    memcpy(adapters->address, stableAddress, sizeof(stableAddress));
    strcpy_s(adapters->name, "{00000000-0000-0000-0000-000000000000}");
    strcpy_s(adapters->description, "BeiDou Wine compatibility adapter");
    *size = sizeof(*adapters);
    return ERROR_SUCCESS;
}

struct Hook { PVOID* original; PVOID replacement; };

using NativeBstrConstructor = void*(__thiscall*)(void*, const char*);
NativeBstrConstructor constructNameplateFace = reinterpret_cast<NativeBstrConstructor>(0x00406301);

void* __fastcall GuildFontFace(void*, void*, void* output, unsigned) {
    // 只替换名牌 1004/1005 的字体名。使用客户端自身构造器分配 _bstr_t，
    // 后面的 IWzFont::Create 包装函数仍按原路径消费引用，避免跨 CRT 释放。
    return constructNameplateFace(output, "SimSun");
}
}

bool WineCompatibility::IsWine() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

bool WineCompatibility::Install() {
    if (!IsWine()) return true;
    // Windows 宋体含小字号点阵字形，强制平滑会使笔画与原生 Windows 不一致。
    wchar_t fontStyle[16]{};
    GetEnvironmentVariableW(L"BEIDOU_WINE_FONT_STYLE", fontStyle, _countof(fontStyle));
    smoothFonts = wcscmp(fontStyle, L"original") != 0;
    wchar_t fontTrace[4]{};
    GetEnvironmentVariableW(L"BEIDOU_WINE_FONT_TRACE", fontTrace, _countof(fontTrace));
    traceFonts = wcscmp(fontTrace, L"1") == 0;
    // 调用点在进程入口，已离开 DllMain 的 loader lock。
    const HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
    originalGetAdaptersInfo = iphlpapi
        ? reinterpret_cast<GetAdaptersInfoFn>(GetProcAddress(iphlpapi, "GetAdaptersInfo")) : nullptr;
    if (!originalGetAdaptersInfo || !BuildStableAddress()) return false;
    const bool fixLocale = GetACP() != ChineseCodePage;
    forceChineseCharset = fixLocale;
    if (fixLocale && !GetCPInfo(ChineseCodePage, &chineseCodePageInfo)) return false;

    const Hook hooks[] = {
        {reinterpret_cast<PVOID*>(&originalGetAdaptersInfo), reinterpret_cast<PVOID>(ReadAdapters)},
        // 字体质量与 ACP 无关：中文 prefix 也要安装，不能被代码页判断跳过。
        {reinterpret_cast<PVOID*>(&originalCreateFontIndirectA), reinterpret_cast<PVOID>(ChineseFontA)},
        {reinterpret_cast<PVOID*>(&originalCreateFontIndirectW), reinterpret_cast<PVOID>(ChineseFontW)},
        {reinterpret_cast<PVOID*>(&originalMultiByteToWideChar), reinterpret_cast<PVOID>(ConvertToWide)},
        {reinterpret_cast<PVOID*>(&originalWideCharToMultiByte), reinterpret_cast<PVOID>(ConvertFromWide)},
        {reinterpret_cast<PVOID*>(&originalGetACP), reinterpret_cast<PVOID>(ChineseACP)},
        {reinterpret_cast<PVOID*>(&originalGetOEMCP), reinterpret_cast<PVOID>(ChineseOEMCP)},
        {reinterpret_cast<PVOID*>(&originalGetCPInfo), reinterpret_cast<PVOID>(ChineseCPInfo)},
        {reinterpret_cast<PVOID*>(&originalIsDBCSLeadByte), reinterpret_cast<PVOID>(ChineseLeadByte)},
        {reinterpret_cast<PVOID*>(&originalIsDBCSLeadByteEx), reinterpret_cast<PVOID>(ChineseLeadByteEx)}
    };
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) return false;
    error = DetourUpdateThread(GetCurrentThread());
    for (size_t index = 0; error == NO_ERROR && index < (fixLocale ? _countof(hooks) : 3); ++index)
        error = DetourAttach(hooks[index].original, hooks[index].replacement);
    if (error == NO_ERROR) error = DetourTransactionCommit();
    else DetourTransactionAbort();
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "wine_compatibility locale=%d adapter=1 fontAntialias=%s fontTrace=%d installError=%ld",
        fixLocale, smoothFonts ? "grayscale" : "original", traceFonts, error);
    return error == NO_ERROR;
}

namespace {
bool InstallNameplateCall(BYTE* call, const BYTE* requestedFontId) {
    const BYTE original[] = {0xE8,0xAB,0x52,0xE1,0xFF}; // call 00406292
    const BYTE fontId[] = {0x68,0x97,0x15,0x00,0x00}; // push StringPool 5527 (Arial)
    if (memcmp(call, original, sizeof(original))
        || memcmp(requestedFontId, fontId, sizeof(fontId))) return false;
    BYTE replacement[5] = {0xE8};
    const DWORD displacement = reinterpret_cast<DWORD>(GuildFontFace) - (reinterpret_cast<DWORD>(call) + 5);
    memcpy(replacement + 1, &displacement, sizeof(displacement));
    DWORD protection = 0;
    if (!VirtualProtect(call, sizeof(replacement), PAGE_EXECUTE_READWRITE, &protection)) return false;
    memcpy(call, replacement, sizeof(replacement));
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), call, sizeof(replacement));
    if (!flushed) memcpy(call, original, sizeof(original));
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(call, sizeof(replacement), protection, &ignored);
    if (!restored) {
        memcpy(call, original, sizeof(original));
        FlushInstructionCache(GetCurrentProcess(), call, sizeof(original));
        VirtualProtect(call, sizeof(original), protection, &ignored);
    }
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "wine_nameplate_fonts guildCall=005F0FE2 legacyFace=SimSun installed=%d", flushed && restored);
    return flushed && restored;
}

bool InstallChatTargetFont(BYTE* code) {
    const BYTE original[] = {0x6A,0x01,0x59,0x6A,0x22,0x58};
    if (memcmp(code, original, sizeof(original))) return false;
    DWORD protection = 0;
    if (!VirtualProtect(code, sizeof(original), PAGE_EXECUTE_READWRITE, &protection)) return false;
    // 仅 CUIStatusBar 聊天对象控件：字体 34 为 Arial 11，字体 0 为 Arial 12，
    // 均为白色普通字重。复用正文的小字号字形，避免 11 像素中文笔画挤压。
    code[4] = 0;
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), code, sizeof(original));
    if (!flushed) code[4] = original[4];
    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(code, sizeof(original), protection, &ignored);
    if (!restored) {
        code[4] = original[4];
        FlushInstructionCache(GetCurrentProcess(), code, sizeof(original));
        VirtualProtect(code, sizeof(original), protection, &ignored);
    }
    return flushed && restored;
}
}

bool WineCompatibility::InstallUiFonts() {
    if (!IsWine() || smoothFonts) return true;
    return InstallNameplateCall(reinterpret_cast<BYTE*>(0x005F0FE2),
        reinterpret_cast<const BYTE*>(0x005F0FD1))
        && InstallChatTargetFont(reinterpret_cast<BYTE*>(0x008D370C));
}
