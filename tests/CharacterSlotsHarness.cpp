// Build as x86 with /BASE:0x20000000 /DYNAMICBASE:NO. This maps a copy of the
// reference executable as data; it never starts or logs into the game.
#define CHARACTER_SLOTS_TEST
#include "../ezorsia/CharacterSlots.cpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace ClientLog {
void Append(Component, const char*, ...) {}
}

static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static void Save(const std::string& path, const void* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), size);
    Require(out.good(), "snapshot write failed");
}
int main(int argc, char** argv) {
    Require(argc == 3, "usage: harness BeiDou.exe output-directory");
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<char> file((std::istreambuf_iterator<char>(in)), {});
    Require(file.size() > 4096, "reference executable missing");
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(file.data() + dos->e_lfanew);
    Require(nt->OptionalHeader.ImageBase == 0x400000, "unexpected image base");
    auto base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x400000)),
        nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(base == reinterpret_cast<BYTE*>(Native(0x400000)), "cannot map test image");
    std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        std::memcpy(base + section[i].VirtualAddress, file.data() + section[i].PointerToRawData,
            section[i].SizeOfRawData);
    }
    auto patches = BuildPatches();
    // A bad site at the END of validation must leave every earlier site intact.
    auto bad = reinterpret_cast<BYTE*>(patches.back().address);
    *bad ^= 1;
    Require(!CharacterSlots::Install(), "mismatch accepted");
    *bad ^= 1;
    for (const auto& patch : patches) {
        Require(!std::memcmp(reinterpret_cast<void*>(patch.address), patch.before.data(), patch.before.size()),
            "failed install modified another site");
    }
    Require(CharacterSlots::Install(), "install failed on original executable");
    Require(CharacterSlots::Install(), "repeat install failed");
    for (const auto& patch : patches) {
        Require(!std::memcmp(reinterpret_cast<void*>(patch.address), patch.after.data(), patch.after.size()),
            "installed bytes differ");
    }
    const std::string directory = argv[2];
    Save(directory + "/native.bin", base, nt->OptionalHeader.SizeOfImage);
    auto own = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto ownDos = reinterpret_cast<IMAGE_DOS_HEADER*>(own);
    auto ownNt = reinterpret_cast<IMAGE_NT_HEADERS*>(own + ownDos->e_lfanew);
    std::vector<BYTE> snapshot(ownNt->OptionalHeader.SizeOfImage);
    for (size_t offset = 0; offset < snapshot.size();) {
        MEMORY_BASIC_INFORMATION region{};
        Require(VirtualQuery(own + offset, &region, sizeof(region)) != 0, "VirtualQuery failed");
        const size_t end = (std::min<size_t>)(snapshot.size(), offset + region.RegionSize);
        if (region.State == MEM_COMMIT && !(region.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            std::memcpy(snapshot.data() + offset, own + offset, end - offset);
        offset = end;
    }
    Save(directory + "/hooks.bin", snapshot.data(), snapshot.size());
    std::ofstream meta(directory + "/snapshot.json");
    meta << "{\"nativeBase\":" << reinterpret_cast<DWORD>(base)
         << ",\"hookBase\":" << reinterpret_cast<DWORD>(own)
         << ",\"records\":" << reinterpret_cast<DWORD>(g_records)
         << ",\"patchCount\":" << patches.size() << "}";
    std::puts("PASS: all reference bytes match; mismatch changes nothing; complete and repeat install succeed");
}
