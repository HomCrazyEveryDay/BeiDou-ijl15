#include "stdafx.h"
#include "ChairCompatibility.h"
#include "ClientLog.h"
#include "ModernCapeEffects.h"
#include "ModernChairResources.h"

#include <array>
#include <cstring>
#include <oaidl.h>

namespace {
constexpr DWORD kCUserUpdate = 0x00930B27;
constexpr DWORD kSetActivePortableChair = 0x0093C7C3;
constexpr size_t kBodyOriginOffset = 0x88 + 0x10B8;
constexpr size_t kRelMoveVtableIndex = 36;
// CUser::GetEffect(10) returns user+26FC; its active item is at +8.
constexpr size_t kActiveChairOffset = 0x2704;
constexpr int kFiveColorSeaChair = 3015060;
constexpr int kFiveColorSeaBodyX = -12;
constexpr int kFiveColorSeaBodyY = -112;
constexpr int kGiantPinkBeanChair = 3010070;
constexpr int kGiantPinkBeanBodyX = 0;
constexpr int kGiantPinkBeanBodyY = -200;

struct ChairOffset {
	void* user = nullptr;
	int itemId = 0;
	int x = 0;
	int y = 0;
	unsigned samples = 0;
	DWORD lastSample = 0;
};

std::array<ChairOffset, 64> g_offsets{};
using RelMove = HRESULT(__stdcall*)(void* vector, int x, int y, VARIANT time, VARIANT type);
struct HiddenEffect {MixedDyeResources::Object layer;int visible=0;};
struct ChairState {int id=0;ModernChairResources::Settings settings;std::vector<HiddenEffect> hidden;};
std::map<void*,ChairState> g_chairs;
void RestoreEffects(ChairState& state) {
    for(auto& effect:state.hidden) {
        MixedDyeResources::Method<HRESULT(__stdcall*)(void*,int)>(effect.layer.p,0x11c)(effect.layer.p,effect.visible);
    }
    state.hidden.clear();
}
void HideItemEffects(void* user,ChairState& state) {
    using namespace MixedDyeResources;
    if(!state.settings.effect)return;
    // CUser::GetEffect(6)=user+2258. SetItemEffect(93ABE2) builds twelve
    // 24h-byte entries; each owns two layers at +14/+20 (93FC93).
    // Chair layers belong to group 10 and must remain visible.
    for(int i=0;i<12;++i)for(int slot:{0x14,0x20}) {
        auto layer=*reinterpret_cast<void**>(static_cast<BYTE*>(user)+0x2258+i*0x24+slot);
        if(!layer)continue;
        bool duplicate=false;for(auto& previous:state.hidden)if(previous.layer.p==layer)duplicate=true;
        if(duplicate)continue;
        try {
            HiddenEffect effect;effect.visible=Int(layer,0x118);
            static_cast<IUnknown*>(layer)->AddRef();effect.layer.p=layer;
            state.hidden.push_back(std::move(effect));
            Check(Method<HRESULT(__stdcall*)(void*,int)>(layer,0x11c)(layer,0));
        }catch(...){}
    }
}
using RefreshAvatar=void(__thiscall*)(void*,int);
RefreshAvatar g_refreshAvatar=reinterpret_cast<RefreshAvatar>(0x004515BD);
using PetCall=void(__thiscall*)(void*);
PetCall g_petUpdate=reinterpret_cast<PetCall>(0x0070403D);
// This destructor's native this pointer is CPet+8 (703171, 7032F1).
PetCall g_petDestroy=reinterpret_cast<PetCall>(0x00703163);
PetCall g_petPrepare=reinterpret_cast<PetCall>(0x00703D15);
void** g_petBasicActions=reinterpret_cast<void**>(0x00BEC21C);
void** g_chairResourceManager=reinterpret_cast<void**>(0x00BF14E8);
struct PetSeatState {
    void* avatar=nullptr;
    int chair=0,petId=0,action=-1;DWORD until=0,random=0;
    ModernChairResources::Seat seat;
    MixedDyeResources::Object layer;
    MixedDyeResources::Value origin;
    int x=0,y=0,flip=0;
};
std::map<void*,PetSeatState> g_petSeats;

void __fastcall RefreshAvatarHook(void* avatar,void*,int force) {
    auto found=g_chairs.find(avatar);
    if(found==g_chairs.end()) {g_refreshAvatar(avatar,force);return;}
    ModernChairResources::RefreshLook(avatar,found->second.settings,force,g_refreshAvatar);
}

void RestorePetLayer(PetSeatState& state) {
    using namespace MixedDyeResources;
    if(!state.layer.p)return;
    Check(Method<HRESULT(__stdcall*)(void*,VARIANT)>(state.layer.p,0x64)(state.layer.p,state.origin.v));
    VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    Check(Method<RelMove>(state.layer.p,0x90)(state.layer.p,state.x,state.y,missing,missing));
    Check(Method<HRESULT(__stdcall*)(void*,int)>(state.layer.p,0xd8)(state.layer.p,state.flip));
    DreamCanvas::Release(state.layer.p);state.layer.p=nullptr;
    ResourceVariant::Clear(&state.origin.v);
}

int PetAction(void* pet,const std::wstring& name) {
    // CPetTemplate+3C contains the extra action names, following the nine
    // common actions at BEC21C (40E1D3, 70D714). Native action slots total 24.
    auto data=*reinterpret_cast<BYTE**>(static_cast<BYTE*>(pet)+0x88);if(!data)return -1;
    auto matches=[&](void* record){return record && *static_cast<BSTR*>(record) && name==*static_cast<BSTR*>(record);};
    auto basic=g_petBasicActions;
    for(int i=0;i<9;++i)if(matches(basic[i]))return i;
    auto extra=*reinterpret_cast<void***>(data+0x3c);
    int count=extra?*reinterpret_cast<int*>(reinterpret_cast<BYTE*>(extra)-4):0;
    if(count<0 || count>256)return -1;
    for(int i=0;i<(std::min)(15,count);++i)if(matches(extra[i]))return i+9;
    return -1;
}

void __fastcall PetUpdateHook(void* pet,void*) {
    using namespace MixedDyeResources;
    auto found=g_petSeats.find(pet);
    if(found!=g_petSeats.end())try{RestorePetLayer(found->second);}catch(...){g_petSeats.erase(found);found=g_petSeats.end();}
    g_petUpdate(pet);
    auto bytes=static_cast<BYTE*>(pet);auto user=*reinterpret_cast<BYTE**>(bytes+0x94);
    auto chair=user?g_chairs.find(user+0x88):g_chairs.end();
    int slot=*reinterpret_cast<int*>(bytes+0x9c);
    auto petTemplate=*reinterpret_cast<BYTE**>(bytes+0x88);
    int petId=petTemplate?*reinterpret_cast<int*>(petTemplate):0;
    bool active=chair!=g_chairs.end() && slot>=0 && slot<3
        && *reinterpret_cast<int*>(user+kActiveChairOffset)==chair->second.id;
    if(found!=g_petSeats.end() && (!active || found->second.chair!=chair->second.id || found->second.petId!=petId)) {
        *reinterpret_cast<int*>(bytes+0xf8)=-1;g_petPrepare(pet);
        g_petSeats.erase(found);found=g_petSeats.end();
    }
    if(!active)return;
    try {
        if(found==g_petSeats.end()) {
            if(g_petSeats.size()>=192)return;
            auto& seat=chair->second.settings.seats[slot];
            if(!ModernChairResources::PetFits(petId,seat,*g_chairResourceManager))return;
            PetSeatState state;state.avatar=user+0x88;state.chair=chair->second.id;state.petId=petId;state.seat=seat;
            state.random=GetTickCount() ^ reinterpret_cast<DWORD>(pet);
            found=g_petSeats.emplace(pet,std::move(state)).first;
        }
        auto& state=found->second;DWORD now=GetTickCount();
        if(!state.until || static_cast<LONG>(now-state.until)>=0) {
            auto random=[&](){state.random=state.random*1664525u+1013904223u;return state.random;};
            std::vector<std::pair<int,int>> actions;int total=0;
            for(auto& action:state.seat.actions){int index=PetAction(pet,action.first);if(index>=0){actions.emplace_back(index,action.second);total+=action.second;}}
            state.action=PetAction(pet,L"stand0");
            if(total){int roll=random()%total;for(auto action:actions){if(roll<action.second){state.action=action.first;break;}roll-=action.second;}}
            state.until=now+state.seat.minGap+random()%(state.seat.maxGap-state.seat.minGap+1);
        }
        if(state.action>=0 && *reinterpret_cast<int*>(bytes+0xf8)!=state.action) {
            *reinterpret_cast<int*>(bytes+0xf8)=state.action;g_petPrepare(pet);
        }
        void* layer=*reinterpret_cast<void**>(bytes+0x124);
        void* origin=*reinterpret_cast<void**>(user+0x88+0x10b0);
        if(!layer || !origin)return;
        static_cast<IUnknown*>(layer)->AddRef();state.layer.p=layer;
        Check(Method<HRESULT(__stdcall*)(void*,VARIANT*)>(layer,0x60)(layer,&state.origin.v));
        state.x=Int(layer,0x20);state.y=Int(layer,0x28);state.flip=Int(layer,0xd4);
        int flip=0;ModernCapeEffects::actionFor(user+0x88,&flip);
        Check(Method<HRESULT(__stdcall*)(void*,VARIANT)>(layer,0x64)(layer,ModernCapeEffects::Borrow(origin)));
        VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
        Check(Method<RelMove>(layer,0x90)(layer,flip?-state.seat.x:state.seat.x,state.seat.y,missing,missing));
        Check(Method<HRESULT(__stdcall*)(void*,int)>(layer,0xd8)(layer,flip ^ state.seat.reverse));
    }catch(...){if(found!=g_petSeats.end())try{RestorePetLayer(found->second);}catch(...){};}
}

void __fastcall PetDestroyHook(void* adjusted,void*) {
    g_petSeats.erase(static_cast<BYTE*>(adjusted)-8);g_petDestroy(adjusted);
}

void LogChair(const char* phase, void* user, int itemId, int x, int y,
	void* vector = nullptr, HRESULT result = S_OK)
{
	static const bool enabled = [] {
		char value[8]{};
		GetEnvironmentVariableA("BEIDOU_CHAIR_LOG", value, sizeof(value));
		return value[0] != '0';
	}();
	static unsigned records = 0;
	if (!enabled || records >= 256) return;
	const DWORD error = GetLastError();
	++records;
	ClientLog::Append(ClientLog::Component::Trace,
		"event=chair_offset phase=%s user=%p item=%d x=%d y=%d vector=%p hr=0x%08lX tick=%lu",
		phase, user, itemId, x, y, vector, static_cast<unsigned long>(result), GetTickCount());
	SetLastError(error);
}

using CUserUpdate = void(__thiscall*)(void* user);
using SetActivePortableChair = void(__thiscall*)(void* user, int itemId);

CUserUpdate g_userUpdate = reinterpret_cast<CUserUpdate>(kCUserUpdate);
SetActivePortableChair g_setActivePortableChair = reinterpret_cast<SetActivePortableChair>(kSetActivePortableChair);

ChairOffset* FindOffset(void* user)
{
	for (auto& offset : g_offsets) {
		if (offset.user == user) {
			return &offset;
		}
	}
	return nullptr;
}

void ClearOffset(void* user)
{
	if (auto* offset = FindOffset(user)) {
		*offset = {};
	}
}

void SetOffset(void* user, int itemId, int x, int y)
{
	if (auto* offset = FindOffset(user)) {
		offset->x = x;
		offset->itemId = itemId;
		offset->y = y;
		offset->samples = 0;
		offset->lastSample = 0;
		return;
	}

	for (auto& offset : g_offsets) {
		if (offset.user == nullptr) {
			offset = {user, itemId, x, y};
			return;
		}
	}
}

bool TryGetBodyOffset(int itemId, int& x, int& y)
{
	// Modern chairs carry their own body position. Use the real vector so new
	// chairs need no item-specific DLL table or coordinate edits in the resource.
	if (itemId / 10000 == 301) {
		try {
			wchar_t path[96];
			swprintf_s(path, L"Item/Install/0301.img/%08d/info/bodyRelMove", itemId);
			auto value = ModernCapeEffects::Resource(path);
			auto vector = MixedDyeResources::Query(value.v, MixedDyeResources::VectorIID());
			if (vector.p) {
				x = MixedDyeResources::Int(vector.p, 0x20);
				y = MixedDyeResources::Int(vector.p, 0x28);
				if (abs(x) <= 2048 && abs(y) <= 2048) return true;
			}
		} catch (...) {}
	}
	switch (itemId) {
	case kFiveColorSeaChair:
		x = kFiveColorSeaBodyX;
		y = kFiveColorSeaBodyY;
		return true;
	case kGiantPinkBeanChair:
		x = kGiantPinkBeanBodyX;
		y = kGiantPinkBeanBodyY;
		return true;
	default:
		return false;
	}
}

void ApplyBodyOffset(void* user, int x, int y, bool sample)
{
	__try {
		void* vector = *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(user) + kBodyOriginOffset);
		if (vector == nullptr) {
			if (sample) LogChair("null_vector", user, 0, x, y);
			return;
		}

		void** vtable = *reinterpret_cast<void***>(vector);
		if (vtable == nullptr || vtable[kRelMoveVtableIndex] == nullptr) {
			if (sample) LogChair("null_relmove", user, 0, x, y, vector);
			return;
		}

		VARIANT missing{};
		missing.vt = VT_ERROR;
		missing.scode = DISP_E_PARAMNOTFOUND;
		const HRESULT result = reinterpret_cast<RelMove>(vtable[kRelMoveVtableIndex])(vector, x, y, missing, missing);
		if (sample) LogChair("relmove", user, 0, x, y, vector, result);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogChair("exception", user, 0, x, y, nullptr, static_cast<HRESULT>(GetExceptionCode()));
		ClearOffset(user);
	}
}

