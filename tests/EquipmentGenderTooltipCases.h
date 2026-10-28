#pragma once

static DWORD genderTitleSite = 0x008EC75A;
// Jump into the actual native CALL with its real stack layout. The isolated
// mapping's continuation pops the caller-owned item ID and returns here.
__declspec(naked) static int RunTitleGender(int, const void*) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ebx, 11223344h
        mov esi, 55667788h
        mov edi, [ebp + 12]
        push offset resume
        push dword ptr [ebp + 8]
        jmp dword ptr [genderTitleSite]
    resume:
        cmp ebx, 11223344h
        jne bad
        cmp esi, 55667788h
        jne bad
        cmp edi, [ebp + 12]
        jne bad
        cmp ecx, [ebp + 8]
        jne bad
        lea edx, [ebp - 12]
        cmp esp, edx
        je done
    bad:
        mov eax, -99
    done:
        lea esp, [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

static int TitleGenderForName(int item, const char* name) {
    void* equipEntry[5]{};
    equipEntry[4] = const_cast<char*>(name);
    return RunTitleGender(item, equipEntry);
}

static void CheckEquipmentGenderTooltip() {
    using namespace EquipmentGenderTooltip;
    auto site = reinterpret_cast<BYTE*>(genderTitleSite);
    BYTE original[11]; memcpy(original, site, sizeof(original));
    const char* male = "Pumpkin(\xC4\xD0)";
    const char* female = "Pumpkin(\xC5\xAE)";
    // Reproduce the native decision to append a second gender to these names.
    site[5] = 0x59; site[6] = 0xc3;
    Require(TitleGenderForName(1050476, male) == 0 && TitleGenderForName(1051543, female) == 1,
        "original native title predicate duplicates explicit resource labels");
    memcpy(site, original, sizeof(original));
    site[0] ^= 1;
    Require(!Install(), "unknown title call signature rejected");
    memcpy(site, original, sizeof(original));
    DWORD ignored;
    Require(VirtualProtect(site, 5, PAGE_EXECUTE_READ, &ignored) != FALSE, "read-only patch page");
    protectionCalls = 0; deniedProtectionCall = 1;
    Require(!Install() && !memcmp(site, original, sizeof(original)), "denied title patch leaves native code intact");
    deniedProtectionCall = 0;
    Require(Install() && !Install(), "title patch installs exactly once");
    MEMORY_BASIC_INFORMATION page{};
    Require(VirtualQuery(site, &page, sizeof(page)) && page.Protect == PAGE_EXECUTE_READ,
        "title patch restores original page protection");
    Require(!memcmp(site + 5, original + 5, 6), "native branch and caller cleanup unchanged");
    Require(genderTitleSite + 5 + *reinterpret_cast<DWORD*>(site + 1) == reinterpret_cast<DWORD>(TitleGender),
        "native call targets production bridge");
    Require(VirtualProtect(site, 11, PAGE_EXECUTE_READWRITE, &ignored) != FALSE, "test continuation access");
    site[5] = 0x59; site[6] = 0xc3;
    struct Case { int item; const char* name; int expected; };
    const Case cases[] = {
        {1050476, male, 2}, {1051543, female, 2},
        {1000096, "Wig(\xC4\xD0\xA3\xA9", 2},
        {1001119, "Wig(\xC5\xAE\xA3\xA9", 2},
        {1050504, "Uniform\xA3\xA8\xC4\xD0\xA3\xA9", 2},
        {1051574, "Uniform\xA3\xA8\xC5\xAE\xA3\xA9", 2},
        {1050476, "Pumpkin(\xE7\x94\xB7)", 2},
        {1051543, "Pumpkin\xEF\xBC\x88\xE5\xA5\xB3\xEF\xBC\x89", 2},
        {1050476, "Pumpkin\xEF\xBC\x88\xE7\x94\xB7) ", 2},
        {1050476, "Pumpkin( \xC4\xD0 \xA3\xA9 \t", 2},
        {1051543, "Pumpkin(\xC5\xAE)\xA1\xA1", 2},
        {1050476, "Pumpkin(Male)", 2}, {1051543, "Pumpkin(Female)", 2},
        {1050476, "Pumpkin", 0}, {1051543, "Pumpkin", 1},
        {1050476, female, 0}, {1051543, male, 1},
        {1050476, "Witch", 0}, {1051543, "\xC5\xAE", 1},
        {1050476, "Pumpkin(\xC4\xD0) special", 0},
        {1051543, "Pumpkin\xC5\xAE)", 1},
        {1050476, "Pumpkin(\xC4\xD0", 0},
        {1050476, "(\xC4\xD0)", 0}, {1051543, " ", 1},
        {1050476, nullptr, 0}, {1051543, "", 1},
        {1005065, "Hair(short)", 2}, {1053869, male, 2}, {3019686, male, 2}
    };
    for (int repeat = 0; repeat < 32; ++repeat) {
        for (const auto& c : cases) {
            SetLastError(9876);
            Require(TitleGenderForName(c.item, c.name) == c.expected, "title gender and x86 stack/register contract");
            Require(GetLastError() == 9876, "title predicate keeps last-error state");
        }
    }
    Require(nativeGender(1050476) == 0 && nativeGender(1051543) == 1 && nativeGender(1005065) == 2,
        "shared wearing/preview gender predicate remains original");
    std::puts("PASS: native duplicate reproduced; 29 title cases x32; bracket encodings, no-label/conflict/unisex, call signature and x86 ABI");
}

static void CheckEquipmentGenderResourceNames() {
    using namespace ModernEquipmentCompatibility;
    const GUID propertyId = {0x986515d9,0x0a0b,0x4929,{0x8b,0x4f,0x71,0x86,0x82,0x17,0x7b,0x92}};
    struct Entry { const wchar_t* category; int id; };
    const Entry entries[] = {
        {L"Cap",1001119}, {L"Cap",1000096},
        {L"Coat",1040027}, {L"Coat",1040137}, {L"Coat",1040138},
        {L"Coat",1041104}, {L"Coat",1041138}, {L"Coat",1041139},
        {L"Longcoat",1050120}, {L"Longcoat",1051112}, {L"Longcoat",1051128},
        {L"Longcoat",1051170}, {L"Longcoat",1051574}, {L"Longcoat",1050504},
        {L"Longcoat",1051543}, {L"Longcoat",1050476}
    };
    for (const auto& entry : entries) {
        wchar_t path[128]; swprintf_s(path, L"String/Eqp.img/Eqp/%s/%d", entry.category, entry.id);
        void* property = LoadInterface(path, propertyId);
        Require(property != nullptr, "actual equipment name node exists");
        BSTR key = SysAllocString(L"name");
        VARIANT value{};
        using GetItem = HRESULT(__stdcall*)(void*, BSTR, VARIANT*);
        Require(SUCCEEDED(Method<GetItem>(property, 0x14)(property, key, &value))
            && value.vt == VT_BSTR && value.bstrVal, "read actual IMG name");
        char name[256]{};
        Require(WideCharToMultiByte(936, 0, value.bstrVal, -1, name, sizeof(name), nullptr, nullptr) > 0,
            "convert resource name to client GBK");
        Require(TitleGenderForName(entry.id, name) == 2, "actual current and historical item names avoid duplicate gender");
        VariantClear(&value); SysFreeString(key); static_cast<IUnknown*>(property)->Release();
    }
    std::puts("PASS: all 16 actual client IMG names (4 November + 12 historical) suppress a repeated gender suffix");
}
