#include "stdafx.h"
#include "VisitorsMovementDiagnostics.h"
#include "ClientLog.h"
#include "Memory.h"
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
struct InPacket {
    int loopback, state;
    unsigned char* data;
    unsigned short length, sequence;
    unsigned unknown, offset;
};
static_assert(sizeof(void*) == 4 && offsetof(InPacket, offset) == 0x14, "v83 packet layout");
using OnMove = void(__fastcall*)(void*, void*, InPacket*);
using OnUpdate = void(__fastcall*)(void*, void*);
using ReadInt = int(__cdecl*)(const void*, unsigned);
using ReadDouble = double(__cdecl*)(const void*, unsigned);
OnMove originalMove = reinterpret_cast<OnMove>(0x0066BE61);
OnUpdate originalUpdate = reinterpret_cast<OnUpdate>(0x006675A8);
ReadInt readInt = reinterpret_cast<ReadInt>(0x00416563);
ReadInt readId = reinterpret_cast<ReadInt>(0x004E8152);
ReadDouble readDouble = reinterpret_cast<ReadDouble>(0x00539338);

template<class T> T At(const void* object, size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(object) + offset);
}
struct Wire {
    int valid = 0, activity = 0, count = 0, type = 0;
    int startX = 0, startY = 0, endX = 0, endY = 0, vx = 0, vy = 0, fh = 0, stance = 0, duration = 0;
};
Wire ReadWire(const InPacket* packet) {
    Wire w;
    if (!packet || !packet->data || packet->offset > packet->length
        || packet->length - packet->offset < 26) return w;
    // CMob::OnMove has already consumed the object id. Only decode the single
    // absolute fragment produced by EscortController; never mutate the packet.
    const auto* p = packet->data + packet->offset;
    w.activity = static_cast<signed char>(p[2]);
    w.startX = At<short>(p, 7); w.startY = At<short>(p, 9);
    w.count = p[11]; w.type = p[12];
    if (w.count != 1 || w.type != 0) return w;
    w.endX = At<short>(p, 13); w.endY = At<short>(p, 15);
    w.vx = At<short>(p, 17); w.vy = At<short>(p, 19);
    w.fh = At<short>(p, 21); w.stance = p[23]; w.duration = At<short>(p, 24);
    w.valid = 1;
    return w;
}
struct Row {
    unsigned elapsed, oid, templateId;
    char event;
    int control, localControl, queue, remaining, stance, anchorX, anchorY;
    double x, y, previousX, previousY, vx, vy;
    Wire wire;
};
constexpr unsigned captureMillis = 15000;
constexpr size_t capacity = 1536;
Row rows[capacity]{};
size_t used = 0;
DWORD started = 0, threadId = 0;
unsigned trackedOid = 0;
void* trackedMob = nullptr;
bool enabled = false, startedCapture = false, attempted = false;

