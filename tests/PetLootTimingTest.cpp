#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cassert>
class Memory { public: static bool SetHook(bool, void**, void*) { return false; } };
#include "../ezorsia/PetLootTiming.h"
namespace ClientLog { void Append(Component, const char*, ...) {} }
static int propertyCalls, scanCalls, updateCalls;
static bool throwScan, throwProperty;
static int dropCalls;
static bool throwDrop;
static void __fastcall DropStub(void* pool, void*, int time) {
    assert(pool == reinterpret_cast<void*>(9) && time == 12345);
    ++dropCalls;
    if (throwDrop) RaiseException(0xE0000123, 0, 0, nullptr);
}
static int __fastcall PropertyStub(void* object, void*, int id) {
    assert(object == reinterpret_cast<void*>(42));
    ++propertyCalls;
    if (throwProperty) RaiseException(0xE0000123, 0, 0, nullptr);
    return id + 7;
}
static void __fastcall ScanStub(void* pool, void*, void* pet, void* position) {
    assert(pool == reinterpret_cast<void*>(1) && pet == reinterpret_cast<void*>(2));
    assert(position == reinterpret_cast<void*>(3));
    ++scanCalls;
    assert(PetLootTiming::PropertyHook(reinterpret_cast<void*>(42), nullptr, 123) == 130);
    if (throwScan) RaiseException(0xE0000123, 0, 0, nullptr);
}
static void __fastcall UpdateStub(void* pet, void*) {
    assert(pet == reinterpret_cast<void*>(2));
    ++updateCalls;
    PetLootTiming::ScanHook(reinterpret_cast<void*>(1), nullptr, pet, reinterpret_cast<void*>(3));
}
int main() {
    using namespace PetLootTiming;
    property = reinterpret_cast<Property>(PropertyStub);
    scan = reinterpret_cast<Scan>(ScanStub);
    update = reinterpret_cast<Update>(UpdateStub);
    dropUpdate = reinterpret_cast<DropUpdate>(DropStub);
    QueryPerformanceFrequency(&frequency);
    cacheEnabled = false;
    active = 1;
    PropertyHook(reinterpret_cast<void*>(42), nullptr, 123);
    assert(properties.calls == 0); // unrelated item lookups excluded
    assert(outsideProperties.calls == 1);
    UpdateHook(reinterpret_cast<void*>(2), nullptr);
    assert(updates.calls == 1 && scans.calls == 1 && properties.calls == 1);
    assert(updateCalls == 1 && scanCalls == 1 && propertyCalls == 2);
    assert(scanDepth == 0);
    throwScan = true;
    bool caught = false;
    __try { UpdateHook(reinterpret_cast<void*>(2), nullptr); }
    __except(GetExceptionCode() == 0xE0000123 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    assert(caught && scanDepth == 0 && scans.calls == 2 && updates.calls == 2);
    throwScan = false;
    active = 0;
    UpdateHook(reinterpret_cast<void*>(2), nullptr);
    assert(scans.calls == 2 && updates.calls == 2 && properties.calls == 2);
    assert(updateCalls == 3 && scanCalls == 3 && propertyCalls == 4);
    Emit("test", scans);
    assert(scans.calls == 0 && scans.ticks == 0 && scans.maximum == 0);
    cacheEnabled = true;
    const int before = propertyCalls;
    UpdateHook(reinterpret_cast<void*>(2), nullptr);
    UpdateHook(reinterpret_cast<void*>(2), nullptr);
    assert(propertyCalls == before + 1); // cache works after timing stops
    PropertyHook(reinterpret_cast<void*>(42), nullptr, 123);
    assert(propertyCalls == before + 1); // outside-scan callers share immutable property cache
    scanDepth = 1;
    assert(PropertyHook(reinterpret_cast<void*>(42), nullptr, 4219) == 4226);
    assert(PropertyHook(reinterpret_cast<void*>(42), nullptr, 123) == 130); // collision cannot return wrong value
    const int afterCollision = propertyCalls;
    assert(PropertyHook(reinterpret_cast<void*>(42), nullptr, 123) == 130);
    assert(propertyCalls == afterCollision);
    throwProperty = true;
    caught = false;
    __try { PropertyHook(reinterpret_cast<void*>(42), nullptr, 456); }
    __except(GetExceptionCode() == 0xE0000123 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    assert(caught);
    throwProperty = false;
    const int beforeRetry = propertyCalls;
    assert(PropertyHook(reinterpret_cast<void*>(42), nullptr, 456) == 463);
    assert(propertyCalls == beforeRetry + 1); // exceptions never cached
    scanDepth = 0;
    cacheEnabled = false;
    UpdateHook(reinterpret_cast<void*>(2), nullptr);
    assert(propertyCalls == beforeRetry + 2);
    DropUpdateHook(reinterpret_cast<void*>(9), nullptr, 12345);
    assert(dropCalls == 1 && dropUpdates.calls == 0 && dropGaps.calls == 0);
    active = 1;
    DropUpdateHook(reinterpret_cast<void*>(9), nullptr, 12345);
    DropUpdateHook(reinterpret_cast<void*>(9), nullptr, 12345);
    assert(dropCalls == 3 && dropUpdates.calls == 2 && dropGaps.calls == 1);
    throwDrop = true;
    caught = false;
    __try { DropUpdateHook(reinterpret_cast<void*>(9), nullptr, 12345); }
    __except(GetExceptionCode() == 0xE0000123 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    assert(caught && dropUpdates.calls == 3 && dropGaps.calls == 2);
    throwProperty = true;
    caught = false;
    __try { PropertyHook(reinterpret_cast<void*>(42), nullptr, 789); }
    __except(GetExceptionCode() == 0xE0000123 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    assert(caught && outsideProperties.calls == 2);
    puts("PASS: cache reuse, collisions, exception retry, cache disable, outside-scan cache reuse;  ABI arguments/results, original calls, scoped attribution, exception propagation, disabled pass-through, counter reset");
}
