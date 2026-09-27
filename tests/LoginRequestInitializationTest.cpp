#include <Windows.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include "LoginRequestUnderTest.h"

static int calls;
static bool fail;
static void* __fastcall NativeConstructor(void* self, void*) {
    ++calls;
    if (fail) throw std::runtime_error("constructor failure");
    // Model native writes away from the two omitted fields, and a distinct
    // return value to verify that the wrapper preserves the native ABI result.
    static_cast<unsigned char*>(self)[0x168] = 0;
    return static_cast<unsigned char*>(self) + 4;
}
int main() {
    g_constructLogin = reinterpret_cast<ConstructLogin>(&NativeConstructor);
    for (unsigned char fill : {0, 1, 0xA5, 0xFF}) {
        alignas(DWORD) std::array<unsigned char, 0x28C> object;
        object.fill(fill);
        auto expected = object;
        expected[0x168] = 0;
        std::memset(expected.data() + 0x238, 0, 8);
        if (ConstructLoginWithCleanRequest(object.data(), nullptr) != object.data()+4
            || object != expected) return 1;
        // Subsequent legitimate requests must survive: there is no per-frame
        // reset or changes to stage transitions / appearance rendering.
        *reinterpret_cast<DWORD*>(object.data()+0x238) = 1;
        *reinterpret_cast<DWORD*>(object.data()+0x23C) = 1;
    }
    alignas(DWORD) std::array<unsigned char, 0x28C> object;
    object.fill(0xA5);const auto before=object;fail=true;
    try { ConstructLoginWithCleanRequest(object.data(),nullptr);return 2; }
    catch (const std::runtime_error&) {}
    if (object != before || calls != 5) return 3;
    std::puts("PASS: dirty/zero memory, exactly two fields reset, native result and exception preserved");
}
