#include "stdafx.h"
#include "MineralBagWnd.h"
#include "CrashReporter.h"
#include <array>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cwchar>

namespace
{
constexpr DWORD kCWndConstructor = 0x009DE383;
constexpr DWORD kCWndCreateWnd = 0x009DE4D2;
constexpr DWORD kCWndSetBackgrnd = 0x009E0AB2;
constexpr DWORD kCWndManPtr = 0x00BEC20C;
constexpr size_t kCWndManFocusOffset = 0x88;
constexpr DWORD kItemInfo = 0x00BE78D8;
constexpr size_t kNativeWindowSize = 0x100;
constexpr int kBagId = 4330030;
constexpr int kWidth = 148;
constexpr int kHeight = 196;
constexpr int kGridLeft = 8;
constexpr int kGridTop = 25;
constexpr int kCellPitch = 33;
constexpr int kCloseX = 131;
constexpr int kCloseY = 6;
constexpr int kCloseSize = 12;
constexpr int kTitleBarHeight = 24;
enum class HitTarget { None, Exit };
struct ButtonCanvases { DWORD normal = 0, pressed = 0, disabled = 0, mouseOver = 0; };
struct CanvasVariant { DWORD data[4]; };
struct Entry { int id = 0; int quantity = 0; std::string name; DWORD icon = 0; };
struct State
{
    unsigned char* window = nullptr;
    DWORD primaryVtable[14]{}, uiVtable[19]{}, refVtable[1]{};
    bool installed = false, shown = false, tooltipCreated = false;
    bool windowReady = false, windowFailed = false;
    bool pending = false, haveSnapshot = false, reopen = false;
    bool escapeDown = false;
    bool dragging = false;
    DWORD sentAt = 0;
    int session = 0, revision = 0, bagSlot = 0, hovered = -1;
    int count = 0;
    int windowX = 0, windowY = 0, dragAnchorX = 0, dragAnchorY = 0;
    HitTarget hoveredTarget = HitTarget::None, pressedTarget = HitTarget::None;
    unsigned char tooltip[1304]{};
    DWORD tooltipItem[2]{};
    std::array<Entry, 20> entries{};
    std::array<DWORD, 10> numbers{};
    ButtonCanvases exit;
} g_state;
using CreateWnd_t = void(__fastcall*)(void*, void*, int, int, int, int, int, int, void*, int);
auto g_createWnd = reinterpret_cast<CreateWnd_t>(kCWndCreateWnd);
auto g_constructWnd = reinterpret_cast<void*(__fastcall*)(void*, void*)>(kCWndConstructor);
auto g_setBackgrnd = reinterpret_cast<void(__fastcall*)(void*, void*, DWORD, int, int)>(kCWndSetBackgrnd);
auto g_invalidateRect = reinterpret_cast<void(__fastcall*)(void*, void*, const RECT*)>(0x009E04C9);
auto g_drawWnd = reinterpret_cast<void(__fastcall*)(void*, void*, const RECT*)>(0x009E0502);
auto g_getCanvas = reinterpret_cast<DWORD*(__fastcall*)(void*, void*, DWORD*)>(0x00425C4C);
auto g_setFocus = reinterpret_cast<void(__fastcall*)(void*, void*, void*)>(0x009E3264);
auto g_constructZXStringW = reinterpret_cast<DWORD*(__fastcall*)(DWORD*, void*, const wchar_t*)>(0x00403382);
auto g_getItemIconCanvas = reinterpret_cast<DWORD*(__fastcall*)(DWORD, void*, DWORD*, DWORD, int, DWORD)>(0x005D3BD8);
auto g_clearToolTip = reinterpret_cast<void(__fastcall*)(void*, void*)>(0x008E6E23);
template<typename T> T ReadField(size_t offset) { return *reinterpret_cast<T*>(g_state.window + offset); }
bool IsShown() { return g_state.window && g_state.shown; }
void Invalidate();
void ClearToolTip();
void SetShow(bool show);
void __fastcall WindowDraw(void*, void*, const RECT*);
void __fastcall WindowOnKey(void*, void*, unsigned int, unsigned int);
int __fastcall WindowOnSetFocus(void*, void*, int);
void __fastcall WindowOnMouseButton(void*, void*, unsigned int, unsigned int, int, int);
int __fastcall WindowOnMouseMove(void*, void*, int, int);
void __fastcall WindowOnMouseEnter(void*, void*, int);
void __fastcall WindowSetShow(void*, void*, int);
int __fastcall WindowIsShown(void*, void*);
void __fastcall WindowOnIMEResult(void*, void*, const char*) {}

void SetHoverCursor(int cursor)
{
    __try
    {
        auto manager = *reinterpret_cast<unsigned char**>(kCWndManPtr);
        auto input = *reinterpret_cast<void**>(0x00BEC33C);
        // CWndMan owns the cursor while an item is being dragged.
        if (input && manager && !*reinterpret_cast<DWORD*>(manager + 0x98))
            reinterpret_cast<void(__thiscall*)(void*, int)>(0x0059A6D9)(input, cursor);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void PlayUiSound(int stringId)
{
    DWORD name = 0;
    __try
    {
        auto pool = reinterpret_cast<void*(__cdecl*)()>(0x0079E805)();
        reinterpret_cast<DWORD*(__thiscall*)(void*, DWORD*, int)>(0x00406276)(pool, &name, stringId);
        if (name) reinterpret_cast<void(__cdecl*)(const char*)>(0x00989588)(reinterpret_cast<const char*>(name));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (name) reinterpret_cast<void(__thiscall*)(DWORD*)>(0x0040265E)(&name);
}

void SetKeyboardFocus(bool focus)
{
    if (!g_state.window)
    {
        return;
    }
    __try
    {
        void* manager = *reinterpret_cast<void**>(kCWndManPtr);
        if (!manager)
        {
            return;
        }
        void* windowUi = g_state.window + 4;
        void* current = *reinterpret_cast<void**>(
            static_cast<unsigned char*>(manager) + kCWndManFocusOffset);
        if (focus)
        {
            if (current != windowUi)
            {
                g_setFocus(manager, nullptr, windowUi);
            }
        }
        else if (current == windowUi)
        {
            g_setFocus(manager, nullptr, nullptr);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("mineralBag.focus", "code=0x%08lX", GetExceptionCode());
    }
}

void SetLayerVisible(DWORD layer, bool show)
{
    if (!layer)
    {
        return;
    }
    __try
    {
        DWORD vtable = *reinterpret_cast<DWORD*>(layer);
        using PutVisible_t = DWORD(__stdcall*)(DWORD, int);
        reinterpret_cast<PutVisible_t>(*reinterpret_cast<DWORD*>(vtable + 0x11C))(
            layer, show ? 1 : 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void Invalidate()
{
    if (!g_state.window)
    {
        return;
    }
    __try
    {
        g_invalidateRect(g_state.window, nullptr, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("mineralBag.invalidate", "code=0x%08lX", GetExceptionCode());
    }
}

void ClearToolTip()
{
    if (!g_state.tooltipCreated)
    {
        return;
    }
    __try
    {
        g_clearToolTip(g_state.tooltip, nullptr);
        reinterpret_cast<void(__thiscall*)(DWORD*)>(0x004288C1)(g_state.tooltipItem);
        g_state.tooltipItem[0] = g_state.tooltipItem[1] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void ShowToolTip(int x, int y, const Entry& entry)
{
    if (!g_state.tooltipCreated || !g_state.window || !entry.id)
    {
        return;
    }
    const int absoluteX = g_state.windowX + x;
    const int absoluteY = g_state.windowY + y;
    DWORD options[11]{};
    bool optionsCreated = false;
    __try
    {
        auto itemInfo = *reinterpret_cast<void**>(kItemInfo);
        if (!itemInfo) return;
        // Build a display-only item; keep its native reference until the tooltip is cleared.
        reinterpret_cast<DWORD*(__thiscall*)(void*, DWORD*, int)>(0x005D5D95)(
            itemInfo, g_state.tooltipItem, entry.id);
        auto item = reinterpret_cast<unsigned char*>(g_state.tooltipItem[1]);
        if (!item) return;
        *reinterpret_cast<DWORD*>(item + 0x2C) =
            reinterpret_cast<DWORD(__fastcall*)(unsigned short, void*)>(0x004E824A)(
                static_cast<unsigned short>(entry.quantity), item + 0x28);
        reinterpret_cast<void(__thiscall*)(void*)>(0x00483EED)(options);
        optionsCreated = true;
        options[4] = 1; // Same inventory context as CUIItem::OnMouseMove.
        reinterpret_cast<void(__thiscall*)(void*, int, int, void*, void*, int, int, int, int)>(0x008F5B20)(
            g_state.tooltip, absoluteX, absoluteY, item, options, 0, 0, 0, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ClearToolTip();
    }
    if (optionsCreated) reinterpret_cast<void(__thiscall*)(void*)>(0x00483F18)(options);
}

void MoveWindow(int x, int y)
{
    if (!g_state.window)
    {
        return;
    }
    x = (std::max)(0, (std::min)(x, Client::m_nGameWidth - kWidth));
    y = (std::max)(0, (std::min)(y, Client::m_nGameHeight - kHeight));
    __try
    {
        DWORD layer = ReadField<DWORD>(0x18);
        if (!layer)
        {
            return;
        }
        DWORD vtable = *reinterpret_cast<DWORD*>(layer);
        CanvasVariant empty{};
        using RelMove_t = HRESULT(__stdcall*)(DWORD, int, int, CanvasVariant, CanvasVariant);
        reinterpret_cast<RelMove_t>(*reinterpret_cast<DWORD*>(vtable + 0x90))(
            layer, x, y, empty, empty);
        g_state.windowX = x;
        g_state.windowY = y;
        ClearToolTip();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_state.dragging = false;
        CrashReporter::RecordEvent("mineralBag.move", "code=0x%08lX", GetExceptionCode());
    }
}

DWORD LoadCanvas(const wchar_t* uol)
{
    unsigned char holder[kNativeWindowSize] = {};
    DWORD value = 0;
    __try
    {
        g_constructWnd(holder, nullptr);
        g_constructZXStringW(&value, nullptr, uol);
        g_setBackgrnd(holder, nullptr, value, 0, 0);
        DWORD canvas = *reinterpret_cast<DWORD*>(holder + 0x68);
        // Transfer the canvas reference before destroying the temporary native holder.
        *reinterpret_cast<DWORD*>(holder + 0x68) = 0;
        reinterpret_cast<void(__fastcall*)(void*, void*)>(0x009DE438)(holder + 8, nullptr);
        return canvas;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("mineralBag.canvas", "code=0x%08lX", GetExceptionCode());
        return 0;
    }
}

ButtonCanvases LoadButtonCanvases(const wchar_t* baseUol)
{
    ButtonCanvases canvases;
    wchar_t path[192] = {};
    swprintf_s(path, _countof(path), L"%ls/normal/0", baseUol);
    canvases.normal = LoadCanvas(path);
    swprintf_s(path, _countof(path), L"%ls/pressed/0", baseUol);
    canvases.pressed = LoadCanvas(path);
    swprintf_s(path, _countof(path), L"%ls/disabled/0", baseUol);
    canvases.disabled = LoadCanvas(path);
    swprintf_s(path, _countof(path), L"%ls/mouseOver/0", baseUol);
    canvases.mouseOver = LoadCanvas(path);
    return canvases;
}

DWORD LoadItemIcon(int itemId)
{
    DWORD itemInfo = 0;
    __try
    {
        itemInfo = *reinterpret_cast<DWORD*>(kItemInfo);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    if (!itemInfo)
    {
        return 0;
    }
    DWORD icon = 0;
    __try
    {
        // Use the same icon variant as the ordinary item inventory.
        g_getItemIconCanvas(itemInfo, nullptr, &icon, static_cast<DWORD>(itemId), 1, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        icon = 0;
    }
    return icon;
}

void DrawCanvas(DWORD destination, DWORD source, int x, int y)
{
    if (!destination || !source)
    {
        return;
    }
    __try
    {
        DWORD vtable = *reinterpret_cast<DWORD*>(destination);
        DWORD drawAddress = *reinterpret_cast<DWORD*>(vtable + 0x80);
        if (!drawAddress)
        {
            return;
        }
        CanvasVariant alpha{};
        // Match the native UI draw path: Ztl_variant_t(255, VT_I4). Passing
        // VT_EMPTY makes transparent source pixels replace the destination,
        // punching holes through previously drawn background layers.
        alpha.data[0] = 3;
        alpha.data[2] = 255;
        using DrawCanvas_t = DWORD(__stdcall*)(DWORD, int, int, DWORD, CanvasVariant);
        reinterpret_cast<DrawCanvas_t>(drawAddress)(destination, x, y, source, alpha);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void __fastcall WindowUpdate(void*, void*)
{
}

int __fastcall WindowNoopFourArgs(void*, void*, int, int, int, int)
{
    return 0;
}

void __fastcall WindowOnCreate(void*, void*, void*)
{
}

void __fastcall WindowOnButtonClicked(void*, void*, unsigned int)
{
}

const void* __fastcall WindowGetRTTI(void*, void*)
{
    return nullptr;
}

int __fastcall WindowIsKindOf(void*, void*, const void*)
{
    return 0;
}

void* __fastcall WindowRefDestructor(void* ref, void*, unsigned int)
{
    return static_cast<unsigned char*>(ref) - 8;
}

void InitializeVtables()
{
    const DWORD primaryTemplate[14] = {
        0x009E067E, 0x004243FE, 0x009DE7FB, 0x00A4F19C,
        0x00424408, 0x009DEB57, 0x009E00A6, 0x0042444E,
        0x00A4F236, 0x00424461, 0x009E03A6, 0x00A4F291,
        0x00424403, 0x004244B1,
    };
    const DWORD uiTemplate[19] = {
        0x00A4F261, 0x009E0369, 0x00424443, 0x00424446, 0x009E02AE,
        0x009E01C8, 0x004286F7, 0x004286FA, 0x004286FD, 0x00428701,
        0x00428704, 0x009E03C5, 0x009E0447, 0x00428708, 0x00428709,
        0x0042870C, 0x0042870F, 0x00A4F091, 0x00A4F097,
    };
    std::memcpy(g_state.primaryVtable, primaryTemplate, sizeof(primaryTemplate));
    std::memcpy(g_state.uiVtable, uiTemplate, sizeof(uiTemplate));
    g_state.primaryVtable[0] = reinterpret_cast<DWORD>(&WindowUpdate);
    g_state.primaryVtable[1] = reinterpret_cast<DWORD>(&WindowNoopFourArgs);
    g_state.primaryVtable[3] = reinterpret_cast<DWORD>(&WindowOnCreate);
    g_state.primaryVtable[8] = reinterpret_cast<DWORD>(&WindowOnButtonClicked);
    g_state.primaryVtable[11] = reinterpret_cast<DWORD>(&WindowDraw);
    g_state.uiVtable[0] = reinterpret_cast<DWORD>(&WindowOnKey);
    g_state.uiVtable[1] = reinterpret_cast<DWORD>(&WindowOnSetFocus);
    g_state.uiVtable[2] = reinterpret_cast<DWORD>(&WindowOnMouseButton);
    g_state.uiVtable[3] = reinterpret_cast<DWORD>(&WindowOnMouseMove);
    g_state.uiVtable[5] = reinterpret_cast<DWORD>(&WindowOnMouseEnter);
    g_state.uiVtable[9] = reinterpret_cast<DWORD>(&WindowSetShow);
    g_state.uiVtable[10] = reinterpret_cast<DWORD>(&WindowIsShown);
    g_state.uiVtable[15] = reinterpret_cast<DWORD>(&WindowOnIMEResult);
    g_state.uiVtable[17] = reinterpret_cast<DWORD>(&WindowGetRTTI);
    g_state.uiVtable[18] = reinterpret_cast<DWORD>(&WindowIsKindOf);
    g_state.refVtable[0] = reinterpret_cast<DWORD>(&WindowRefDestructor);
}

bool MatchesExpectedClient()
{
    const unsigned char constructorPrologue[] = {0xB8, 0x4A, 0x68, 0xAE, 0x00, 0xE8};
    const unsigned char createWndPrologue[] = {0xB8, 0xD8, 0x68, 0xAE, 0x00, 0xE8};
    const unsigned char setBackgrndPrologue[] = {0xB8, 0x14, 0x6D, 0xAE, 0x00, 0xE8};
    __try
    {
        return std::memcmp(reinterpret_cast<const void*>(kCWndConstructor), constructorPrologue,
                sizeof(constructorPrologue)) == 0
            && std::memcmp(reinterpret_cast<const void*>(kCWndCreateWnd), createWndPrologue,
                sizeof(createWndPrologue)) == 0
            && std::memcmp(reinterpret_cast<const void*>(kCWndSetBackgrnd), setBackgrndPrologue,
                sizeof(setBackgrndPrologue)) == 0
            && *reinterpret_cast<DWORD*>(0x00B404E4) == 0x009E067E
            && *reinterpret_cast<DWORD*>(0x00B40498) == 0x00A4F261;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void ReleaseCanvas(DWORD& canvas)
{
    if (!canvas) return;
    __try
    {
        auto vtable = *reinterpret_cast<DWORD**>(canvas);
        reinterpret_cast<ULONG(__stdcall*)(DWORD)>(vtable[2])(canvas);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    canvas = 0;
}
void ClearEntries()
{
    for (auto& entry : g_state.entries) { ReleaseCanvas(entry.icon); entry = Entry{}; }
    g_state.count = 0;
    g_state.hovered = -1;
    ClearToolTip();
}
void SetShow(bool show)
{
    if (!show && g_state.shown && (g_state.hovered >= 0 || g_state.hoveredTarget != HitTarget::None))
        SetHoverCursor(0);
    g_state.shown = show;
    if (show) g_state.escapeDown = (GetAsyncKeyState(VK_ESCAPE)&0x8000) != 0;
    else
    {
        g_state.dragging = false;
        g_state.hovered = -1;
        g_state.hoveredTarget = g_state.pressedTarget = HitTarget::None;
    }
    if (!g_state.window) return;
    SetLayerVisible(ReadField<DWORD>(0x18), show);
    SetLayerVisible(ReadField<DWORD>(0x1C), show);
    SetLayerVisible(ReadField<DWORD>(0x20), show);
    if (show) Invalidate();
    else { SetKeyboardFocus(false); ClearToolTip(); }
}
bool SendAction(int action, int slot = 0, int itemId = 0, int quantity = 0, int targetSlot = 0)
{
    if (g_state.pending && action != 4 && action != 5) return false;
    unsigned char data[24]{};
    auto put16 = [&](int at, int value) { data[at] = static_cast<BYTE>(value); data[at+1] = static_cast<BYTE>(value >> 8); };
    auto put32 = [&](int at, int value) { for (int i=0;i<4;i++) data[at+i] = static_cast<BYTE>(static_cast<unsigned int>(value) >> (8*i)); };
    put16(0, 0x1003); put16(2, 0x424D); data[4]=2; data[5]=static_cast<BYTE>(action);
    put32(6,g_state.session); put32(10,g_state.revision); put16(14,slot); put32(16,itemId); put16(20,quantity);
    put16(22,targetSlot);
    struct Packet { int loopback; unsigned char* data; unsigned long size; unsigned int offset; int shanda; } packet{0,data,sizeof(data),0,0};
    __try
    {
        void* socket = *reinterpret_cast<void**>(0x00BE7914);
        if (!socket) return false;
        reinterpret_cast<void(__fastcall*)(void*,void*,Packet*)>(0x0049637B)(socket,nullptr,&packet);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    g_state.pending = action != 4;
    g_state.sentAt = GetTickCount();
    return true;
}
void Close()
{
    if (g_state.window)
    {
        auto manager = *reinterpret_cast<unsigned char**>(kCWndManPtr);
        if (manager && *reinterpret_cast<void**>(manager + 0x90) == g_state.window + 4)
            reinterpret_cast<void(__thiscall*)(void*)>(0x009E3AC0)(manager);
    }
    if (g_state.session || g_state.pending) SendAction(4);
    SetShow(false);
    g_state.session = 0;
    g_state.pending = g_state.haveSnapshot = g_state.reopen = false;
}
bool IsMineral(int id)
{
    return (id >= 4010000 && id <= 4010007) || (id >= 4011000 && id <= 4011008)
        || (id >= 4020000 && id <= 4020008) || (id >= 4021000 && id <= 4021009);
}
int __stdcall HandleEtcDoubleClick(void* nativeItem, int slot)
{
    int id = 0;
    __try { id = reinterpret_cast<int(__thiscall*)(void*)>(0x0042873D)(static_cast<unsigned char*>(nativeItem)+0x0C); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (id == kBagId)
    {
        if (IsShown() && g_state.bagSlot == slot) Close();
        else if (!g_state.pending) { g_state.reopen = true; SendAction(0,slot,id); }
        return 1;
    }
    return 0;
}
DWORD g_etcContinue = 0x004EFF39;
DWORD g_doubleClickDone = 0x004EFD4C;
__declspec(naked) void EtcDoubleClickHook()
{
    __asm
    {
        pushfd
        pushad
        mov eax, dword ptr[ebp-18h]
        push dword ptr[eax+1Ch]
        push esi
        call HandleEtcDoubleClick
        test eax,eax
        jnz handled
        popad
        popfd
        mov ecx, dword ptr[ebp-10h]
        push eax
        push 500
        jmp dword ptr[g_etcContinue]
    handled:
        popad
        popfd
        jmp dword ptr[g_doubleClickDone]
    }
}
bool Contains(int x,int y,int left,int top,int width,int height)
{ return x>=left && x<left+width && y>=top && y<top+height; }
int CellAt(int x,int y)
{
    if (!Contains(x,y,kGridLeft,kGridTop,kCellPitch*4,kCellPitch*5)) return -1;
    return (y-kGridTop)/kCellPitch*4+(x-kGridLeft)/kCellPitch;
}

unsigned char* InventoryWindow(void* ui)
{
    if (!ui) return nullptr;
    auto isItemWindow = [](void* handler) -> bool
    {
        __try
        {
            auto vtable = *reinterpret_cast<DWORD**>(handler);
            return reinterpret_cast<int(__thiscall*)(void*,const void*)>(vtable[18])(
                handler,reinterpret_cast<void*>(0x00BF0EB0)) != 0;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    };
    // CWndMan normally supplies IUIMsgHandler (window + 4). Accept the primary
    // pointer as well because both forms are used by native inventory helpers.
    auto window = static_cast<unsigned char*>(ui)-4;
    if (!isItemWindow(ui))
    {
        window = static_cast<unsigned char*>(ui);
        if (!isItemWindow(window+4)) return nullptr;
    }
    __try { return *reinterpret_cast<int*>(window+0x5E4)==4 ? window : nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

int NativeEtcItemId(int slot)
{
    DWORD character[2]{}, item[2]{};
    int id=0;
    auto context=*reinterpret_cast<void**>(0x00BE7918);
    if (!context || slot<1 || slot>127) return 0;
    reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*)>(0x00425D0B)(context,character);
    if (character[1])
        reinterpret_cast<DWORD*(__thiscall*)(DWORD,DWORD*,int,int)>(0x004282F7)(character[1],item,4,slot);
    if (item[1])
        id=reinterpret_cast<int(__thiscall*)(DWORD)>(0x0042873D)(item[1]+0x0C);
    reinterpret_cast<void(__thiscall*)(DWORD*)>(0x004288C1)(item);
    reinterpret_cast<void(__thiscall*)(DWORD*)>(0x004289B7)(character);
    return id;
}

int __fastcall InventoryItemDropped(void* draggable,void*,void* source,void* target,int x,int y)
{
    if (!g_state.window || target!=g_state.window+4)
        return reinterpret_cast<int(__thiscall*)(void*,void*,void*,int,int)>(0x004EF140)(
            draggable,source,target,x,y);
    __try
    {
        // CWndMan supplies coordinates relative to the target UI, as for CUIItem.
        int cell=CellAt(x,y);
        auto fields=static_cast<DWORD*>(draggable);
        if (!IsShown() || g_state.pending || cell<0 || fields[6]!=4 || !InventoryWindow(source)) return 0;
        int id=NativeEtcItemId(fields[7]);
        return IsMineral(id) && SendAction(1,fields[7],id,0,cell+1) ? 1 : 0;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __fastcall BagItemDropped(void* draggable,void*,void* source,void* target,int x,int y)
{
    __try
    {
        auto fields=static_cast<DWORD*>(draggable);
        if (!IsShown() || g_state.pending || source!=g_state.window+4
                || static_cast<int>(fields[8])!=g_state.session || static_cast<int>(fields[9])!=g_state.revision) return 0;
        int cell=static_cast<int>(fields[6]),id=static_cast<int>(fields[7]);
        if (cell<0 || cell>=20 || g_state.entries[cell].id!=id) return 0;
        if (target==g_state.window+4)
        {
            int destination=CellAt(x,y);
            return destination>=0 && SendAction(6,cell+1,id,0,destination+1) ? 1 : 0;
        }
        auto inventory=InventoryWindow(target);
        if (!inventory) return 0;
        int destination=reinterpret_cast<int(__thiscall*)(void*,int,int)>(0x0081DB7E)(inventory,x,y);
        return destination>0 && SendAction(2,cell+1,id,0,destination) ? 1 : 0;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

DWORD g_bagDragVtable[3]={0x004F3ED4,reinterpret_cast<DWORD>(&BagItemDropped),0x004F3ED1};

void BeginBagDrag(int cell,int x,int y)
{
    DWORD layer=0;
    DWORD parentLayer=0;
    DWORD* draggable=nullptr;
    __try
    {
        auto manager=*reinterpret_cast<unsigned char**>(kCWndManPtr);
        DWORD graphics=*reinterpret_cast<DWORD*>(0x00BF14EC);
        if (!manager || !graphics || *reinterpret_cast<DWORD*>(manager+0x98)) return;
        auto& entry=g_state.entries[cell];
        if (!entry.icon) entry.icon=LoadItemIcon(entry.id);
        if (!entry.icon) return;
        CanvasVariant missing{},zero{},result{},origin{};
        missing.data[0]=10;missing.data[2]=0x80020004;
        zero.data[0]=3;
        reinterpret_cast<DWORD*(__thiscall*)(DWORD,DWORD*,int,int,int,int,int,CanvasVariant*,CanvasVariant*)>(0x00426C7E)(
            graphics,&layer,0,0,0,0,0,&zero,&missing);
        if (!layer) return;
        reinterpret_cast<CanvasVariant*(__thiscall*)(DWORD,CanvasVariant*,DWORD,CanvasVariant*,CanvasVariant*,CanvasVariant*,CanvasVariant*,CanvasVariant*)>(0x00426BAB)(
            layer,&result,entry.icon,&missing,&missing,&missing,&missing,&missing);
        reinterpret_cast<HRESULT(__cdecl*)(CanvasVariant*)>(0x0040291D)(&result);
        auto vtable=*reinterpret_cast<DWORD**>(layer);
        reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*)>(0x00426604)(g_state.window,&parentLayer);
        if (!parentLayer) { ReleaseCanvas(layer);return; }
        reinterpret_cast<CanvasVariant*(__thiscall*)(CanvasVariant*,DWORD,int)>(0x00410FDF)(
            &origin,parentLayer,1);
        ReleaseCanvas(parentLayer);
        // Match CUIItem: put_origin takes the parent; RelMove takes optional times.
        HRESULT hr=reinterpret_cast<HRESULT(__stdcall*)(DWORD,CanvasVariant)>(vtable[0x64/4])(
            layer,origin);
        reinterpret_cast<HRESULT(__cdecl*)(CanvasVariant*)>(0x0040291D)(&origin);
        if (FAILED(hr)) { ReleaseCanvas(layer);return; }
        int iconY=reinterpret_cast<int(__thiscall*)(DWORD)>(0x0040F0C2)(entry.icon);
        int iconX=reinterpret_cast<int(__thiscall*)(DWORD)>(0x0040F09B)(entry.icon);
        hr=reinterpret_cast<HRESULT(__stdcall*)(DWORD,int,int,CanvasVariant,CanvasVariant)>(vtable[0x90/4])(
            layer,x-iconX-16,y-iconY+16,missing,missing);
        if (FAILED(hr)) { ReleaseCanvas(layer);return; }
        reinterpret_cast<HRESULT(__stdcall*)(DWORD,DWORD)>(vtable[0xE0/4])(layer,0x80FFFFFF);
        draggable=reinterpret_cast<DWORD*(__thiscall*)(void*,unsigned int)>(0x00403065)(
            reinterpret_cast<void*>(0x00BF0B00),0x28);
        if (!draggable) { ReleaseCanvas(layer);return; }
        std::memset(draggable,0,0x28);
        reinterpret_cast<void*(__thiscall*)(void*,DWORD)>(0x006FFDA3)(draggable,layer);
        draggable[0]=reinterpret_cast<DWORD>(g_bagDragVtable);
        draggable[6]=cell;draggable[7]=entry.id;draggable[8]=g_state.session;draggable[9]=g_state.revision;
        reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x009E353D)(manager,g_state.window+4,draggable);
        draggable=nullptr;
        PlayUiSound(0x737); // Same pickup sound as CUIItem::OnMouseButton.
        ClearToolTip();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        // A successful BeginDragDrop transfers the reference to CWndMan.
        auto manager=*reinterpret_cast<unsigned char**>(kCWndManPtr);
        if (draggable && (!manager || *reinterpret_cast<DWORD**>(manager+0x98)!=draggable))
            reinterpret_cast<void(__thiscall*)(void*,int)>(0x004F3ED4)(draggable,1);
    }
    ReleaseCanvas(layer);
}
HitTarget TargetAt(int x,int y)
{
    if(Contains(x,y,kCloseX,kCloseY,kCloseSize,kCloseSize)) return HitTarget::Exit;
    return HitTarget::None;
}
void DrawNumber(DWORD dest,int number,int x,int y)
{
    char text[16]{}; _snprintf_s(text,sizeof(text),_TRUNCATE,"%d",number);
    for(const char* p=text;*p;p++)
    { DrawCanvas(dest,g_state.numbers[*p-'0'],x,y);x+=*p=='1'?3:6; }
}
void DrawButton(DWORD dest,const ButtonCanvases& button,HitTarget target,int x,int y)
{
    DWORD canvas=g_state.pressedTarget==target?button.pressed:g_state.hoveredTarget==target?button.mouseOver:button.normal;
    DrawCanvas(dest,canvas,x,y);
}
void __fastcall WindowDraw(void* window,void*,const RECT* rect)
{
    DWORD dest=0;
    __try { g_drawWnd(window,nullptr,rect); g_getCanvas(window,nullptr,&dest); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if(!dest)return;
    for(int i=0;i<20;i++)
    {
        int x=kGridLeft+i%4*kCellPitch, y=kGridTop+i/4*kCellPitch;
        auto& entry=g_state.entries[i];
        if(!entry.id)continue;
        if(!entry.icon)entry.icon=LoadItemIcon(entry.id);
        DrawCanvas(dest,entry.icon,x,y);
        DrawNumber(dest,entry.quantity,x+1,y+21);
    }
    DrawButton(dest,g_state.exit,HitTarget::Exit,kCloseX,kCloseY);
    ReleaseCanvas(dest);
}
void __fastcall WindowOnKey(void*,void*,unsigned int key,unsigned int)
{
    if(key==VK_ESCAPE)Close();
}
int __fastcall WindowOnSetFocus(void*,void*,int focused)
{ return 1; }
void __fastcall WindowSetShow(void*,void*,int show){SetShow(show!=0);}
int __fastcall WindowIsShown(void*,void*){return IsShown()?1:0;}
void __fastcall WindowOnMouseButton(void*,void*,unsigned int message,unsigned int,int x,int y)
{
    auto target=TargetAt(x,y);
    if(message==WM_LBUTTONDOWN)
    {
        g_state.pressedTarget=target;
        SetKeyboardFocus(true);
        int cell=CellAt(x,y);
        if(target==HitTarget::None && cell<0 && y>=0 && y<kTitleBarHeight)
        {
            g_state.dragging=true;
            g_state.dragAnchorX=x;
            g_state.dragAnchorY=y;
        }
        else if(cell>=0 && g_state.entries[cell].id && !g_state.pending)BeginBagDrag(cell,x,y);
    }
    else if(message==WM_LBUTTONUP)
    {
        g_state.dragging=false;
        if(target==g_state.pressedTarget)
        {
            if(target==HitTarget::Exit){PlayUiSound(0x5A3);Close();}
        }
        g_state.pressedTarget=HitTarget::None;
    }
    Invalidate();
}
int __fastcall WindowOnMouseMove(void*,void*,int x,int y)
{
    if(g_state.dragging)
    {
        if((GetAsyncKeyState(VK_LBUTTON)&0x8000)==0)
        {
            g_state.dragging=false;
        }
        else
        {
            MoveWindow(g_state.windowX+x-g_state.dragAnchorX,
                g_state.windowY+y-g_state.dragAnchorY);
            return 0;
        }
    }
    int cell=CellAt(x,y);
    auto target=TargetAt(x,y);
    SetHoverCursor(target==HitTarget::Exit ? 4 : cell>=0 ? 5 : 0);
    if(cell!=g_state.hovered || target!=g_state.hoveredTarget)
    {
        if(target==HitTarget::Exit && g_state.hoveredTarget!=HitTarget::Exit)
            PlayUiSound(0x5A4);
        g_state.hovered=cell;g_state.hoveredTarget=target;ClearToolTip();
        if(cell>=0 && g_state.entries[cell].id)
            ShowToolTip(x,y+20,g_state.entries[cell]);
        Invalidate();
    }
    return 0;
}
void __fastcall WindowOnMouseEnter(void*,void*,int entered)
{if(!entered){SetHoverCursor(0);g_state.hovered=-1;g_state.hoveredTarget=HitTarget::None;ClearToolTip();Invalidate();}}
bool CreateBagWindow()
{
    if(g_state.windowReady)return true;
    if(g_state.windowFailed)return false;
    g_state.windowFailed=true;
    g_state.window=new unsigned char[kNativeWindowSize]{};
    __try
    {
        g_constructWnd(g_state.window,nullptr);
        *reinterpret_cast<DWORD*>(g_state.window)=reinterpret_cast<DWORD>(g_state.primaryVtable);
        *reinterpret_cast<DWORD*>(g_state.window+4)=reinterpret_cast<DWORD>(g_state.uiVtable);
        *reinterpret_cast<DWORD*>(g_state.window+8)=reinterpret_cast<DWORD>(g_state.refVtable);
        DWORD path=0;
        g_constructZXStringW(&path,nullptr,L"UI/UIWindow.img/MineralBag/backgrnd");
        g_setBackgrnd(g_state.window,nullptr,path,0,0);
        if(!ReadField<DWORD>(0x68))return false;
        g_state.windowX=(std::max)(0,Client::m_nGameWidth/2);
        g_state.windowY=(std::max)(0,(Client::m_nGameHeight-kHeight)/2);
        g_createWnd(g_state.window,nullptr,g_state.windowX,g_state.windowY,
            kWidth,kHeight,10,1,nullptr,0);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x008E49B5)(g_state.tooltip,nullptr);
        g_state.tooltipCreated=true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    g_state.exit=LoadButtonCanvases(L"UI/Basic.img/BtClose");
    for(int i=0;i<10;i++)
    {
        wchar_t path[128];
        swprintf_s(path,L"UI/Basic.img/ItemNo/%d",i);g_state.numbers[i]=LoadCanvas(path);
    }
    g_state.windowReady=true;
    g_state.windowFailed=false;
    return true;
}
}

bool MineralBagWnd::Install()
{
    const BYTE branch[]={0x8B,0x4D,0xF0,0x50,0x68,0xF4,0x01,0x00,0x00};
    if(!MatchesExpectedClient() || std::memcmp(reinterpret_cast<void*>(0x004EFF30),branch,sizeof(branch))
        || *reinterpret_cast<DWORD*>(0x00AF34DC)!=0x004EF140)return false;
    InitializeVtables();
    Memory::CodeCave(EtcDoubleClickHook,0x004EFF30,sizeof(branch));
    Memory::WriteInt(0x00AF34DC,reinterpret_cast<DWORD>(&InventoryItemDropped));
    g_state.installed=true;
    return true;
}
bool MineralBagWnd::HandlePacket(const unsigned char* data,unsigned short length)
{
    if(!data || length<6 || data[4]!=0x08 || data[5]!=0x10)return false;
    if(!g_state.installed || length<21)return true;
    const unsigned char* p=data+6;const unsigned char* end=data+length;
    auto u16=[&](){int value=p[0]|p[1]<<8;p+=2;return value;};
    auto u32=[&](){unsigned int value=p[0]|p[1]<<8|p[2]<<16|static_cast<unsigned int>(p[3])<<24;p+=4;return static_cast<int>(value);};
    if(u16()!=0x424D || *p++!=2)return true;
    int result=*p++,session=u32(),revision=u32(),slot=u16(),count=*p++;
    if(result>5 || count>20 || session<0 || revision<0 || slot>127
        || (session!=0 && (slot==0 || revision==0)) || (session==0 && count!=0))return true;
    std::array<Entry,20> entries{};
    for(int i=0;i<count;i++)
    {
        if(end-p<9)return true;
        int index=*p++-1;
        if(index<0 || index>=20 || entries[index].id)return true;
        auto& entry=entries[index];
        entry.id=u32();entry.quantity=u16();int size=u16();
        if(!IsMineral(entry.id) || entry.quantity<1 || entry.quantity>32767 || size>256 || end-p<size)return true;
        entry.name.assign(reinterpret_cast<const char*>(p),size);p+=size;
    }
    if(p!=end)return true;
    if(!g_state.pending && !IsShown())return true;
    if(!g_state.reopen && g_state.session!=0 && session!=0 && session!=g_state.session)return true;
    ClearEntries();g_state.entries=std::move(entries);g_state.count=count;
    g_state.session=session;g_state.revision=revision;g_state.bagSlot=slot;
    g_state.pending=false;g_state.haveSnapshot=true;
    return true;
}
void MineralBagWnd::OnFieldUpdate()
{
    if(!g_state.installed)return;
    bool escapeDown=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
    if(IsShown() && escapeDown && !g_state.escapeDown)Close();
    g_state.escapeDown=escapeDown;
    if(g_state.haveSnapshot)
    {
        g_state.haveSnapshot=false;
        if(!g_state.session){SetShow(false);g_state.reopen=false;return;}
        if(CreateBagWindow()){SetShow(true);SetKeyboardFocus(true);}
        else Close();
        g_state.reopen=false;
    }
    if(g_state.pending && GetTickCount()-g_state.sentAt>5000)
    {
        if(g_state.session)SendAction(5);
        else {g_state.pending=false;g_state.reopen=false;}
    }
}
void MineralBagWnd::OnFieldDispose()
{
    Close();ClearEntries();g_state.revision=0;g_state.bagSlot=0;g_state.dragging=false;
}
