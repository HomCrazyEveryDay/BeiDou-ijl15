#include "stdafx.h"
#include "ClientCrashFixes.h"

#include "CrashReporter.h"
#include "Memory.h"
#include <cstring>
#include <cwchar>
#include "DreamCanvas.h"
#include "IncubationCanvas.h"

namespace {

// v83 task-dialog continuation: 00959F4D -> 00A26E31 -> 00716FE1.
// The latter unconditionally dereferences CUserLocal (00BEBF98) + 4.
// Cash shop has no CUserLocal, but a dialog opened in the field can survive
// the transition. Guard before allocating the continuation's task object.
using ContinueQuest = void(__thiscall*)(void*, unsigned int, int, int);
ContinueQuest g_continueQuest = reinterpret_cast<ContinueQuest>(0x00A26E31);

void __fastcall ContinueQuestInField(void* self, void*, unsigned int quest, int npc, int flags)
{
    if (*reinterpret_cast<void**>(0x00BEBF98) == nullptr) {
        CrashReporter::RecordEvent("quest.context", "skip continuation without local user quest=%u", quest & 0xFFFF);
        return;
    }
    g_continueQuest(self, quest, npc, flags);
}

bool InstallQuestContextGuard()
{
    const BYTE expected[] = {0xB8, 0x72, 0xCC, 0xAE, 0x00};
    if (std::memcmp(reinterpret_cast<void*>(0x00A26E31), expected, sizeof(expected)) != 0)
        return false;
    return Memory::SetHook(true, reinterpret_cast<void**>(&g_continueQuest), ContinueQuestInField);
}

thread_local bool g_dreamFrame = false;
thread_local IncubationCanvas::Part g_incubationPart = IncubationCanvas::Part::None;
// v83 scene cue Update: type at +0, ZXString<wchar_t> visual at +4,
// x/y at +10/+14. Type=0 resolves the property and synchronously creates
// the animation through 004398F6 -> 0043EA3E -> InsertCanvas.
using SceneCueUpdate = int(__thiscall*)(void*, int);
auto g_updateSceneCue = reinterpret_cast<SceneCueUpdate>(0x0043A8C4);

int __fastcall UpdateIncubationCue(void* self, void*, int tick) {
    struct Restore {
        IncubationCanvas::Part previous;
        ~Restore() { g_incubationPart = previous; }
    } restore{g_incubationPart};
    const auto cue = static_cast<const unsigned char*>(self);
    g_incubationPart = *reinterpret_cast<const int*>(cue) == 0
        && *reinterpret_cast<const int*>(cue + 0x10) == 0
        && *reinterpret_cast<const int*>(cue + 0x14) == 0
        ? IncubationCanvas::Identify(*reinterpret_cast<const wchar_t* const*>(cue + 4))
        : IncubationCanvas::Part::None;
    return g_updateSceneCue(self, tick);
}
using LayerFn = DWORD*(__cdecl*)(DWORD*,DWORD,int,DWORD,int,int,DWORD,int,int,int);
auto g_createBackgroundLayer = reinterpret_cast<LayerFn>(0x0043EA3E);
using InsertFn = void*(__fastcall*)(void*,void*,void*,void*,void*,void*,void*,void*,void*);
auto g_insertCanvas = reinterpret_cast<InsertFn>(0x00426BAB);

// CMapLoadable::LoadBack: -38 is the bS _bstr_t; -3C is ani/%d.
// Restrict adaptation to the three original full-screen dream overlays.
void __cdecl SelectDreamFrame(const unsigned char* frame) {
    const auto nameHolder=*reinterpret_cast<const wchar_t* const* const*>(frame-0x38);
    const wchar_t* name=nameHolder ? *nameHolder : nullptr;
    const wchar_t* path=*reinterpret_cast<const wchar_t* const*>(frame-0x3c);
    g_dreamFrame=name && path && std::wcscmp(name,L"dragonDream")==0
        && (!std::wcscmp(path,L"ani/6") || !std::wcscmp(path,L"ani/7") || !std::wcscmp(path,L"ani/8"));
}

DWORD* __cdecl CreateDreamLayer(DWORD* result,DWORD property,int flip,DWORD origin,
    int x,int y,DWORD overlay,int z,int alpha,int magnification) {
    struct Reset { ~Reset(){g_dreamFrame=false;} } reset;
    return g_createBackgroundLayer(result,property,flip,origin,x,y,overlay,z,alpha,magnification);
}

__declspec(naked) void DreamLayerCall() {
    __asm {
        pushad
        push ebp
        call SelectDreamFrame
        add esp,4
        popad
        jmp CreateDreamLayer
    }
}

void* __fastcall InsertDreamCanvas(void* self,void*,void* result,void* canvas,
    void* delay,void* alpha0,void* alpha1,void* zoom0,void* zoom1) {
    void* scaled=nullptr;
    if(g_dreamFrame) {
        auto factory=*reinterpret_cast<DreamCanvas::Factory*>(0x00BF0CC0);
        scaled=DreamCanvas::Scale(canvas,Client::m_nGameWidth,Client::m_nGameHeight,factory);
    } else if(g_incubationPart != IncubationCanvas::Part::None) {
        auto factory=*reinterpret_cast<DreamCanvas::Factory*>(0x00BF0CC0);
        scaled=IncubationCanvas::Scale(canvas,Client::m_nGameWidth,Client::m_nGameHeight,g_incubationPart,factory);
    }
    struct Release { void* value; ~Release(){DreamCanvas::Release(value);} } release{scaled};
    return g_insertCanvas(self,nullptr,result,scaled ? scaled : canvas,delay,alpha0,alpha1,zoom0,zoom1);
}

bool InstallDreamFrameScaling() {
    const BYTE expected[]={0xe8,0x68,0x10,0xe0,0xff};
    const BYTE insertPrologue[]={0x55,0x8b,0xec,0x83,0xec,0x14};
    if(std::memcmp(reinterpret_cast<void*>(0x0063D9D1),expected,sizeof(expected))
        || std::memcmp(reinterpret_cast<void*>(0x00426BAB),insertPrologue,sizeof(insertPrologue))) return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&g_insertCanvas),InsertDreamCanvas)) return false;
    Memory::WriteInt(0x0063D9D2,reinterpret_cast<DWORD>(&DreamLayerCall)-0x0063D9D6);
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x0063D9D1),5);
    return true;
}

