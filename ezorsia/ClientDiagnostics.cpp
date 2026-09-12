#include "stdafx.h"
#include "ClientDiagnostics.h"
#include "ClientLog.h"

#include <cstring>
#include <strsafe.h>
#include <NTSecAPI.h>

namespace {
INIT_ONCE g_initialized = INIT_ONCE_STATIC_INIT;
SRWLOCK g_stateLock = SRWLOCK_INIT;
volatile LONG g_ready = 0;
volatile LONG g_ackConsumerReady = 0;
volatile LONG g_rejectedAcks = 0;
unsigned char g_run[16]{};
char g_runText[33]{};
ClientDiagnostics::Snapshot g_state;

bool Nonzero(const unsigned char* data, std::size_t size)
{
    unsigned char combined = 0;
    for (std::size_t i = 0; i < size; ++i) combined |= data[i];
    return combined != 0;
}

void Hex16(const unsigned char* data, char* text)
{
    static const char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 16; ++i) {
        text[i * 2] = digits[data[i] >> 4];
        text[i * 2 + 1] = digits[data[i] & 15];
    }
    text[32] = 0;
}

BOOL CALLBACK InitializeOnce(PINIT_ONCE, PVOID, PVOID*)
{
    // The DLL initializes under the loader lock; avoid loading a cryptographic provider here.
    const bool available = RtlGenRandom(g_run, sizeof(g_run)) != FALSE && Nonzero(g_run, sizeof(g_run));
    if (available) {
        Hex16(g_run, g_runText);
        g_state.connectionState = ClientDiagnostics::ConnectionState::Unbound;
    }
    InterlockedExchange(&g_ready, available ? 1 : -1);
    return TRUE;
}

std::uint32_t ReadU32(const unsigned char* data)
{
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

void LogRejectedAck(const char* reason)
{
    const LONG count = InterlockedIncrement(&g_rejectedAcks);
    if (count > 0 && count <= 8)
        ClientLog::Append(ClientLog::Component::Lifecycle, "diagnostic_ack_rejected reason=%s suppressedAfter=8", reason);
}

void ApplyAck(const unsigned char* packet)
{
    const char* rejected = nullptr;
    const std::uint32_t attempt = ReadU32(packet + 23);
    if (packet[6] != 1) rejected = "version";
    else if (InterlockedCompareExchange(&g_ready, 0, 0) != 1) rejected = "run_unavailable";
    else if (std::memcmp(packet + 7, g_run, sizeof(g_run)) != 0) rejected = "run_mismatch";
    else if (attempt == 0) rejected = "zero_attempt";
    else if (!Nonzero(packet + 27, 16)) rejected = "zero_boot";
    else if (packet[50] & 0x80) rejected = "negative_session";

    char connection[54]{};
    if (!rejected) {
        char boot[33]{};
        Hex16(packet + 27, boot);
        const ULONGLONG session = static_cast<ULONGLONG>(ReadU32(packet + 43))
            | (static_cast<ULONGLONG>(ReadU32(packet + 47)) << 32);
        StringCchPrintfA(connection, ARRAYSIZE(connection), "%s-%llu", boot, session);
    }

    bool changed = false;
    if (!rejected) {
        AcquireSRWLockExclusive(&g_stateLock);
        if (attempt != g_state.diagnosticAttempt) rejected = "stale_attempt";
        else if (g_state.connectionState == ClientDiagnostics::ConnectionState::Bound) {
            if (std::strcmp(connection, g_state.connectionId) != 0) rejected = "already_bound";
        } else if (g_state.connectionState != ClientDiagnostics::ConnectionState::Pending) rejected = "not_pending";
        else {
            StringCchCopyA(g_state.connectionId, ARRAYSIZE(g_state.connectionId), connection);
            g_state.connectionState = ClientDiagnostics::ConnectionState::Bound;
            changed = true;
        }
        ReleaseSRWLockExclusive(&g_stateLock);
    }
    if (rejected) LogRejectedAck(rejected);
    else if (changed) ClientLog::Append(ClientLog::Component::Lifecycle,
        "diagnostic_binding clientRunId=%s connectionId=%s diagnosticAttempt=%lu connectionState=bound",
        ClientDiagnostics::RunId(), connection, static_cast<unsigned long>(attempt));
}
}

