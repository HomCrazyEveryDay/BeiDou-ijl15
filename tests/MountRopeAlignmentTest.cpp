// Runs the original v83 rope lookup and the production CALL bridges in an
// isolated process. No game startup, login, graphics or live process access.
#include "../ezorsia/MountRopeAlignment.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static void CopyNative(const std::vector<unsigned char>& exe, DWORD address, size_t size) {
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data());
    auto pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(exe.data() + dos->e_lfanew);
    assert(pe->OptionalHeader.ImageBase == 0x400000);
    auto section = IMAGE_FIRST_SECTION(pe);
    const DWORD rva = address - 0x400000;
    for (unsigned i = 0; i < pe->FileHeader.NumberOfSections; ++i) {
        if (rva < section[i].VirtualAddress ||
            rva + size > section[i].VirtualAddress + section[i].SizeOfRawData) continue;
        const size_t offset = section[i].PointerToRawData + rva - section[i].VirtualAddress;
        assert(offset + size <= exe.size());
        std::memcpy(reinterpret_cast<void*>(address), exe.data() + offset, size);
        return;
    }
    assert(false);
}

static void* Invoke(DWORD site, void* space, void* controller, int x1, int y1, int x2, int y2) {
    void* result = nullptr;
    DWORD before = 0, after = 0, keptEdi = 0, keptEbx = 0, keptEsi = 0, resume = 0;
    __asm {
        mov before, esp
        mov edi, controller
        mov ebx, controller
        mov esi, offset returned
        mov resume, esi
        push y2
        push x2
        push y1
        push x1
        mov ecx, space
        // Enter the CALL site without adding a second return address between
        // its arguments and the bridge. The fixture jumps back via saved ESI.
        jmp site
    returned:
        mov result, eax
        mov after, esp
        mov keptEdi, edi
        mov keptEbx, ebx
        mov keptEsi, esi
    }
    assert(before == after);
    assert(keptEdi == reinterpret_cast<DWORD>(controller));
    assert(keptEbx == reinterpret_cast<DWORD>(controller));
    assert(keptEsi == resume);
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* error) -> LONG {
        std::fprintf(stderr, "Test exception %08lX at %p (ecx=%08lX edx=%08lX)\n",
            error->ExceptionRecord->ExceptionCode, error->ExceptionRecord->ExceptionAddress,
            error->ContextRecord->Ecx, error->ContextRecord->Edx);
        return EXCEPTION_EXECUTE_HANDLER;
    });
    // Reserve the native address pages before loading the large EXE fixture;
    // otherwise its vector allocation can occupy those low addresses.
    const DWORD regions[] = {0x00450000, 0x009C0000, 0x00A40000};
    for (auto address : regions) {
        if (VirtualAlloc(reinterpret_cast<void*>(address), 0x10000,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE) != reinterpret_cast<void*>(address)) {
            MEMORY_BASIC_INFORMATION memory{};
            const DWORD error = GetLastError();
            VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory));
            std::fprintf(stderr, "Native address unavailable: %08lX error=%lu allocation=%p type=%lx state=%lx\n",
                address, error, memory.AllocationBase, memory.Type, memory.State);
            return 2;
        }
    }
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<unsigned char> exe((std::istreambuf_iterator<char>(file)), {});
    assert(exe.size() > 0x1000);
    CopyNative(exe, 0x0045297B, 12);
    CopyNative(exe, 0x004529BA, 6);
    CopyNative(exe, 0x00A45B03, 0x89); // entire original GetLadderOrRope, ending RET 10h
    using namespace MountRopeAlignment::Detail;
    for (const auto& site : sites) {
        CopyNative(exe, site.address, 5);
        *reinterpret_cast<unsigned char*>(site.address + 5) = 0xFF;
        *reinterpret_cast<unsigned char*>(site.address + 6) = 0xE6; // JMP ESI back to Invoke
    }
    // All-or-nothing preflight: a mismatch in the last site must leave the first intact.
    unsigned char original[5];
    std::memcpy(original, reinterpret_cast<void*>(sites[0].address), 5);
    *reinterpret_cast<unsigned char*>(sites[5].address) = 0x90;
    assert(!MountRopeAlignment::Install());
    assert(!std::memcmp(original, reinterpret_cast<void*>(sites[0].address), 5));
    CopyNative(exe, sites[5].address, 5);

    struct Rope { int serial, ladder, upper, x, y1, y2, page; };
    static_assert(sizeof(Rope) == 28, "v83 ladder layout");
    struct { int count = 3; Rope entries[3] = {{}, {1,0,1,100,-200,-10,1}, {2,1,1,160,-200,-10,1}}; } ropes;
    unsigned char space[0xAC]{}, controller[0x20]{}, user[0x1200]{};
    *reinterpret_cast<void**>(space + 0xA8) = ropes.entries;
    *reinterpret_cast<void**>(controller + 0x14) = user + 4;
    *reinterpret_cast<void**>(user + 0x11A4) = controller + 0xC;
    auto avatar = user + 0x88;
    auto& model = *reinterpret_cast<int*>(avatar + 0x4BC);
    auto& dx = *reinterpret_cast<int*>(avatar + 0x4D4);
    auto& origin = *reinterpret_cast<void**>(avatar + 0x10B8);
    origin = reinterpret_cast<void*>(1); model = 1902000; dx = 20;
    // Original predicate proves the reported failure: rider X=100, physics X=80.
    assert(findRope(space, 80, -60, 80, -60) == nullptr);
    assert(findRope(space, 100, -60, 100, -60) == &ropes.entries[1]);
    assert(MountRopeAlignment::Install());
    assert(MountRopeAlignment::Install());
    for (const auto& site : sites) {
        auto query = [&](int x1, int x2, int y1 = -60, int y2 = -60) {
            return Invoke(site.address, space, controller, x1, y1, x2, y2);
        };
        model = 1902000; dx = 20; // Base hog plus motorcycle cash appearance.
        assert(query(80,80) == &ropes.entries[1]);
        assert(query(140,140) == &ropes.entries[2]); // ladder shares the same predicate
        assert(query(100,100) == nullptr); // shifted, NOT a wider catch rectangle
        dx = 25; assert(query(75,75) == &ropes.entries[1]);
        dx = -20; assert(query(120,120) == &ropes.entries[1]);
        dx = -25; assert(query(125,125) == &ropes.entries[1]);
        model = 1902999; dx = 37; assert(query(63,63) == &ropes.entries[1]);
        model = 1932999; dx = 12; assert(query(88,88) == &ropes.entries[1]);
        dx = 20;
        assert(query(70,70) == &ropes.entries[1]); // existing inclusive +/-10 boundary
        assert(query(69,69) == nullptr);
        assert(query(91,91) == nullptr);
        assert(query(40,110) == &ropes.entries[1]);
        assert(query(110,40) == &ropes.entries[1]); // motion in either direction
        *reinterpret_cast<int*>(avatar + 0x4D8) = -100;
        assert(query(80,80,20,30) == nullptr); // never raise the vertical reach
        assert(query(80,80,-250,-210) == nullptr);
        model = 0;
        assert(query(80,80) == nullptr); assert(query(100,100) == &ropes.entries[1]);
        model = 3010000; assert(query(80,80) == nullptr);
        model = 1902000; dx = 0; assert(query(100,100) == &ropes.entries[1]);
        dx = 20; origin = nullptr; assert(query(80,80) == nullptr);
        origin = reinterpret_cast<void*>(1);
        *reinterpret_cast<void**>(user + 0x11A4) = nullptr;
        assert(query(80,80) == nullptr);
        *reinterpret_cast<void**>(user + 0x11A4) = controller + 0xC;
        *reinterpret_cast<void**>(controller + 0x14) = nullptr;
        assert(query(80,80) == nullptr);
        *reinterpret_cast<void**>(controller + 0x14) = user + 4;
        unsigned char userBefore[sizeof(user)], controllerBefore[sizeof(controller)];
        std::memcpy(userBefore,user,sizeof(user)); std::memcpy(controllerBefore,controller,sizeof(controller));
        assert(query(80,80) == &ropes.entries[1]);
        assert(!std::memcmp(userBefore,user,sizeof(user)) && !std::memcmp(controllerBefore,controller,sizeof(controller)));
    }
    puts("PASS: original v83 lookup reproduces 20px miss; six installed CALL bridges align both motorcycles, flips, future IDs, rope/ladder and moving segments");
    puts("PASS: +/-10 boundary and Y unchanged; ordinary/unready/foreign owners unchanged; no state mutation; registers/stack balanced; signature preflight and idempotence");
    for (auto address : regions) VirtualFree(reinterpret_cast<void*>(address), 0, MEM_RELEASE);
}
