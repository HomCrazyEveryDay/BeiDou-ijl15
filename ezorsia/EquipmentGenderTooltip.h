#pragma once
#include <windows.h>
#include <cstring>

namespace EquipmentGenderTooltip {
struct TextEnd { const char* data; size_t length; };
static bool RemoveEnd(TextEnd& text, const char* suffix) {
    const size_t length = std::strlen(suffix);
    if (text.length < length || std::memcmp(text.data + text.length - length, suffix, length)) return false;
    text.length -= length;
    return true;
}

static void TrimEnd(TextEnd& text) {
    while (text.length) {
        const char last = text.data[text.length - 1];
        if (last == ' ' || last == '\t' || last == '\r' || last == '\n') --text.length;
        else if (!RemoveEnd(text, "\xA1\xA1") && !RemoveEnd(text, "\xE3\x80\x80")) break;
    }
}

// Resource names are normally GBK. Also accept UTF-8 and English labels, and
// mixed full-width/ASCII parentheses found in the original CMS names. Require
// a terminal, bracketed label matching the real restriction; words such as
// "witch" or "prince", and conflicting labels, must not suppress the warning.
static int __stdcall ForTitle(int gender, const char* name) {
    if ((gender != 0 && gender != 1) || !name) return gender;
    TextEnd text{name, std::strlen(name)};
    TrimEnd(text);
    if (!RemoveEnd(text, ")") && !RemoveEnd(text, "\xA3\xA9") && !RemoveEnd(text, "\xEF\xBC\x89")) return gender;
    TrimEnd(text);
    if (!RemoveEnd(text, gender ? "\xC5\xAE" : "\xC4\xD0")
        && !RemoveEnd(text, gender ? "\xE5\xA5\xB3" : "\xE7\x94\xB7")
        && !RemoveEnd(text, gender ? "Female" : "Male")) return gender;
    TrimEnd(text);
    if (!RemoveEnd(text, "(") && !RemoveEnd(text, "\xA3\xA8") && !RemoveEnd(text, "\xEF\xBC\x88")) return gender;
    TrimEnd(text);
    return text.length ? 2 : gender;
}

using NativeGender = int(__cdecl*)(int);
static auto nativeGender = reinterpret_cast<NativeGender>(0x00460ADC);

// CToolTip::MakeEquipItemName calls the common gender predicate at 008EC75A.
// EDI is its validated CItemInfo equip entry, whose +10 is the WZ name string.
// Suppress only this title's extra label. The common predicate (including all
// wearing/preview restrictions), resource name, upgrade suffix and title color
// remain native. The caller still owns and pops its item-ID argument.
__declspec(naked) static void TitleGender() {
    __asm {
        push dword ptr [esp + 4]
        call dword ptr [nativeGender]
        add esp, 4
        push ecx
        push edx
        push dword ptr [edi + 10h]
        push eax
        call ForTitle
        pop edx
        pop ecx
        ret
    }
}

static bool Install() {
    const BYTE expected[] = {0xe8,0x7d,0x43,0xb7,0xff,0x83,0xe8,0x00,0x59,0x74,0x2e};
    const BYTE genderCode[] = {0x8b,0x44,0x24,0x04,0x99,0xb9,0x40,0x42,0x0f,0x00,0xf7,0xf9};
    auto site = reinterpret_cast<BYTE*>(0x008EC75A);
    if (std::memcmp(site, expected, sizeof(expected))
        || std::memcmp(reinterpret_cast<void*>(nativeGender), genderCode, sizeof(genderCode))) return false;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection)) return false;
    const DWORD offset = reinterpret_cast<DWORD>(TitleGender) - 0x008EC75F;
    std::memcpy(site + 1, &offset, sizeof(offset));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
    if (!flushed) {
        std::memcpy(site, expected, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    const bool restored = VirtualProtect(site, 5, protection, &ignored) != FALSE;
    return flushed && restored;
}
}
