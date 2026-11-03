#include "stdafx.h"
#include "VisitorsLabPlatforms.h"
#include "MixedDyeResources.h"
#include "ClientLog.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>

namespace {
using namespace MixedDyeResources;
using VisitorsLabPlatforms::Motion;
#ifdef VISITORS_LAB_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
template<class T> T& Field(void* object, unsigned offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}
using Load = void(__thiscall*)(void*, int);
using Call = void(__thiscall*)(void*);
using Insert = void*(__thiscall*)(void*, int, int, int, int, void*);
using FindObject = void*(__thiscall*)(void*, const char**, void*);
auto nativeLoad = reinterpret_cast<Load>(Native(0x00529BB4));
auto nativeUpdate = reinterpret_cast<Call>(Native(0x00531B8D));
auto nativeClear = reinterpret_cast<Call>(Native(0x005294CE)); // CField::OnLeave, no stack arguments
auto nativeInsert = reinterpret_cast<Insert>(Native(0x00A45DF9));
const auto findObject = reinterpret_cast<FindObject>(Native(0x0064527E));
const auto clockNow = reinterpret_cast<unsigned(__cdecl*)()>(Native(0x00987257));

struct Platform {
    std::string name;
    Motion motion;
    int initialX = 0, initialY = 0;
    int localX = 0, localY = 0;
    std::vector<int> serials;
    std::vector<void*> footholds;
    // Borrowed from the field; no COM/native pointer survives Clear/Load.
    void* layer = nullptr;
};
struct State {
    void* field = nullptr;
    void* space = nullptr;
    bool loading = false, ready = false;
    std::vector<Platform> platforms;
    std::map<int, size_t> serials;
    unsigned updates = 0, moves = 0, firstUpdate = 0;
    bool reportedSkip = false, reportedMotion = false;
} state;
bool installed = false;

int Number(void* property, const std::wstring& key, int fallback = 0) {
    Value v; Get(property, key, &v.v);
    if (v.v.vt == VT_EMPTY || v.v.vt == VT_NULL) return fallback;
    if (v.v.vt != VT_I4) throw E_INVALIDARG;
    return v.v.lVal;
}
Object Child(void* property, const std::wstring& key) {
    Value v; Get(property, key, &v.v); return Query(v.v, PropertyIID());
}
void ReadPlatforms(void* root) {
    if (!root) throw E_POINTER;
    for (int layer = 0; layer < 8; ++layer) {
        auto group = Child(root, std::to_wstring(layer));
        if (!group.p) continue;
        auto objects = Child(group.p, L"obj");
        if (!objects.p) continue;
        for (const auto& key : Names(objects.p)) {
            auto object = Child(objects.p, key);
            if (!object.p || !Number(object.p, L"dynamic") || !Number(object.p, L"move")) continue;
            if (state.platforms.size() >= 64 || Number(object.p, L"start") != 2 ||
                Number(object.p, L"repeat") != 1) throw E_INVALIDARG;
            Platform p;
            Value name; Get(object.p, L"name", &name.v);
            if (name.v.vt != VT_BSTR || !name.v.bstrVal || SysStringLen(name.v.bstrVal) > 64) throw E_INVALIDARG;
            for (const wchar_t c : std::wstring(name.v.bstrVal)) {
                if (c < 32 || c > 126) throw E_INVALIDARG;
                p.name.push_back(static_cast<char>(c));
            }
            if (p.name.empty()) throw E_INVALIDARG;
            auto& m = p.motion;
            m.x = p.initialX = Number(object.p, L"x");
            m.y = p.initialY = Number(object.p, L"y");
            m.x1 = Number(object.p, L"x1"); m.x2 = Number(object.p, L"x2");
            m.y1 = Number(object.p, L"y1"); m.y2 = Number(object.p, L"y2");
            m.speed = Number(object.p, L"speed");
            m.reverseX = Number(object.p, L"reverseHorizontal") != 0;
            m.reverseY = Number(object.p, L"reverseVertical") != 0;
            for (int n : {m.x,m.y,m.x1,m.x2,m.y1,m.y2}) if (n < -32768 || n > 32767) throw E_INVALIDARG;
            if (m.speed < 1 || m.speed > 60000 || m.x < (std::min)(m.x1,m.x2) ||
                m.x > (std::max)(m.x1,m.x2) || m.y < (std::min)(m.y1,m.y2) || m.y > (std::max)(m.y1,m.y2)) throw E_INVALIDARG;
            const int count = Number(object.p, L"SN_count");
            if (count < 1 || count > 16) throw E_INVALIDARG;
            p.footholds.reserve(count);
            for (int i = 0; i < count; ++i) {
                const int sn = Number(object.p, L"SN" + std::to_wstring(i));
                if (sn <= 0 || !state.serials.emplace(sn, state.platforms.size()).second) throw E_INVALIDARG;
                p.serials.push_back(sn);
            }
            state.platforms.push_back(std::move(p));
        }
    }
    if (state.platforms.empty()) throw E_INVALIDARG;
}

void* __fastcall InsertHook(void* tree, void*, int x1, int y1, int x2, int y2, void* ref) {
    // v83 has no movable-foothold index handle. Index the entire ORIGINAL
    // travel envelope once. Native narrow-phase collision uses the current
    // endpoints, so this conservative broad phase adds no walkable surface.
    // One reference per foothold, no repeated tree allocations per frame.
    auto* space = *reinterpret_cast<void**>(Native(0x00BEBFA0));
    if (state.loading && space && tree == static_cast<unsigned char*>(space) + 0x44 && ref) {
        auto* fh = Field<void*>(ref, 4);
        auto found = fh ? state.serials.find(Field<int>(fh, 0x48)) : state.serials.end();
        if (found != state.serials.end()) {
            auto& p = state.platforms[found->second];
            // The laboratory's original lifts are independent horizontal
            // footholds. Do not attempt to move a connected terrain chain.
            if (x1 >= x2 || y1 != y2 || Field<int>(fh,0x4C) || Field<int>(fh,0x50)) {
                state.loading = false;
            } else {
                p.footholds.push_back(fh); state.space = space;
                x1 += (std::min)(p.motion.x1,p.motion.x2) - p.initialX;
                x2 += (std::max)(p.motion.x1,p.motion.x2) - p.initialX;
                y1 += (std::min)(p.motion.y1,p.motion.y2) - p.initialY;
                y2 += (std::max)(p.motion.y1,p.motion.y2) - p.initialY;
            }
        }
    }
    return nativeInsert(tree,x1,y1,x2,y2,ref);
}

void BindLayers(void* field) {
    for (auto& p : state.platforms) {
        if (p.footholds.size() != p.serials.size()) throw E_INVALIDARG;
        const char* key = p.name.c_str();
        auto* record = findObject(static_cast<unsigned char*>(field)+0xA0, &key, nullptr);
        if (!record) throw E_POINTER;
        auto* frames = Field<unsigned char*>(record,4);
        if (!frames || *reinterpret_cast<unsigned*>(frames-4) != 1) throw E_INVALIDARG;
        p.layer = Field<void*>(frames,8);
        if (!p.layer) throw E_POINTER;
        Value parentValue;
        Check(Method<HRESULT(__stdcall*)(void*,VARIANT*)>(p.layer,0x60)(p.layer,&parentValue.v));
        auto parent=Query(parentValue.v,VectorIID());
        // Keep the canvas's original local offset (including its origin).
        p.localX=Int(p.layer,0x20)-(parent.p?Int(parent.p,0x20):0);
        p.localY=Int(p.layer,0x28)-(parent.p?Int(parent.p,0x28):0);
        // v83 LoadObject (63C0D0..63C0F0) hides every named state layer.
        // These start=2 lifts have one static image and must be visible on
        // entry. Match native SetObjectState (642C78..642C9D), state zero.
        Object alpha;
        Check(Method<HRESULT(__stdcall*)(void*,void**)>(p.layer,0xE4)(p.layer,&alpha.p));
        if (!alpha.p) throw E_POINTER;
        Check(Method<HRESULT(__stdcall*)(void*,int,int)>(alpha.p,0x40)(alpha.p,255,0));
    }
}

void __fastcall LoadHook(void* field, void*, int mapId) {
    if (state.field) {
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "[visitor-lifts] reload oldField=%p newField=%p map=%d updates=%u moves=%u\n",
            state.field,field,mapId,state.updates,state.moves);
    }
    state = {};
    if (mapId == 502023000) {
        try {
            ReadPlatforms(Field<void*>(field,0x30));
            state.field = field; state.loading = true;
        } catch (...) {
            state = {};
            ClientLog::Append(ClientLog::Component::Lifecycle,"[visitor-lifts] invalid laboratory resource\n");
        }
    }
    nativeLoad(field,mapId);
    if (state.field != field) return;
    state.loading = false;
    try {
        BindLayers(field); state.ready = true;
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "[visitor-lifts] loaded map=%d platforms=%u footholds=%u field=%p space=%p update=%p\n",
            mapId,static_cast<unsigned>(state.platforms.size()),static_cast<unsigned>(state.serials.size()),
            field,state.space,Field<void*>(Field<void*>(field,0),0));
    } catch (...) {
        state = {};
        ClientLog::Append(ClientLog::Component::Lifecycle,"[visitor-lifts] incomplete native platform bindings\n");
    }
}