void __fastcall CUserUpdateHook(void* user, void*)
{
	auto before=g_chairs.find(static_cast<BYTE*>(user)+0x88);
	if(before!=g_chairs.end())RestoreEffects(before->second);
	g_userUpdate(user);
	auto avatar=static_cast<BYTE*>(user)+0x88;
	auto chair=g_chairs.find(avatar);
	if(chair!=g_chairs.end() && *reinterpret_cast<int*>(static_cast<BYTE*>(user)+kActiveChairOffset)!=chair->second.id) {
		g_chairs.erase(chair);g_refreshAvatar(avatar,1);
	}
	else if(chair!=g_chairs.end())HideItemEffects(user,chair->second);
	if (auto* offset = FindOffset(user)) {
		if (*reinterpret_cast<int*>(static_cast<BYTE*>(user) + kActiveChairOffset) != offset->itemId) {
			ClearOffset(user);
			return;
		}
		const DWORD now = GetTickCount();
		const bool sample = offset->samples < 5 && (offset->samples == 0 || now - offset->lastSample >= 1000);
		if (sample) { ++offset->samples; offset->lastSample = now; }
		ApplyBodyOffset(user, offset->x, offset->y, sample);
	}
}

void __fastcall SetActivePortableChairHook(void* user, void*, int itemId)
{
	LogChair("set_begin", user, itemId, 0, 0);
	auto before=g_chairs.find(static_cast<BYTE*>(user)+0x88);
	if(before!=g_chairs.end())RestoreEffects(before->second);
	g_setActivePortableChair(user, itemId);
	auto avatar=static_cast<BYTE*>(user)+0x88;
	bool hadChair=g_chairs.erase(avatar)!=0;
	if(itemId/10000==301 && *reinterpret_cast<int*>(static_cast<BYTE*>(user)+kActiveChairOffset)==itemId && g_chairs.size()<512) {
		try {g_chairs.emplace(avatar,ChairState{itemId,ModernChairResources::Read(itemId)});}catch(...){}
	}
	if(hadChair || g_chairs.count(avatar))RefreshAvatarHook(avatar,nullptr,1);
	auto current=g_chairs.find(avatar);if(current!=g_chairs.end())HideItemEffects(user,current->second);
	int x = 0;
	int y = 0;
	if (TryGetBodyOffset(itemId, x, y)) {
		SetOffset(user, itemId, x, y);
		LogChair(FindOffset(user) ? "tracked" : "table_full", user, itemId, x, y);
	}
	else {
		ClearOffset(user);
		LogChair("cleared", user, itemId, 0, 0);
	}
}

