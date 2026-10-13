#include "stdafx.h"
#include "ResourceLookupDiagnostics.h"
#include "ClientDiagnostics.h"
#include "ClientLog.h"
#include <oleauto.h>
#include <cstring>
#include <cstdio>

namespace ResourceLookupDiagnostics {
namespace {
constexpr LONG kMaxReports = 8;
constexpr unsigned kMaxNodes = 24, kMaxBuckets = 64;
enum Kind : DWORD { Property, Resource, CanvasInterface };
volatile LONG reports = 0, busy = 0;
DWORD seenCaller[kMaxReports]{}, seenKind[kMaxReports]{};
bool installed = false, installationReady = false;
DWORD propertySuccess = 0x00403980, propertyFailure = 0x00403979;
DWORD resourceSuccess = 0x00403AF7, resourceFailure = 0x00403AF0;
DWORD canvasSuccess = 0x0041E55C, canvasFailure = 0x0041E54B;
DWORD pcomBase = 0;

bool Read(DWORD address, void* output, SIZE_T size) {
    SIZE_T copied = 0;
    return address && address <= MAXDWORD - size
        && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, size, &copied)
        && copied == size;
}
bool Word(DWORD address, DWORD& value) { return Read(address, &value, sizeof(value)); }

// Bounded text, safe for one log line. No SysStringLen on an untrusted pointer.
bool Name(DWORD address, char (&text)[160]) {
    if (!address) { strcpy_s(text, "<null>"); return true; }
    if (address > MAXDWORD - 128) { strcpy_s(text, "<invalid>"); return false; }
    unsigned used = 0;
    for (unsigned i = 0; i < 64 && used + 7 < sizeof(text); ++i) {
        wchar_t c = 0;
        if (!Read(address + i * 2, &c, sizeof(c))) {
            strncpy_s(text + used, sizeof(text) - used, "<unreadable>", _TRUNCATE); return false;
        }
        if (!c) { text[used] = 0; return true; }
        if (c >= 0x21 && c <= 0x7e && c != '\\' && c != '"') text[used++] = static_cast<char>(c);
        else used += sprintf_s(text + used, sizeof(text) - used, "\\u%04X", unsigned(c));
    }
    strcpy_s(text + used, sizeof(text) - used, "..."); return false;
}

struct Node {
    DWORD address = 0, entry = 0, object = 0, vtable = 0, lazy = 0;
    VARIANT value{};
    bool readable = false, nameRead = false;
    char name[160]{};
};
struct Sample {
    Kind kind{};
    DWORD caller = 0, outerCaller = 0, owner = 0, ownerVtable = 0, source = 0, sourceVtable = 0;
    DWORD output = 0, outputVtable = 0, keyAddress = 0;
    HRESULT hr = S_OK;
    VARIANT value{};
    bool valueRead = false, nameRead = false, knownProperty = false, complete = false;
    DWORD propertyCount = 0, bucketCount = 0, dictionary = 0, nodeCount = 0;
    char name[160]{};
    Node nodes[kMaxNodes]{};
};
DWORD KnownPcom() {
    const DWORD pc = reinterpret_cast<DWORD>(GetModuleHandleW(L"PCOM.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{};
    return pc && Read(pc, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE
        && dos.e_lfanew >= 0x40 && dos.e_lfanew <= 0x1000
        && Read(pc + dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE
        && nt.FileHeader.TimeDateStamp == 0x4b7c140c && nt.OptionalHeader.SizeOfImage == 0x25000 ? pc : 0;
}
DWORD Object(const VARIANT& value) {
    DWORD pointer = 0;
    if (value.vt == VT_UNKNOWN || value.vt == VT_DISPATCH) return reinterpret_cast<DWORD>(value.punkVal);
    if (value.vt == (VT_BYREF | VT_UNKNOWN) || value.vt == (VT_BYREF | VT_DISPATCH))
        Word(reinterpret_cast<DWORD>(value.byref), pointer);
    return pointer;
}

void CaptureProperty(Sample& sample) {
    // Interpret layout only for the verified original PCOM WzProperty class.
    // No COM calls, AddRef, enumeration, or resource loading: preserve first failure.
    if (!pcomBase) pcomBase = KnownPcom(); // PCOM may load after hook installation.
    if (!pcomBase || sample.ownerVtable != pcomBase + 0x131c4) return;
    DWORD header[20]{};
    if (!Read(sample.owner, header, sizeof(header))) return;
    sample.knownProperty = true;
    sample.dictionary = header[10]; sample.bucketCount = header[11]; sample.propertyCount = header[12];
    if (!sample.dictionary || sample.bucketCount > kMaxBuckets || !sample.bucketCount) return;
    DWORD buckets[kMaxBuckets]{};
    if (!Read(sample.dictionary, buckets, sample.bucketCount * sizeof(DWORD))) return;
    bool readable = true;
    for (DWORD bucket = 0; bucket < sample.bucketCount; ++bucket) {
        DWORD next = buckets[bucket];
        while (next) {
            if (sample.nodeCount == kMaxNodes) return;
            for (DWORD i = 0; i < sample.nodeCount; ++i) if (sample.nodes[i].address == next) return;
            auto& node = sample.nodes[sample.nodeCount++]; node.address = next;
            DWORD data[5]{}, key = 0;
            if (!Read(next, data, sizeof(data))) return;
            next = data[1]; node.entry = data[4];
            if (Word(data[2], key)) node.nameRead = Name(key, node.name);
            node.readable = node.entry && node.entry <= MAXDWORD - 0x24
                && Read(node.entry + 0x10, &node.value, sizeof(node.value))
                && Word(node.entry + 0x20, node.lazy);
            if (node.readable) { node.object = Object(node.value); Word(node.object, node.vtable); }
            readable = readable && node.readable && node.nameRead;
        }
    }
    sample.complete = readable && sample.nodeCount == sample.propertyCount;
}

void Capture(Sample& sample, DWORD frame, DWORD owner) {
    sample.owner = owner;
    bool keyKnown = false;
    if (sample.kind != CanvasInterface) {
        DWORD holder = 0;
        Word(frame + 4, sample.caller);
        if (Word(frame + 0xc, holder)) keyKnown = !holder || Word(holder, sample.keyAddress);
        sample.valueRead = frame >= 0x20 && Read(frame - 0x20, &sample.value, sizeof(sample.value));
        if (sample.valueRead) sample.output = Object(sample.value);
    } else {
        DWORD directCaller = 0, outerFrame = 0;
        Word(frame + 4, directCaller); Word(frame, outerFrame);
        if (directCaller == 0x00428DE1) {
            // The intermediate canvas smart-pointer constructor has no EBP frame.
            DWORD sourceHolder = 0;
            Word(frame + 0x10, sample.caller);
            if (Word(frame + 0x14, sourceHolder)) Word(sourceHolder, sample.source);
            if (outerFrame >= 0x54) {
                sample.valueRead = Read(outerFrame - 0x54, &sample.value, sizeof(sample.value));
                Word(outerFrame - 0x2c, sample.owner);
            }
            strcpy_s(sample.name, "<canvas-conversion;see-property-query>");
        } else if (directCaller == 0x004381C4) {
            sample.caller = directCaller;
            if (outerFrame >= 0x8c) {
                sample.valueRead = Read(outerFrame - 0x70, &sample.value, sizeof(sample.value));
                Word(outerFrame - 0x8c, sample.source);
                Word(outerFrame - 0x18, sample.owner);
            }
            strcpy_s(sample.name, "<critical-effect;see-property-query>");
        } else if (directCaller >= 0x005336CA && directCaller < 0x00534A1E) {
            // Boss QI call sites have different local layouts. Do not guess a
            // VARIANT/source slot; the preceding GetObject observer has the key.
            sample.caller = directCaller; sample.owner = 0;
            strcpy_s(sample.name, "<boss-canvas-conversion;see-resource-query>");
        } else {
            sample.caller = directCaller;
            if (outerFrame >= 0x108) {
                sample.valueRead = Read(outerFrame - 0x108, &sample.value, sizeof(sample.value));
                Word(outerFrame - 0x14, sample.source);
            }
            DWORD listEntry = 0;
            if (Word(frame - 8, listEntry) && listEntry <= MAXDWORD - 8)
                keyKnown = Word(listEntry + 8, sample.keyAddress); // list entry owns ZXString<wchar_t>
            sample.owner = listEntry;
        }
        // On a failed QI its output slot is unspecified, so do not dereference it.
        if (SUCCEEDED(sample.hr)) Word(frame + 8, sample.output);
    }
    if (keyKnown) sample.nameRead = Name(sample.keyAddress, sample.name);
    else if (!sample.name[0]) strcpy_s(sample.name, "<unreadable-key>");
    DWORD outerFrame = 0;
    if (Word(frame, outerFrame) && outerFrame <= MAXDWORD - 4) Word(outerFrame + 4, sample.outerCaller);
    Word(sample.owner, sample.ownerVtable); Word(sample.source, sample.sourceVtable); Word(sample.output, sample.outputVtable);
    if (sample.kind == Property) CaptureProperty(sample);
}

void Write(const Sample& s, LONG index) {
    ClientDiagnostics::Snapshot identity;
    ClientDiagnostics::TrySnapshot(identity);
    ClientLog::EmergencyBatch batch;
    ClientLog::Emergency("resource_lookup report=%ld stage=%s caller=%08lX outerCaller=%08lX hr=%08lX vtKnown=%d vt=%04X keyRead=%d key=\"%s\" keyPtr=%08lX owner=%08lX ownerVtable=%08lX source=%08lX sourceVtable=%08lX output=%08lX outputVtable=%08lX clientRunId=%s connectionId=%s action=observe_only",
        index, s.kind == Property ? "property" : s.kind == Resource ? "resource" : "canvas_qi",
        s.caller, s.outerCaller, static_cast<DWORD>(s.hr), s.valueRead, unsigned(s.value.vt), s.nameRead,
        s.name, s.keyAddress, s.owner, s.ownerVtable, s.source, s.sourceVtable, s.output, s.outputVtable,
        identity.clientRunId, identity.connectionId);
    if (!s.knownProperty) return;
    ClientLog::Emergency("resource_property report=%ld pcomBase=%08lX count=%lu buckets=%lu dictionary=%08lX captured=%lu complete=%d snapshot=best_effort",
        index, pcomBase, s.propertyCount, s.bucketCount, s.dictionary, s.nodeCount, s.complete);
    for (DWORD i = 0; i < s.nodeCount; ++i) {
        const auto& n = s.nodes[i];
        ClientLog::Emergency("resource_child report=%ld index=%lu nameRead=%d name=\"%s\" node=%08lX entry=%08lX valueRead=%d vt=%04X object=%08lX vtable=%08lX lazy=%08lX",
            index, i, n.nameRead, n.name, n.address, n.entry, n.readable, unsigned(n.value.vt), n.object, n.vtable, n.lazy);
    }
}

void __cdecl Observe(DWORD kind, DWORD frame, DWORD owner, HRESULT hr) {
    const DWORD error = GetLastError();
    if (InterlockedCompareExchange(&busy, 1, 0)) { SetLastError(error); return; }
    __try {
        __try {
            DWORD caller = 0;
            if (!Word(frame + 4, caller)) __leave;
            if (kind == CanvasInterface && caller == 0x00428DE1 && !Word(frame + 0x10, caller)) __leave;
            const LONG count = reports;
            if (count >= kMaxReports) __leave;
            for (LONG i = 0; i < count; ++i) if (seenKind[i] == kind && seenCaller[i] == caller) __leave;
            seenKind[count] = kind; seenCaller[count] = caller;
            const LONG index = InterlockedIncrement(&reports);
            Sample sample{}; sample.kind = static_cast<Kind>(kind); sample.hr = hr;
            Capture(sample, frame, owner); Write(sample, index);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // A diagnostic failure must never change the observed native result.
        }
    } __finally { InterlockedExchange(&busy, 0); SetLastError(error); }
}

// These are post-COM-call observers, before _bstr_t/VARIANT cleanup. On ordinary
// calls only integer comparisons run: no C++, clock, atomics, allocations or I/O.
// The abnormal branch saves x87/SSE as well as GPRs before invoking any logger.
__declspec(naked) void PropertyReturned() {
    __asm {
        cmp reports, 8
        jge resume
        push ecx
        mov ecx, [ebp + 4]
        cmp ecx, 0043819Dh
        je watched
        cmp ecx, 008DA000h
        jb digit
        cmp ecx, 008DB000h
        jb watched
    digit:
        cmp ecx, 009881A0h
        jne leaveFast
    watched:
        test eax, eax
        js abnormal
        movzx ecx, word ptr [ebp - 20h]
        and ecx, 0BFFFh
        cmp ecx, 13
        je objectValue
        cmp ecx, 9
        jne abnormal
    objectValue:
        cmp dword ptr [ebp - 18h], 0
        jne leaveFast
    abnormal:
        pop ecx
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push eax
        push esi
        push ebp
        push 0
        call Observe
        add esp, 16
        fxrstor [esp]
        mov esp, ebx
        popad
        jmp resume
    leaveFast:
        pop ecx
    resume:
        test eax, eax
        jge success
        push 00BD8308h
        jmp dword ptr [propertyFailure]
    success:
        jmp dword ptr [propertySuccess]
    }
}

__declspec(naked) void ResourceReturned() {
    __asm {
        cmp reports, 8
        jge resume
        push ecx
        mov ecx, [ebp + 4]
        cmp ecx, 0066B37Dh
        jne objectScope
        // CMob::AddDamage reads /hitAfter as a scalar. Only failed HRESULTs
        // need evidence here; an integer or optional empty value is valid.
        test eax, eax
        js abnormal
        jmp leaveFast
    objectScope:
        cmp ecx, 005336CAh
        jb existingScope
        cmp ecx, 00534A1Eh
        jb watched
    existingScope:
        cmp ecx, 004B7CF0h
        je watched
        cmp ecx, 008DA000h
        jb leaveFast
        cmp ecx, 008DB000h
        jae leaveFast
    watched:
        test eax, eax
        js abnormal
        movzx ecx, word ptr [ebp - 20h]
        and ecx, 0BFFFh
        cmp ecx, 13
        je objectValue
        cmp ecx, 9
        jne abnormal
    objectValue:
        cmp dword ptr [ebp - 18h], 0
        jne leaveFast
    abnormal:
        pop ecx
        pushad
        mov edi, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push eax
        push ebx
        push ebp
        push 1
        call Observe
        add esp, 16
        fxrstor [esp]
        mov esp, edi
        popad
        jmp resume
    leaveFast:
        pop ecx
    resume:
        test eax, eax
        jge success
        push 00BD8318h
        jmp dword ptr [resourceFailure]
    success:
        jmp dword ptr [resourceSuccess]
    }
}

__declspec(naked) void CanvasReturned() {
    __asm {
        cmp reports, 8
        jge resume
        push ecx
        mov ecx, [ebp + 4]
        cmp ecx, 004381C4h
        je watched
        cmp ecx, 005336CAh
        jb existingScope
        cmp ecx, 00534A1Eh
        jb watched
    existingScope:
        cmp ecx, 004B7D11h
        je watched
        cmp ecx, 00428DE1h
        jne leaveFast
        mov ecx, [ebp + 10h]
        cmp ecx, 008DA000h
        jb leaveFast
        cmp ecx, 008DB000h
        jae leaveFast
    watched:
        test eax, eax
        js abnormal
        cmp dword ptr [ebp + 8], 0
        jne leaveFast
    abnormal:
        pop ecx
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push eax
        push esi
        push ebp
        push 2
        call Observe
        add esp, 16
        fxrstor [esp]
        mov esp, ebx
        popad
        jmp resume
    leaveFast:
        pop ecx
    resume:
        mov ebx, eax
        test ebx, ebx
        jge success
        jmp dword ptr [canvasFailure]
    success:
        jmp dword ptr [canvasSuccess]
    }
}

struct Patch { DWORD address; BYTE expected[9]; SIZE_T size; void* target; };
Patch patches[] = {
    {0x00403970, {0x85,0xc0,0x7d,0x0c,0x68,0x08,0x83,0xbd,0}, 9, PropertyReturned},
    {0x00403AE7, {0x85,0xc0,0x7d,0x0c,0x68,0x18,0x83,0xbd,0}, 9, ResourceReturned},
    {0x0041E545, {0x8b,0xd8,0x85,0xdb,0x7d,0x11}, 6, CanvasReturned},
};
}

bool Install(HMODULE client, bool enabled) {
    if (!enabled) return true;
    if (installed) return installationReady;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{};
    const DWORD base = reinterpret_cast<DWORD>(client);
    if (base != 0x00400000 || !Read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew < 0x40 || dos.e_lfanew > 0x1000
        || !Read(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt.FileHeader.TimeDateStamp != 0x4b7c15c9
        || nt.OptionalHeader.SizeOfImage != 0xa94000) return false;
    for (const auto& p : patches) {
        BYTE bytes[9]{};
        if (!Read(p.address, bytes, p.size) || std::memcmp(bytes, p.expected, p.size)) return false;
    }
    DWORD protection[ARRAYSIZE(patches)]{};
    for (unsigned i = 0; i < ARRAYSIZE(patches); ++i) {
        if (!VirtualProtect(reinterpret_cast<void*>(patches[i].address), patches[i].size, PAGE_EXECUTE_READWRITE, &protection[i])) {
            for (unsigned j = i; j > 0; --j) {
                const unsigned previous = j - 1;
                DWORD ignored = 0;
                VirtualProtect(reinterpret_cast<void*>(patches[previous].address), patches[previous].size, protection[previous], &ignored);
            }
            return false;
        }
    }
    // Install during single-threaded client initialization, before resource use.
    for (const auto& p : patches) {
        auto code = reinterpret_cast<BYTE*>(p.address);
        code[0] = 0xe9;
        const DWORD relative = reinterpret_cast<DWORD>(p.target) - p.address - 5;
        std::memcpy(code + 1, &relative, 4); std::memset(code + 5, 0x90, p.size - 5);
    }
    bool ready = FlushInstructionCache(GetCurrentProcess(), nullptr, 0) != FALSE;
    // The first two sites share a page. Restore in reverse order so the original
    // protection wins over the temporary RWX observed by the second site.
    for (unsigned remaining = ARRAYSIZE(patches); remaining > 0; --remaining) {
        const unsigned i = remaining - 1;
        DWORD ignored = 0;
        ready = (VirtualProtect(reinterpret_cast<void*>(patches[i].address), patches[i].size, protection[i], &ignored) != FALSE) && ready;
    }
    pcomBase = KnownPcom();
    installed = true;
    return installationReady = ready;
}
}