bool InstallIncubationScaling() {
    const BYTE expected[] = {0xb8,0x09,0xa0,0xa7,0x00,0xe8,0xca,0x62,0x62,0x00};
    if (std::memcmp(reinterpret_cast<void*>(0x0043A8C4), expected, sizeof(expected))) return false;
    return Memory::SetHook(true, reinterpret_cast<void**>(&g_updateSceneCue), UpdateIncubationCue);
}

// Effect_Catch's success branch already plays a WzProperty frame sequence.
// The shipped BasicEff.img has a sequence at Catch/Fail too, whereas v83's
// failure branch queries IWzCanvas directly and passes null to InsertCanvas.
DWORD g_catchAnimationResume = 0x00438FDD;

__declspec(naked) void AnimatedCatchFailure()
{
	__asm {
		// Preserve EAX (the temporary string destination on the native stack).
		mov dword ptr [ebp - 54h], esp
		push 0E68h // StringPool: Catch/Fail; success uses 0E67h.
		jmp dword ptr [g_catchAnimationResume]
	}
}

bool InstallAnimatedCatchFailure()
{
	// Redirect only the zero/success=false branch. Its entry stack and the
	// success branch's SEH state are identical at this point (state 9).
	auto branch = reinterpret_cast<BYTE*>(0x00438FCF);
	const BYTE expected[] = {0x0F, 0x84, 0x63, 0x01, 0x00, 0x00};
	const BYTE resume[] = {0x50, 0xE8, 0x22, 0x58, 0x36, 0x00};
	if (std::memcmp(branch, expected, sizeof(expected)) != 0
		|| std::memcmp(reinterpret_cast<void*>(g_catchAnimationResume), resume, sizeof(resume)) != 0) {
		return false;
	}
	DWORD oldProtect = 0;
	if (!VirtualProtect(branch, sizeof(expected), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
	const DWORD displacement = reinterpret_cast<DWORD>(&AnimatedCatchFailure)
		- (reinterpret_cast<DWORD>(branch) + sizeof(expected));
	std::memcpy(branch + 2, &displacement, sizeof(displacement));
	FlushInstructionCache(GetCurrentProcess(), branch, sizeof(expected));
	DWORD ignored = 0;
	VirtualProtect(branch, sizeof(expected), oldProtect, &ignored);
	return true;
}

// Character-selection UI creation handler. The single argument is the base
// address of 15 fixed-size (0x2AC-byte) character records.
using InitializeCharacterList = void(__thiscall*)(void* self, void* records);
InitializeCharacterList g_originalInitializeCharacterList =
	reinterpret_cast<InitializeCharacterList>(0x006042DC);

void __fastcall HookInitializeCharacterList(void* self, void*, void* records)
{
	if (records == nullptr) {
		// The native function immediately turns a null base into
		// 0, 0x2AC, 0x558, ... and dereferences the first entry while drawing.
		// Skip the incomplete UI initialization as one unit so later slots do not
		// receive those offset-derived invalid pointers.
		CrashReporter::RecordEvent(
			"charSelect.guard",
			"skip null character-list records self=%p",
			self);
		return;
	}

	g_originalInitializeCharacterList(self, records);
}

} // namespace

void ClientCrashFixes::Install()
{
    CrashReporter::RecordEvent("quest.context", "install result=%d", InstallQuestContextGuard() ? 1 : 0);
    const bool canvasScaling = InstallDreamFrameScaling();
    CrashReporter::RecordEvent("dream.frame", "install result=%d", canvasScaling ? 1 : 0);
    CrashReporter::RecordEvent("incubation.frame", "install result=%d",
        canvasScaling && InstallIncubationScaling() ? 1 : 0);
	CrashReporter::RecordEvent("magnet.animation", "install result=%d",
		InstallAnimatedCatchFailure() ? 1 : 0);
	const bool installed = Memory::SetHook(
		true,
		reinterpret_cast<void**>(&g_originalInitializeCharacterList),
		HookInitializeCharacterList);
	CrashReporter::RecordEvent(
		"charSelect.guard",
		"install result=%d trampoline=%p",
		installed ? 1 : 0,
		reinterpret_cast<void*>(g_originalInitializeCharacterList));
}
