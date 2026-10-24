#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace TestSupport {
inline void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
inline DWORD Address(DWORD address) { return address + 0x10000000; }
template<typename Value> void Set(void* object, size_t offset, Value value) {
    *reinterpret_cast<Value*>(static_cast<unsigned char*>(object) + offset) = value;
}
inline void Jump(DWORD address, void* target) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0xE9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(target) - address - 5;
}
inline void Bytes(DWORD address, const char* expected, size_t size) {
    if (std::memcmp(reinterpret_cast<void*>(Address(address)), expected, size)) {
        std::fprintf(stderr, "Signature address=%08lX\n", address);
        Require(false, "original EXE signature");
    }
}
inline HMODULE LoadImage(const char* filename) {
    FILE* file = nullptr;
    Require(fopen_s(&file, filename, "rb") == 0, "open original EXE without executing it");
    std::fseek(file, 0, SEEK_END);
    std::vector<unsigned char> bytes(std::ftell(file));
    std::rewind(file);
    Require(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read EXE");
    std::fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(image->OptionalHeader.ImageBase == 0x400000, "fixed original base");
    auto* mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(Address(0x400000)),
        image->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped == reinterpret_cast<void*>(Address(0x400000)), "isolated test image base");
    std::memcpy(mapped, bytes.data(), image->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(image);
    for (unsigned index = 0; index < image->FileHeader.NumberOfSections; ++index)
        std::memcpy(mapped + sections[index].VirtualAddress, bytes.data() + sections[index].PointerToRawData,
            sections[index].SizeOfRawData);
    return reinterpret_cast<HMODULE>(mapped);
}
struct Registers { DWORD eax, ebx, ecx, edx, esi, edi, ebp, flags; };
__declspec(naked) inline void Invoke(void* gate, Registers* state) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        push dword ptr [ebp+0ch]
        push ebp
        push dword ptr [ebp+8]
        mov eax, [ebp+0ch]
        push dword ptr [eax+1ch]
        popfd
        mov ebx, [eax+4]
        mov ecx, [eax+8]
        mov edx, [eax+0ch]
        mov esi, [eax+10h]
        mov edi, [eax+14h]
        mov ebp, [eax+18h]
        mov eax, [eax]
        call dword ptr [esp]
        pushfd
        pushad
        mov eax, [esp+2ch]
        mov edx, [esp+1ch]
        mov [eax], edx
        mov edx, [esp+10h]
        mov [eax+4], edx
        mov edx, [esp+18h]
        mov [eax+8], edx
        mov edx, [esp+14h]
        mov [eax+0ch], edx
        mov edx, [esp+4]
        mov [eax+10h], edx
        mov edx, [esp]
        mov [eax+14h], edx
        mov edx, [esp+8]
        mov [eax+18h], edx
        mov edx, [esp+20h]
        mov [eax+1ch], edx
        popad
        popfd
        add esp, 4
        pop ebp
        add esp, 4
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
inline Registers RegistersFor(int* frame) {
    return {0x1111,0x2222,0x3333,0x4444,0x5555,0x6666,reinterpret_cast<DWORD>(frame),0x202};
}
inline int reached = 0;
__declspec(naked) inline void Accepted() {
    __asm {
        mov dword ptr [reached], 1
        ret
    }
}
__declspec(naked) inline void Fallback() {
    __asm {
        mov dword ptr [reached], 2
        ret
    }
}
}