bool MatchesExpectedClient()
{
	const unsigned char updatePrologue[] = {0xB8, 0x63, 0xBC, 0xAD, 0x00, 0xE8};
	const unsigned char chairPrologue[] = {0xB8, 0x84, 0xCF, 0xAD, 0x00, 0xE8};
	const BYTE refresh[]={0xb8,0x6c,0xc0,0xa7,0,0xe8},petDestroy[]={0xb8,0x46,0xd9,0xaa,0,0xe8};
	return std::memcmp(reinterpret_cast<const void*>(kCUserUpdate), updatePrologue, sizeof(updatePrologue)) == 0
		&& std::memcmp(reinterpret_cast<const void*>(kSetActivePortableChair), chairPrologue, sizeof(chairPrologue)) == 0
		&& !std::memcmp(reinterpret_cast<void*>(g_refreshAvatar),refresh,sizeof(refresh))
		&& !std::memcmp(reinterpret_cast<void*>(g_petDestroy),petDestroy,sizeof(petDestroy))
		&& *reinterpret_cast<DWORD*>(0x00AFD5E8)==0x0070403D;
}
}

bool ChairCompatibility::Install()
{
	if (!MatchesExpectedClient()) {
		LogChair("signature_mismatch", nullptr, 0, 0, 0);
		return false;
	}

	if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_userUpdate), CUserUpdateHook)) {
		LogChair("update_hook_failed", nullptr, 0, 0, 0);
		return false;
	}
	if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_setActivePortableChair), SetActivePortableChairHook)) {
		LogChair("chair_hook_failed", nullptr, 0, 0, 0);
		Memory::SetHook(false, reinterpret_cast<void**>(&g_userUpdate), CUserUpdateHook);
		return false;
	}
	bool refreshed=Memory::SetHook(true,reinterpret_cast<void**>(&g_refreshAvatar),RefreshAvatarHook);
	bool petDestroyed=refreshed && Memory::SetHook(true,reinterpret_cast<void**>(&g_petDestroy),PetDestroyHook);
	DWORD protection=0;
	if(!petDestroyed || !VirtualProtect(reinterpret_cast<void*>(0x00AFD5E8),4,PAGE_READWRITE,&protection)) {
		if(petDestroyed)Memory::SetHook(false,reinterpret_cast<void**>(&g_petDestroy),PetDestroyHook);
		if(refreshed)Memory::SetHook(false,reinterpret_cast<void**>(&g_refreshAvatar),RefreshAvatarHook);
		Memory::SetHook(false,reinterpret_cast<void**>(&g_setActivePortableChair),SetActivePortableChairHook);
		Memory::SetHook(false,reinterpret_cast<void**>(&g_userUpdate),CUserUpdateHook);
		return false;
	}
	*reinterpret_cast<void**>(0x00AFD5E8)=reinterpret_cast<void*>(PetUpdateHook);
	DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(0x00AFD5E8),4,protection,&ignored);
	LogChair("installed", nullptr, 0, 0, 0);
	ModernCapeEffects::OnAvatarDestroyed() = [](void* avatar) {
		ClearOffset(static_cast<BYTE*>(avatar) - 0x88);
		g_chairs.erase(avatar);
		for(auto it=g_petSeats.begin();it!=g_petSeats.end();) {
			if(it->second.avatar==avatar)it=g_petSeats.erase(it);else ++it;
		}
	};
	ModernCapeEffects::HiddenByChair()=[](void* avatar,int item) {
		auto found=g_chairs.find(avatar);
		return found!=g_chairs.end() && (found->second.settings.effect ||
			(item/10000==110 && found->second.settings.cape));
	};
	return true;
}