void Tick(void* field, unsigned now) {
    if (!state.field) return;
    auto* space = *reinterpret_cast<void**>(Native(0x00BEBFA0));
    if (!state.updates++) {
        state.firstUpdate = now;
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "[visitor-lifts] first-update field=%p boundField=%p space=%p boundSpace=%p ready=%d tick=%u\n",
            field,state.field,space,state.space,state.ready,now);
    }
    if (!state.ready || state.field != field || state.space != space) {
        if (!state.reportedSkip) {
            state.reportedSkip = true;
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "[visitor-lifts] update-skipped field=%p boundField=%p space=%p boundSpace=%p ready=%d\n",
                field,state.field,space,state.space,state.ready);
        }
        return;
    }
    try {
        for (auto& p : state.platforms) {
            auto next = p.motion;
            if (!next.Step(now)) { p.motion = next; continue; }
            VARIANT missing{}; missing.vt = VT_ERROR; missing.scode = DISP_E_PARAMNOTFOUND;
            // The native layer already inherits an origin at (initialX,
            // initialY). RelMove takes LOCAL coordinates, not map coordinates.
            // Passing the map position twice displaced the visible platform
            // away from its otherwise correctly moving collision surface.
            Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(p.layer,0x90)
                (p.layer,p.localX+next.x-p.initialX,p.localY+next.y-p.initialY,missing,missing));
            const int dx = next.x-p.motion.x, dy = next.y-p.motion.y;
            for (auto* fh : p.footholds) {
                Field<int>(fh,0x0C) += dx; Field<int>(fh,0x14) += dx;
                Field<int>(fh,0x10) += dy; Field<int>(fh,0x18) += dy;
            }
            // Translation preserves slope/length. v83 grounded physics
            // (9B3FD1 -> 9B1646) projects its relative standing/walking
            // position onto these endpoints, carrying the rider naturally.
            // Leave velocity, jump/rope state and movement packets native.
            p.motion = next;
            ++state.moves;
        }
        if (!state.reportedMotion && (now-state.firstUpdate >= 2000 || state.updates >= 150)) {
            state.reportedMotion = true;
            const auto& p = state.platforms.front();
            // A diagnostic getter must never stop an otherwise working lift.
            try {
                Object alpha;
                Check(Method<HRESULT(__stdcall*)(void*,void**)>(p.layer,0xE4)(p.layer,&alpha.p));
                if (!alpha.p) throw E_POINTER;
                ClientLog::Append(ClientLog::Component::Lifecycle,
                    "[visitor-lifts] motion-check updates=%u moves=%u tick=%u elapsed=%u name=%s start=(%d,%d) target=(%d,%d) vector=(%d,%d) foothold=(%d,%d) alpha=%d\n",
                    state.updates,state.moves,now,now-state.firstUpdate,p.name.c_str(),p.initialX,p.initialY,
                    p.motion.x,p.motion.y,Int(p.layer,0x20),Int(p.layer,0x28),
                    Field<int>(p.footholds.front(),0x0C),Field<int>(p.footholds.front(),0x10),Int(alpha.p,0x20));
            } catch (...) {
                ClientLog::Append(ClientLog::Component::Lifecycle,
                    "[visitor-lifts] motion-check vector unreadable updates=%u moves=%u\n",state.updates,state.moves);
            }
        }
    } catch (...) {
        state.ready = false;
        ClientLog::Append(ClientLog::Component::Lifecycle,"[visitor-lifts] stopped after graphics failure\n");
    }
}
void __fastcall UpdateHook(void* field, void*) {
    Tick(field,clockNow());
    nativeUpdate(field);
}
void __fastcall ClearHook(void* field, void*) {
    if (state.field) {
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "[visitor-lifts] leave field=%p boundField=%p clear=%d updates=%u moves=%u\n",
            field,state.field,state.field==field,state.updates,state.moves);
        if (state.field == field) state = {};
    }
    nativeClear(field);
}
}

