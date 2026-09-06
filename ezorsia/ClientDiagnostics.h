#pragma once

#include <cstddef>
#include <cstdint>

namespace ClientDiagnostics {
constexpr std::size_t kIdentifyPacketSize = 23;
constexpr std::size_t kAckPacketSize = 51;

enum class ConnectionState { Unavailable, Unbound, Pending, Bound };

struct Snapshot {
    char clientRunId[33] = "unavailable";
    char connectionId[54] = "unavailable";
    std::uint32_t diagnosticAttempt = 0;
    ConnectionState connectionState = ConnectionState::Unavailable;
};

void Initialize();
void SetAckConsumerReady(bool ready);
// Never initializes or waits; the process identifier is immutable once published.
const char* RunId();
const char* StateName(ConnectionState state);
bool TrySnapshot(Snapshot& snapshot);
bool BeginConnection(unsigned char (&packet)[kIdentifyPacketSize], unsigned short triggerOpcode);
// Incoming client buffers include the four-byte transport prefix. Consume malformed ACKs too.
bool HandleIncoming(const unsigned char* data, std::size_t length);
}
