#include "stdafx.h"
#include <winsock2.h>
#include <intrin.h>
#include "DisconnectDiagnostics.h"
#include "DisconnectClassification.h"
#include "ClientDiagnostics.h"
#include "ClientLog.h"
#include <strsafe.h>
#pragma comment(lib, "Ws2_32.lib")

namespace {
bool g_enabled = false;
thread_local bool g_logging = false;
SRWLOCK g_lock = SRWLOCK_INIT;
struct PacketEntry { ULONGLONG time; DWORD thread; unsigned short opcode; unsigned long size; bool incoming; };
PacketEntry g_packets[32]{};
unsigned int g_next = 0;
ULONGLONG g_packetGeneration = 0;
thread_local ULONGLONG g_eventSequence = 0;
struct ExceptionDetail {
    bool valid = false;
    void* frames[24]{};
    USHORT count = 0;
    DWORD code = 0;
    void* address = nullptr;
    ULONG_PTR access = 0, target = 0;
    ULONGLONG generation = 0, id = 0;
    const char* phase = nullptr;
    const char* kind = nullptr;
};
thread_local ExceptionDetail g_detail;
DWORD g_gameThread = 0;
thread_local ULONGLONG g_channelCloseDeadline = 0;
thread_local const char* g_phase = "active";
bool g_knownExitSites = false;

bool CheckCall(const BYTE* base, DWORD returnOffset, DWORD targetOffset) {
    const BYTE* instruction = base + returnOffset - 5;
    LONG displacement = 0;
    memcpy(&displacement, instruction + 1, sizeof(displacement));
    return instruction[0] == 0xE8 && returnOffset + displacement == targetOffset;
}
bool ValidateExitSites() {
    const auto* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    return CheckCall(base, 0x6068BB, 0x62465E)
        && CheckCall(base, 0x607529, 0x606735)
        && CheckCall(base, 0x4D42B0, 0x606735)
        && CheckCall(base, 0x5F5219, 0x5F69B7)
        && CheckCall(base, 0x5F1CF1, 0x5F51F6);
}
DisconnectDiagnostics::ClosePath PathFromFrames(void* const* frames, USHORT count) {
    if (!g_knownExitSites) return DisconnectDiagnostics::ClosePath::Unknown;
    std::uintptr_t offsets[24]{};
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    for (USHORT i = 0; i < count; ++i) {
        const auto address = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (address >= base && address < base + 0xA94000) offsets[i] = address - base;
    }
    return DisconnectDiagnostics::ClassifyClosePath(offsets, count);
}
DisconnectDiagnostics::ClosePath CurrentClosePath() {
    void* frames[24]{};
    const USHORT count = CaptureStackBackTrace(0, ARRAYSIZE(frames), frames, nullptr);
    return PathFromFrames(frames, count);
}
using Recv = int (WSAAPI*)(SOCKET, char*, int, int);
using Close = int (WSAAPI*)(SOCKET);
Recv g_recv = nullptr;
Close g_close = nullptr;

bool FromGame(void* caller) {
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(caller, &memory, sizeof(memory)) && memory.AllocationBase == GetModuleHandleW(nullptr);
}

void Event(const char* kind, SOCKET socket, int error, EXCEPTION_POINTERS* exception = nullptr) {
    if (!g_enabled || g_logging) return;
    if (exception || error) g_channelCloseDeadline = 0;
    const DWORD savedError = GetLastError();
    g_logging = true;
    ClientDiagnostics::Snapshot connection;
    ClientDiagnostics::TrySnapshot(connection);
    void* frames[24]{};
    const USHORT count = CaptureStackBackTrace(1, ARRAYSIZE(frames), frames, nullptr);
    // Cleanup exceptions may occur before closesocket. Classify this event's stack,
    // without permanently changing phase when native code handles the exception.
    const auto path = PathFromFrames(frames, count);
    const char* phase = path == DisconnectDiagnostics::ClosePath::ProcessCleanup ? "process_cleanup"
        : path == DisconnectDiagnostics::ClosePath::ConfirmedLogout ? "user_logout" : g_phase;
    const ULONGLONG id = ++g_eventSequence;
    ULONGLONG generation = 0;
    const bool historyAvailable = TryAcquireSRWLockShared(&g_lock) != FALSE;
    if (historyAvailable) {
        generation = g_packetGeneration;
        ReleaseSRWLockShared(&g_lock);
    }
    ExceptionDetail detail;
    if (exception && exception->ExceptionRecord && exception->ContextRecord) {
        const auto& record = *exception->ExceptionRecord;
        detail.valid = historyAvailable && count > 0;
        memcpy(detail.frames, frames, sizeof(frames));
        detail.count = count;
        detail.code = record.ExceptionCode;
        detail.address = record.ExceptionAddress;
        detail.access = record.NumberParameters > 0 ? record.ExceptionInformation[0] : 0;
        detail.target = record.NumberParameters > 1 ? record.ExceptionInformation[1] : 0;
        detail.generation = generation;
        detail.id = id;
        detail.phase = phase;
        detail.kind = kind;
        if (detail.valid && g_detail.valid && detail.count == g_detail.count
            && !memcmp(detail.frames, g_detail.frames, sizeof(frames))
            && detail.code == g_detail.code && detail.address == g_detail.address
            && detail.access == g_detail.access && detail.target == g_detail.target
            && detail.generation == g_detail.generation
            && !strcmp(phase, g_detail.phase) && !strcmp(kind, g_detail.kind)) {
            const auto& c = *exception->ContextRecord;
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "exception_repeat kind=%s event=%llu detailRef=%llu phase=%s code=%08lX address=%p eip=%08lX esp=%08lX ebp=%08lX eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX access=%llu target=%p",
                kind, id, g_detail.id, phase, record.ExceptionCode, record.ExceptionAddress,
                c.Eip, c.Esp, c.Ebp, c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi,
                static_cast<unsigned long long>(detail.access), reinterpret_cast<void*>(detail.target));
            g_logging = false;
            SetLastError(savedError);
            return;
        }
    }
    g_detail = detail;
    char stack[1536]{};
    for (USHORT i = 0; i < count; ++i) {
        MEMORY_BASIC_INFORMATION memory{};
        VirtualQuery(frames[i], &memory, sizeof(memory));
        char module[MAX_PATH]{};
        GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), module, ARRAYSIZE(module));
        const char* name = strrchr(module, '\\');
        name = name ? name + 1 : module;
        char frame[128]{};
        StringCchPrintfA(frame, ARRAYSIZE(frame), "%s%.64s@%p+%lX", i ? ";" : "", name,
            memory.AllocationBase, static_cast<unsigned long>(reinterpret_cast<ULONG_PTR>(frames[i]) - reinterpret_cast<ULONG_PTR>(memory.AllocationBase)));
        StringCchCatA(stack, ARRAYSIZE(stack), frame);
    }
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "disconnect_event kind=%s socket=%llu error=%d clientRunId=%s connectionId=%s attempt=%lu event=%llu phase=%s stackModuleBaseOffset=%s",
        kind, static_cast<unsigned long long>(socket), error, connection.clientRunId, connection.connectionId,
        static_cast<unsigned long>(connection.diagnosticAttempt), id, phase, stack);
    if (exception && exception->ExceptionRecord && exception->ContextRecord) {
        const CONTEXT& c = *exception->ContextRecord;
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "disconnect_exception connectionId=%s code=%08lX address=%p eip=%08lX esp=%08lX ebp=%08lX eax=%08lX ecx=%08lX edx=%08lX access=%llu target=%p",
            connection.connectionId, exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress,
            c.Eip, c.Esp, c.Ebp, c.Eax, c.Ecx, c.Edx,
            static_cast<unsigned long long>(detail.access), reinterpret_cast<void*>(detail.target));
        // FPO/naked native functions can defeat CaptureStackBackTrace. Preserve only
        // executable-address candidates from the ORIGINAL stack, never packet data.
        DWORD words[64]{};
        SIZE_T bytes = 0;
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(c.Esp), words, sizeof(words), &bytes);
        for (SIZE_T i = 0; i < bytes / sizeof(DWORD); ++i) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<const void*>(words[i]), &memory, sizeof(memory))) continue;
            const DWORD protection = memory.Protect & 0xff;
            if (memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE
                || (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ
                    && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)) continue;
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "exception_stack_candidate connectionId=%s slot=%lu address=%08lX moduleBaseOffset=%p+%lX notUnwound=1",
                connection.connectionId, static_cast<DWORD>(i), words[i], memory.AllocationBase,
                words[i] - reinterpret_cast<DWORD>(memory.AllocationBase));
        }
    }
    PacketEntry packets[32]{};
    unsigned int next = 0;
    if (TryAcquireSRWLockShared(&g_lock)) {
        memcpy(packets, g_packets, sizeof(packets));
        next = g_next;
        ReleaseSRWLockShared(&g_lock);
        for (unsigned int i = 0; i < ARRAYSIZE(packets); ++i) {
            const auto& p = packets[(next + i) % ARRAYSIZE(packets)];
            if (p.time) ClientLog::Append(ClientLog::Component::Lifecycle,
                "disconnect_packet connectionId=%s direction=%s tick=%llu thread=%lu opcode=%04X size=%lu",
                connection.connectionId, p.incoming ? "in" : "out", p.time, p.thread, p.opcode, p.size);
        }
    }
    g_logging = false;
    SetLastError(savedError);
}