bool VisitorsLabPlatforms::Install(HMODULE module) {
    if (installed) return true;
    if (!module || reinterpret_cast<DWORD>(module) != Native(0x00400000)) return false;
    // Validate every interception and the native layouts before any writes.
    const struct { DWORD address; unsigned char bytes[5]; } signatures[] = {
        {0x00529BB4,{0xB8,0x3C,0xA6,0xA8,0x00}},
        {0x00531B8D,{0xB8,0x80,0xB6,0xA8,0x00}},
        {0x005294CE,{0xA1,0x6C,0xBF,0xBE,0x00}},
        {0x00A45DF9,{0xB8,0x24,0xEA,0xAE,0x00}},
        {0x0064527E,{0x56,0x8B,0xF1,0x83,0x7E}}
    };
    for (const auto& s : signatures)
        if (std::memcmp(reinterpret_cast<void*>(Native(s.address)),s.bytes,sizeof(s.bytes))) return false;
    struct Hook { void** original; void* replacement; } hooks[] = {
        {reinterpret_cast<void**>(&nativeLoad),LoadHook},
        {reinterpret_cast<void**>(&nativeInsert),InsertHook},
        {reinterpret_cast<void**>(&nativeUpdate),UpdateHook},
        {reinterpret_cast<void**>(&nativeClear),ClearHook}
    };
    for (unsigned i=0;i<ARRAYSIZE(hooks);++i) {
        if (!Memory::SetHook(true,hooks[i].original,hooks[i].replacement)) {
            while (i) { --i; Memory::SetHook(false,hooks[i].original,hooks[i].replacement); }
            return false;
        }
    }
    installed = true;
    return true;
}
