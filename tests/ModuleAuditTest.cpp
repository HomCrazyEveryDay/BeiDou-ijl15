#include "../ezorsia/ModuleAudit.h"
#include "../ezorsia/ModuleAuditCrypto.h"
#include "../out/module-audit/Key.h"
#include <cstdio>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc == 4) { ModuleAudit::ExerciseBoundsForTest(); return 0; }
    if (argc != 3) return 2;
    const std::wstring directory = argv[1];
    const std::wstring fixture = argv[2];
    {
        ModuleAuditCrypto::Writer writer;
        writer.Initialize(ModuleAuditKey::Secret, ModuleAuditKey::Id);
        writer.Open(directory + L"\\crypto.bda");
        writer.Append("{\"event\":\"session\",\"clientRunId\":\"crypto-test\",\"part\":1}");
        writer.Append("{\"event\":\"unicode\",\"value\":\"\xe6\xb5\x8b\xe8\xaf\x95.dll\",\"record\":1}");
        writer.Flush();
    }
    SetEnvironmentVariableW(L"BEIDOU_MODULE_AUDIT_SECONDS", L"5");
    ModuleAudit::Start();
    Sleep(1000);
    // Both calls occur between periodic snapshots: only notifications can see it.
    HMODULE transient = LoadLibraryW(fixture.c_str());
    if (!transient) { std::fprintf(stderr, "fixture load failed %lu\n", GetLastError()); return 3; }
    FreeLibrary(transient);
    Sleep(2000);
    HMODULE resident = LoadLibraryW(fixture.c_str());
    if (!resident) return 4;
    ModuleAudit::FailSnapshotsForTest(true);
    Sleep(6000);
    ModuleAudit::FailSnapshotsForTest(false);
    Sleep(12000);
    FreeLibrary(resident);
    Sleep(1000);
    ModuleAudit::StopForTest();
    std::puts("Module audit worker and AES writer completed.");
    return 0;
}