void ClientDiagnostics::Initialize()
{
    const DWORD savedError = GetLastError();
    InitOnceExecuteOnce(&g_initialized, InitializeOnce, nullptr, nullptr);
    SetLastError(savedError);
}

void ClientDiagnostics::SetAckConsumerReady(bool ready)
{
    InterlockedExchange(&g_ackConsumerReady, ready ? 1 : 0);
    if (!ready) {
        AcquireSRWLockExclusive(&g_stateLock);
        StringCchCopyA(g_state.connectionId, ARRAYSIZE(g_state.connectionId), "unavailable");
        g_state.connectionState = ConnectionState::Unavailable;
        ReleaseSRWLockExclusive(&g_stateLock);
    }
}

const char* ClientDiagnostics::RunId()
{
    return InterlockedCompareExchange(&g_ready, 0, 0) == 1 ? g_runText : "unavailable";
}

const char* ClientDiagnostics::StateName(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Unbound: return "unbound";
    case ConnectionState::Pending: return "pending";
    case ConnectionState::Bound: return "bound";
    default: return "unavailable";
    }
}

bool ClientDiagnostics::TrySnapshot(Snapshot& snapshot)
{
    snapshot = Snapshot{};
    StringCchCopyA(snapshot.clientRunId, ARRAYSIZE(snapshot.clientRunId), RunId());
    if (InterlockedCompareExchange(&g_ready, 0, 0) != 1 || !TryAcquireSRWLockShared(&g_stateLock)) return false;
    StringCchCopyA(snapshot.connectionId, ARRAYSIZE(snapshot.connectionId), g_state.connectionId);
    snapshot.diagnosticAttempt = g_state.diagnosticAttempt;
    snapshot.connectionState = g_state.connectionState;
    ReleaseSRWLockShared(&g_stateLock);
    return true;
}

bool ClientDiagnostics::BeginConnection(unsigned char (&packet)[kIdentifyPacketSize], unsigned short triggerOpcode)
{
    Initialize();
    Snapshot previous;
    TrySnapshot(previous);
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "diagnostic_previous_connection clientRunId=%s connectionId=%s attempt=%lu nextTriggerOpcode=%04X",
        previous.clientRunId, previous.connectionId, static_cast<unsigned long>(previous.diagnosticAttempt), triggerOpcode);
    AcquireSRWLockExclusive(&g_stateLock);
    StringCchCopyA(g_state.connectionId, ARRAYSIZE(g_state.connectionId), "unavailable");
    const bool available = InterlockedCompareExchange(&g_ready, 0, 0) == 1
        && InterlockedCompareExchange(&g_ackConsumerReady, 0, 0) == 1
        && g_state.diagnosticAttempt != UINT32_MAX;
    if (available) ++g_state.diagnosticAttempt;
    g_state.connectionState = available ? ConnectionState::Pending : ConnectionState::Unavailable;
    const std::uint32_t attempt = g_state.diagnosticAttempt;
    ReleaseSRWLockExclusive(&g_stateLock);
    if (available) {
        packet[0] = 0x02;
        packet[1] = 0x10;
        packet[2] = 1;
        std::memcpy(packet + 3, g_run, sizeof(g_run));
        for (unsigned int i = 0; i < 4; ++i) packet[19 + i] = static_cast<unsigned char>(attempt >> (8 * i));
    }
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "diagnostic_connect clientRunId=%s connectionId=unavailable diagnosticAttempt=%lu connectionState=%s triggerOpcode=0x%04X",
        RunId(), static_cast<unsigned long>(attempt), available ? "pending" : "unavailable", triggerOpcode);
    return available;
}

bool ClientDiagnostics::HandleIncoming(const unsigned char* data, std::size_t length)
{
    const DWORD savedError = GetLastError();
    bool consumed = false;
    unsigned char packet[kAckPacketSize]{};
    // Copy before taking a lock so a malformed pointer cannot strand the connection lock.
    __try {
        if (data && length >= 6 && data[4] == 0x07 && data[5] == 0x10) {
            consumed = true;
            if (length == sizeof(packet)) {
                std::memcpy(packet, data, sizeof(packet));
                ApplyAck(packet);
            } else LogRejectedAck("length");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (consumed) LogRejectedAck("exception");
    }
    SetLastError(savedError);
    return consumed;
}
