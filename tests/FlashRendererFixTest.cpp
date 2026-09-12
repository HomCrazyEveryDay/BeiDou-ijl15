#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include "../ezorsia/FlashRendererFix.h"
namespace CrashReporter { void RecordEvent(const char*, const char*, ...) {} }
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s error=%lu\n", __LINE__, #x, GetLastError()); fflush(stdout); ExitProcess(1); } } while (0)
using Render = int(__thiscall*)(void*);
static LONG faults = 0;
static LONG releases = 0;
static int frames = 0;
static int instances = 0;
static int __cdecl ReleaseStub() { ++releases; return 0; }
static int __fastcall FrameCount(void*, void*) { ++frames; return 2; }
static void __fastcall SetPlaying(void*, void*, int value) { CHECK(value == 0); }
static int __fastcall CurrentFrame(void*, void*) { ++instances; return 0; }
static LONG CALLBACK Observe(EXCEPTION_POINTERS* p) {
    if (p->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) ++faults;
    return EXCEPTION_CONTINUE_SEARCH;
}
static void RunCleanupBlock(BYTE* base, DWORD* parent) {
    void* entry = base + 0x35DF;
    DWORD dummy = 0;
    __asm {
        push esi
        push edi
        push ebx
        mov esi, parent
        xor edi, edi
        lea eax, dummy
        call entry
        pop ebx
        pop edi
        pop esi
    }
}
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); CHECK(argc == 2);
    CHECK(SetCurrentDirectoryA(argv[1]));
    CHECK(SetDllDirectoryA(argv[1]));
    auto observer = AddVectoredExceptionHandler(1, Observe);
    CHECK(observer);
    CHECK(!GetModuleHandleA("Gr2D_DX8.dll"));
    FlashRendererFix::Install();
    CHECK(!GetModuleHandleA("Gr2D_DX8.dll"));
    printf("PASS early install does not load graphics module\n");
    auto gr = LoadLibraryA("Gr2D_DX8.dll");
    auto flash = LoadLibraryA("WzFlashRenderer.dll");
    CHECK(gr && flash);
    auto base = reinterpret_cast<BYTE*>(gr);
    auto fb = reinterpret_cast<BYTE*>(flash);
    Render render = reinterpret_cast<Render>(base + 0x7CA6);
    DWORD object[0x140 / 4]{};
    DWORD owner[0x400 / 4]{};
    object[0] = reinterpret_cast<DWORD>(flash);
    object[3] = reinterpret_cast<DWORD>(GetProcAddress(flash, "RenderFlash"));
    object[7] = reinterpret_cast<DWORD>(owner);
    CHECK(object[3]);
    for (int i=0; i<100; ++i) CHECK(render(object) == 1);
    CHECK(faults == 100);
    printf("PASS original native render: 100 calls, 100 AVs\n");

    // Execute the real native duplicate-call block, returning immediately
    // after it; the rest of device teardown requires an actual D3D device.
    DWORD protect = 0;
    CHECK(VirtualProtect(base + 0x35F5, 1, PAGE_EXECUTE_READWRITE, &protect));
    BYTE saved = base[0x35F5]; base[0x35F5] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), base + 0x35F5, 1);
    DWORD parent[0x300 / 4]{};
    DWORD* cleanup = parent + 0x128 / 4;
    // No library handle in this synthetic object: test the exact callback
    // count without unloading the renderer used by the rendering tests.
    cleanup[3] = object[3];
    cleanup[4] = reinterpret_cast<DWORD>(&ReleaseStub);
    RunCleanupBlock(base, parent);
    CHECK(releases == 2);
    printf("PASS original native cleanup: 2 release callbacks\n");

    FlashRendererFix::Install();
    CHECK(FlashRendererFix::Install(gr));
    CHECK(FlashRendererFix::Install(gr));
    CHECK(!FlashRendererFix::Install(GetModuleHandleA(nullptr)));
    faults = 0;
    for (int i=0; i<10000; ++i) CHECK(render(object) == 1);
    CHECK(faults == 0);
    printf("PASS fixed native render: 10000 calls, 0 AVs, same failure return\n");
    auto load = reinterpret_cast<int(__cdecl*)(const char*)>(GetProcAddress(flash, "LoadMediaFile"));
    CHECK(load && load("__beidou_test_missing_movie_435983.swf") == 1);
    CHECK(render(object) == 1 && faults == 0);
    printf("PASS failed media load: no null-object rendering\n");

    // Exercise the actual native RenderFlash valid-object branch. Deliberately
    // offscreen coordinates stop before D3D, so no game/window/GPU is needed.
    DWORD movieVtable[0x44 / 4]{};
    DWORD instanceVtable[0x74 / 4]{};
    movieVtable[0x38 / 4] = reinterpret_cast<DWORD>(&FrameCount);
    instanceVtable[0x24 / 4] = reinterpret_cast<DWORD>(&CurrentFrame);
    instanceVtable[0x70 / 4] = reinterpret_cast<DWORD>(&SetPlaying);
    DWORD movie = reinterpret_cast<DWORD>(movieVtable);
    DWORD instance = reinterpret_cast<DWORD>(instanceVtable);
    auto state = reinterpret_cast<DWORD*>(fb + 0x6E088);
    DWORD savedMovie = state[0], savedInstance = state[1];
    state[0] = reinterpret_cast<DWORD>(&movie);
    CHECK(render(object) == 1 && faults == 0); // definition but no instance
    state[1] = reinterpret_cast<DWORD>(&instance);
    object[0x28/4] = 1000;
    CHECK(render(object) == 0);
    CHECK(frames == 1 && instances == 1 && faults == 0);
    state[0] = savedMovie; state[1] = savedInstance;
    printf("PASS ready objects reach real native RenderFlash, success preserved\n");
    releases = 0;
    RunCleanupBlock(base, parent);
    CHECK(releases == 1);
    printf("PASS fixed native cleanup: 1 release callback\n");
    base[0x35F5] = saved;
    FlushInstructionCache(GetCurrentProcess(), base + 0x35F5, 1);
    DWORD ignored; VirtualProtect(base + 0x35F5, 1, protect, &ignored);
    RemoveVectoredExceptionHandler(observer);
    printf("PASS FlashRendererFixTest\n");
    return 0;
}