int WSAAPI Receive(SOCKET s, char* buffer, int length, int flags) {
    void* caller = _ReturnAddress();
    const int result = g_recv(s, buffer, length, flags);
    const int error = WSAGetLastError();
    if (FromGame(caller) && (result == 0 || (result == SOCKET_ERROR && error != WSAEWOULDBLOCK)))
        Event(result == 0 ? "peer_eof" : "recv_error", s, result == 0 ? 0 : error);
    WSASetLastError(error);
    return result;
}
int WSAAPI CloseSocket(SOCKET s) {
    void* caller = _ReturnAddress();
    // Native code calls shutdown immediately before closesocket. Never insert file
    // I/O into that gap: a queued FD_CLOSE can outlive this socket and hit a reused handle.
    const int result = g_close(s);
    const int error = WSAGetLastError();
    if (FromGame(caller)) {
        const auto path = CurrentClosePath();
        if (path == DisconnectDiagnostics::ClosePath::ConfirmedLogout) g_phase = "user_logout";
        else if (path == DisconnectDiagnostics::ClosePath::ProcessCleanup) g_phase = "process_cleanup";
        const bool expected = g_channelCloseDeadline && GetTickCount64() <= g_channelCloseDeadline;
        g_channelCloseDeadline = 0;
        if (result == SOCKET_ERROR) Event("local_socket_close_error", s, error);
        else if (!expected && path == DisconnectDiagnostics::ClosePath::Unknown) Event("local_socket_close", s, 0);
    }
    WSASetLastError(error);
    return result;
}
LONG CALLBACK ObserveException(EXCEPTION_POINTERS* info) {
    if (g_logging || GetCurrentThreadId() != g_gameThread || !info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION
        || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == 0xE06D7363) {
        Event("first_chance_exception_not_necessarily_fatal", INVALID_SOCKET, 0, info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}

void DisconnectDiagnostics::ExpectChannelClose() {
    if (g_enabled) g_channelCloseDeadline = GetTickCount64() + 5000;
}

void DisconnectDiagnostics::Install(bool enabled) {
    if (!enabled || g_enabled) return;
    HMODULE module = GetModuleHandleW(L"ws2_32.dll");
    if (!module) module = LoadLibraryW(L"ws2_32.dll");
    if (!module) return;
    g_recv = reinterpret_cast<Recv>(GetProcAddress(module, "recv"));
    g_close = reinterpret_cast<Close>(GetProcAddress(module, "closesocket"));
    g_enabled = true;
    g_gameThread = GetCurrentThreadId();
    g_knownExitSites = ValidateExitSites();
    const bool exceptionObserver = AddVectoredExceptionHandler(1, ObserveException) != nullptr;
    const bool receive = g_recv && Memory::SetHook(true, reinterpret_cast<void**>(&g_recv), Receive);
    const bool close = g_close && Memory::SetHook(true, reinterpret_cast<void**>(&g_close), CloseSocket);
    ClientLog::Append(ClientLog::Component::Lifecycle, "disconnect_diagnostics enabled=1 recvHook=%d closeHook=%d exceptionObserver=%d eventLimitPerConnection=unlimited", receive, close, exceptionObserver);
    ClientLog::Append(ClientLog::Component::Lifecycle, "disconnect_classification verifiedExitSites=%d", g_knownExitSites);
}

void DisconnectDiagnostics::Packet(bool incoming, unsigned short opcode, unsigned long size) {
    if (!g_enabled) return;
    const DWORD savedError = GetLastError();
    if (!incoming && (opcode == 1 || opcode == 0x14)) {
        g_phase = "active";
        g_channelCloseDeadline = 0;
        AcquireSRWLockExclusive(&g_lock);
        memset(g_packets, 0, sizeof(g_packets));
        g_next = 0;
        ReleaseSRWLockExclusive(&g_lock);
    }
    AcquireSRWLockExclusive(&g_lock);
    ++g_packetGeneration;
    g_packets[g_next] = {GetTickCount64(), GetCurrentThreadId(), opcode, size, incoming};
    g_next = (g_next + 1) % ARRAYSIZE(g_packets);
    ReleaseSRWLockExclusive(&g_lock);
    SetLastError(savedError);
}

LONG DisconnectDiagnostics::PacketException(EXCEPTION_POINTERS* info) {
    Event("packet_processing_exception", INVALID_SOCKET, 0, info);
    // Observe before native handlers unwind the stack; never consume the exception.
    return EXCEPTION_CONTINUE_SEARCH;
}
