#include "stdafx.h"
#include "SkillTooltipLayout.h"

#include <unknwn.h>

namespace {
DWORD g_skillTooltipHeightReturn = 0x008F266F;

int __stdcall MeasureSkillDescription(void* tooltip, const void* skill, IUnknown* font, int originalLines) {
    if (!skill || !font) {
        return originalLines;
    }
    const auto* data = static_cast<const unsigned char*>(skill);
    const int skillId = *reinterpret_cast<const int*>(data);
    if (skillId != 3120005 && skillId != 3121006) {
        return originalLines;
    }
    const char* description = *reinterpret_cast<const char* const*>(data + 8);
    if (!description || !*description) {
        return originalLines;
    }

    using FontHeight = int(__thiscall*)(IUnknown*);
    using DrawText = int(__thiscall*)(void*, int, int, int, const char*, IUnknown*, int, int*, int);
    const int lineHeight = reinterpret_cast<FontHeight>(0x00485A45)(font) + 4;
    if (lineHeight <= 0) {
        return originalLines;
    }

    // CUIToolTip::DrawText with no canvas uses the exact GBK, color and newline
    // layout used for rendering. Its by-value font argument consumes one reference.
    font->AddRef();
    const int height = reinterpret_cast<DrawText>(0x008F4535)(
        tooltip, 87, 300, 0, description, font, 0, nullptr, 1);
    const int lines = height > 0 ? (height - 1) / lineHeight + 1 : 0;
    return lines > originalLines ? lines : originalLines;
}

__declspec(naked) void SkillTooltipHeightCave() {
    __asm {
        pushad
        push eax
        push dword ptr[ebp - 14h]
        push dword ptr[ebp + 10h]
        push esi
        call MeasureSkillDescription
        mov dword ptr[esp + 1Ch], eax
        popad

        // Replay the native minimum-four-line calculation and its comparison.
        add eax, -4
        cmp eax, edi
        jmp dword ptr[g_skillTooltipHeightReturn]
    }
}
}

namespace SkillTooltipLayout {
void Install() {
    const unsigned char expected[] = { 0x83, 0xC0, 0xFC, 0x3B, 0xC7 };
    if (memcmp(reinterpret_cast<const void*>(0x008F266A), expected, sizeof(expected)) != 0) {
        return;
    }
    Memory::CodeCave(SkillTooltipHeightCave, 0x008F266A, sizeof(expected));
}
}
