#pragma once
#include "ClientLog.h"

// Event-only observer. Never rewrites packets or changes native state.
namespace EvanMountDiagnostics {
inline bool Enabled() {
    static const bool enabled = [] { char value[8]{};
        GetEnvironmentVariableA("BEIDOU_EVAN_MOUNT_LOG", value, sizeof(value));
        return value[0] != '0'; }();
    return enabled;
}
inline void Log(const char* stage, const unsigned char* data, unsigned long size) {
    static volatile LONG records = 0;
    if (!Enabled() || !data || size < 22 || data[5] != 0 ||
        (data[4] != 0x20 && data[4] != 0x21) || !(data[12] & 0x20)) return;
    if (InterlockedIncrement(&records) > 192) return;
    bool single = true;
    for (unsigned i=6; i<22; ++i) if (data[i] != (i==12 ? 0x20 : 0)) single=false;
    int model=0, source=0;
    const bool decoded=single && data[4]==0x20 && size>=32;
    if(decoded) { memcpy(&model,data+24,4); memcpy(&source,data+28,4); }
    int avatarMount=-1, stance=-1;
    bool stateRead=false;
    // v83 967160 reads CUser+544 to classify the active mount (190/193).
    // The follow-facing hook already reads CUser+570 as the avatar stance.
    __try {
        const auto user=*reinterpret_cast<const unsigned char**>(0x00BEBF98);
        if(user) {
            memcpy(&avatarMount,user+0x544,4);
            memcpy(&stance,user+0x570,4);
            stateRead=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { stateRead=false; }
    ClientLog::Append(ClientLog::Component::Trace,
        "event=evan_mount_packet stage=%s opcode=%u bytes=%lu single=%d decoded=%d model=%d source=%d stateRead=%d avatarMount=%d stanceRaw=%d",
        stage,data[4],size,single,decoded,model,source,stateRead,avatarMount,stance);
}
}
