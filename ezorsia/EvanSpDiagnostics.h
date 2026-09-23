#pragma once
#include "ClientLog.h"

// Read-only wire observations, independent of the native packet cursor.
// Layouts: PacketCreator.getCharInfo/addCharStats/updatePlayerStats, GMS 083.
// native_return confirms handler return, NOT the skill window's stored values.
namespace EvanSpDiagnostics {
inline bool Enabled() {
    static const bool enabled = [] { char value[8]{};
        GetEnvironmentVariableA("BEIDOU_EVAN_SP_LOG", value, sizeof(value));
        return value[0] != '0'; }();
    return enabled;
}
inline unsigned Read16(const unsigned char* p) { return p[0] | (unsigned(p[1]) << 8); }
inline unsigned Read32(const unsigned char* p) { return Read16(p) | (Read16(p + 2) << 16); }
inline bool IsEvan(int job) { return job == 2001 || job == 2200 || (job >= 2210 && job <= 2218); }
struct Snapshot {
    bool relevant = false;
    bool decoded = false;
    unsigned mask = 0;
    unsigned count = 0;
    unsigned sp[10]{};
    const char* source = "stat_changed";
    const char* reason = "truncated";
};
inline void Table(const unsigned char* data, unsigned long size, unsigned pos, Snapshot& out) {
    if (pos >= size) return;
    out.count = data[pos++];
    if (out.count > 10 || size - pos < out.count * 2) return;
    unsigned seen = 0;
    for (unsigned i = 0; i < out.count; ++i) {
        const unsigned book = data[pos++];
        const unsigned value = data[pos++];
        if (book < 1 || book > 10 || (seen & (1u << (book - 1)))) {
            out.reason = "invalid_book";
            return;
        }
        seen |= 1u << (book - 1);
        out.sp[book - 1] = value;
    }
    out.decoded = true;
    out.reason = "ok";
}
inline Snapshot Decode(const unsigned char* data, unsigned long size, int& job) {
    Snapshot out;
    if (!data || size < 6) return out;
    const unsigned opcode = Read16(data + 4);
    if (opcode == 0x7d) {
        if (size < 12 || data[11] != 1) return out; // ordinary map warp has no character data
        job = -1;
        out.relevant = true;
        out.source = "set_field";
        // Full character data: channel(4), flags(2), notifications(2), seeds(12),
        // all-data mask(8), zero(1), CharacterStat. Never log identity/equipment payloads.
        if (size < 108) return out;
        if (data[12] || data[13] || data[34]) { out.reason = "unsupported_layout"; return out; }
        for (unsigned i = 26; i < 34; ++i)
            if (data[i] != 0xff) { out.reason = "unsupported_layout"; return out; }
        job = Read16(data + 87);
        if (!IsEvan(job)) { out.relevant = false; return out; }
        Table(data, size, 107, out);
        return out;
    }
    if (opcode != 0x1f || size < 11) return out;
    out.mask = Read32(data + 7);
    out.relevant = (out.mask & 0x8000) != 0;
    // Reserved bit 0x8 has no scalar stat definition; do not guess its width.
    if (out.mask & 8) { out.reason = "unsupported_mask"; return out; }
    unsigned pos = 11;
    for (unsigned bit = 1; bit < 0x8000; bit <<= 1) {
        if (!(out.mask & bit)) continue;
        const unsigned width = (bit == 1 || bit == 0x10) ? 1 : (bit <= 4 ? 4 : 2);
        if (size - pos < width) return out;
        if (bit == 0x20) job = Read16(data + pos);
        pos += width;
    }
    if (!out.relevant) return out;
    if (job < 0) { out.reason = "unknown_job"; return out; }
    if (!IsEvan(job)) { out.relevant = false; return out; }
    Table(data, size, pos, out);
    return out;
}
inline void Observe(const char* stage, const unsigned char* data, unsigned long size) {
    static int job = -1;
    static volatile LONG records = 0;
    if (!Enabled()) return;
    const Snapshot s = Decode(data, size, job);
    if (!s.relevant || InterlockedIncrement(&records) > 256) return;
    ClientLog::Append(ClientLog::Component::Trace,
        "event=client_evan_sp version=2 stage=%s source=%s bytes=%lu job=%d mask=%08X decoded=%d reason=%s count=%u wireSp=[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",
        stage, s.source, size, job, s.mask, s.decoded, s.reason, s.count,
        s.sp[0], s.sp[1], s.sp[2], s.sp[3], s.sp[4], s.sp[5], s.sp[6], s.sp[7], s.sp[8], s.sp[9]);
}
}
