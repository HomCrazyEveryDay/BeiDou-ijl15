// The runner rebases only native addresses; this is the production hook code.
#include "SkillTooltipLayoutUnderTest.h"

#include <cstdio>
#include <cstdlib>

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void Memory::CodeCave(void* target, DWORD address, int size) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    memset(code, 0x90, size);
    code[0] = 0xE9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(target) - address - 5;
    FlushInstructionCache(GetCurrentProcess(), code, size);
}

class TestFont : public IUnknown {
public:
    ULONG references = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { return --references; }
};

static TestFont g_font;
static int g_drawHeight = 150, g_drawCalls;
static char g_tooltip;
static const char g_description[] = "Original text\r\n#cPassive# details";
struct TestSkill { int id; const char* name; const char* description; };
static TestSkill g_skill = {3120005, "Bow Expert", g_description};

static int __fastcall FontHeightStub(IUnknown* font, void*) {
    Require(font == &g_font, "measurement uses the native tooltip font");
    return 11;
}

static int __fastcall DrawTextStub(void* tooltip, void*, int left, int right, int top,
    const char* description, IUnknown* font, int canvas, int* width, int lineBreaks) {
    Require(tooltip == &g_tooltip && description == g_description, "original tooltip and description");
    Require(left == 87 && right == 300 && top == 0, "same description bounds as native drawing");
    Require(canvas == 0 && width == nullptr && lineBreaks == 1, "measurement cannot draw or discard newlines");
    Require(font == &g_font && g_font.references == 2, "by-value font reference is retained");
    font->Release();
    ++g_drawCalls;
    return g_drawHeight;
}

static DWORD g_afterEbx, g_afterEcx, g_afterEdx, g_afterEsi, g_afterEdi, g_afterFlags;

__declspec(naked) int InvokeHeightCave(void* tooltip, IUnknown* font, TestSkill* skill, int lines) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 14h
        push ebx
        push esi
        push edi
        mov eax, dword ptr[ebp + 0Ch]
        mov dword ptr[ebp - 14h], eax
        mov esi, dword ptr[ebp + 8]
        mov eax, dword ptr[ebp + 14h]
        xor edi, edi
        mov ebx, 12345678h
        mov ecx, 23456789h
        mov edx, 3456789Ah
        push offset resume
        push 308F266Ah
        ret
    resume:
        mov g_afterEbx, ebx
        mov g_afterEcx, ecx
        mov g_afterEdx, edx
        mov g_afterEsi, esi
        mov g_afterEdi, edi
        pushfd
        pop g_afterFlags
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

int main() {
    for (DWORD base : {0x30480000u, 0x308F0000u}) {
        Require(VirtualAlloc(reinterpret_cast<void*>(base), 0x10000,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE) == reinterpret_cast<void*>(base), "reserve native call stubs");
    }
    Memory::CodeCave(FontHeightStub, 0x30485A45, 5);
    Memory::CodeCave(DrawTextStub, 0x308F4535, 5);
    const unsigned char expected[] = {0x83, 0xC0, 0xFC, 0x3B, 0xC7};
    SkillTooltipLayout::Install();
    Require(*reinterpret_cast<unsigned char*>(0x308F266A) == 0, "unexpected client code is not patched");
    memcpy(reinterpret_cast<void*>(0x308F266A), expected, sizeof(expected));
    *reinterpret_cast<unsigned char*>(0x308F266F) = 0xC3;
    SkillTooltipLayout::Install();
    Require(*reinterpret_cast<unsigned char*>(0x308F266A) == 0xE9, "matching native instructions are patched");

    Require(InvokeHeightCave(&g_tooltip, &g_font, &g_skill, 8) == 6, "overlapping ten-line description expands from eight to ten");
    Require(g_afterEbx == 0x12345678 && g_afterEcx == 0x23456789 && g_afterEdx == 0x3456789A
        && g_afterEsi == reinterpret_cast<DWORD>(&g_tooltip) && g_afterEdi == 0, "cave preserves native registers and stack");
    Require((g_afterFlags & 0xC1) == 0, "native positive comparison flags are replayed");
    Require(g_font.references == 1 && g_drawCalls == 1, "font references balance after measurement");
    g_skill.id = 3121006;
    g_drawHeight = 151;
    Require(InvokeHeightCave(&g_tooltip, &g_font, &g_skill, 8) == 7, "Phoenix height rounds up partial lines");
    g_skill.id = 4111002;
    Require(InvokeHeightCave(&g_tooltip, &g_font, &g_skill, 8) == 7, "Shadow Partner passive description expands tooltip height");
    g_drawHeight = 30;
    Require(InvokeHeightCave(&g_tooltip, &g_font, &g_skill, 8) == 4, "shorter measurement never shrinks native layout");
    g_drawHeight = 0;
    Require(InvokeHeightCave(&g_tooltip, &g_font, &g_skill, 8) == 4, "empty measurement preserves native layout");
    const int calls = g_drawCalls;
    g_skill.id = 3121004;
    Require(InvokeHeightCave(&g_tooltip, &g_font, &g_skill, 2) == -2, "unrelated skills retain native line count");
    Require((g_afterFlags & 0x80) != 0, "native negative comparison flags are replayed");
    g_skill.id = 3120005;
    g_skill.description = "";
    Require(InvokeHeightCave(&g_tooltip, &g_font, &g_skill, 4) == 0 && (g_afterFlags & 0x40), "empty description and zero comparison");
    g_skill.description = g_description;
    Require(InvokeHeightCave(&g_tooltip, nullptr, &g_skill, 8) == 4, "missing font falls back");
    Require(InvokeHeightCave(&g_tooltip, &g_font, nullptr, 8) == 4, "missing skill falls back");
    Require(g_drawCalls == calls && g_font.references == 1, "fallback paths do not call renderer or change references");
    std::puts("PASS: skill tooltip height scope, bounds, rounding, ABI, flags and COM references");
}
