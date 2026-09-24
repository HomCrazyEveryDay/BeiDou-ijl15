#include "stdafx.h"
#include "ClientCrashFixes.h"

#include "CrashReporter.h"
#include "Memory.h"
#include <cstring>
#include <cwchar>
#include "DreamCanvas.h"

namespace {

thread_local bool g_dreamFrame = false;
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
    CrashReporter::RecordEvent("dream.frame", "install result=%d", InstallDreamFrameScaling() ? 1 : 0);
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