bool Snapshot(void* mob, Row& row) {
    __try {
        const auto* mobTemplate = At<unsigned char*>(mob, 0x188);
        row.templateId = readId(mobTemplate + 0xC, At<unsigned>(mobTemplate, 0x14));
        if (row.templateId < 9420075 || row.templateId > 9420077) return false;
        row.oid = readId(static_cast<unsigned char*>(mob) + 0x17C, At<unsigned>(mob, 0x184));
        const auto* v = At<unsigned char*>(mob, 0x118) - 12;
        const auto* local = At<unsigned char*>(mob, 0x11C) - 12;
        row.control = readInt(static_cast<unsigned char*>(mob) + 0x130, At<unsigned>(mob, 0x138));
        row.localControl = At<int>(local, 0x18);
        row.x = readDouble(v + 0x20, At<unsigned>(v, 0x30));
        row.y = readDouble(v + 0x38, At<unsigned>(v, 0x48));
        row.previousX = readDouble(v + 0x80, At<unsigned>(v, 0x90));
        row.previousY = readDouble(v + 0x98, At<unsigned>(v, 0xA8));
        row.vx = readDouble(v + 0x50, At<unsigned>(v, 0x60));
        row.vy = readDouble(v + 0x68, At<unsigned>(v, 0x78));
        const auto* path = v + 0x1AC;
        row.queue = At<int>(path, 0x18);
        row.remaining = readInt(path + 0x90, At<unsigned>(path, 0x98));
        row.stance = At<int>(v, 0x148);
        row.anchorX = At<short>(path, 0x46); row.anchorY = At<short>(path, 0x48);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Flush() {
    enabled = false;
    // At most 1,536 rows / about 500 KiB, written once after capture. No disk IO
    // in the frame sampling path. The existing session logger handles retention.
    HANDLE file = ClientLog::Open(ClientLog::Component::Trace);
    ClientLog::Write(file, "visitors_movement_begin format=1 event=B/before_move,A/after_move,U/after_update\r\n");
    for (size_t i = 0; i < used; ++i) {
        const auto& r = rows[i]; const auto& w = r.wire;
        char line[640]{};
        std::snprintf(line, sizeof(line),
            "visitors_movement ms=%u event=%c oid=%u template=%u xy=%.3f,%.3f prev=%.3f,%.3f v=%.3f,%.3f control=%d local=%d queue=%d remain=%d stance=%d tail=%d,%d wire=%d raw=%d count=%d type=%d start=%d,%d end=%d,%d velocity=%d,%d fh=%d action=%d duration=%d\r\n",
            r.elapsed,r.event,r.oid,r.templateId,r.x,r.y,r.previousX,r.previousY,r.vx,r.vy,
            r.control,r.localControl,r.queue,r.remaining,r.stance,r.anchorX,r.anchorY,
            w.valid,w.activity,w.count,w.type,w.startX,w.startY,w.endX,w.endY,w.vx,w.vy,w.fh,w.stance,w.duration);
        ClientLog::Write(file, line);
    }
    ClientLog::Write(file, "visitors_movement_end\r\n");
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    ClientLog::Append(ClientLog::Component::Lifecycle, "visitors_movement_capture finished rows=%u", unsigned(used));
}

void Capture(void* mob, char event, const InPacket* packet = nullptr) {
    if (!enabled || (startedCapture && GetCurrentThreadId() != threadId)) return;
    if (startedCapture && (GetTickCount() - started >= captureMillis || used == capacity)) { Flush(); return; }
    if (startedCapture && mob != trackedMob) return;
    Row row{};
    if (!Snapshot(mob, row)) return;
    if (!startedCapture) {
        if (event != 'B') return;
        startedCapture = true; started = GetTickCount(); threadId = GetCurrentThreadId();
        trackedMob = mob; trackedOid = row.oid;
    }
    if (row.oid != trackedOid) return;
    row.elapsed = GetTickCount() - started; row.event = event;
    __try { row.wire = ReadWire(packet); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    rows[used++] = row;
}
void __fastcall Move(void* mob, void* edx, InPacket* packet) {
    Capture(mob, 'B', packet);
    originalMove(mob, edx, packet);
    Capture(mob, 'A');
}
void __fastcall Update(void* mob, void* edx) {
    originalUpdate(mob, edx);
    Capture(mob, 'U');
}
}

void VisitorsMovementDiagnostics::Install() {
    if (attempted) return;
    attempted = true;
    wchar_t marker[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, marker, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    auto* slash = std::wcsrchr(marker, L'\\');
    if (!slash) return;
    if (wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - marker), L"logs\\visitors-movement.once")) return;
    if (GetFileAttributesW(marker) == INVALID_FILE_ATTRIBUTES) return;
    const unsigned char moveSignature[] = {0xB8,0x78,0x1A,0xAA,0,0xE8,0x2D,0x4D,0x3F,0};
    const unsigned char updateSignature[] = {0xB8,0x1B,0x17,0xAA,0,0xE8,0xE6,0x95,0x3F,0};
    if (std::memcmp(reinterpret_cast<void*>(originalMove),moveSignature,sizeof(moveSignature))
        || std::memcmp(reinterpret_cast<void*>(originalUpdate),updateSignature,sizeof(updateSignature))) {
        ClientLog::Append(ClientLog::Component::Lifecycle,"visitors_movement_capture signature_mismatch"); return;
    }
    if (!DeleteFileW(marker)) return;
    if (!Memory::SetHook(true,reinterpret_cast<void**>(&originalMove),reinterpret_cast<void*>(&Move))) return;
    if (!Memory::SetHook(true,reinterpret_cast<void**>(&originalUpdate),reinterpret_cast<void*>(&Update))) {
        Memory::SetHook(false,reinterpret_cast<void**>(&originalMove),reinterpret_cast<void*>(&Move)); return;
    }
    enabled = true;
    ClientLog::Append(ClientLog::Component::Lifecycle,"visitors_movement_capture armed durationMs=%u capacity=%u",captureMillis,unsigned(capacity));
}
