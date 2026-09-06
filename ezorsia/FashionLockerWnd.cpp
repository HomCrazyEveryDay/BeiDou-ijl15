#include "stdafx.h"
#include "FashionLockerWnd.h"
#include "CrashReporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr WORD kActionOpcode = 0x1001;
constexpr WORD kOpenOpcode = 0x1005;
constexpr WORD kUpdateOpcode = 0x1006;
constexpr WORD kProtocolMagic = 0x4C46;
constexpr BYTE kProtocolVersion = 2;
constexpr DWORD kClientSocketPtr = 0x00BE7914;
constexpr DWORD kCWndConstructor = 0x009DE383;
constexpr DWORD kCWndCreateWnd = 0x009DE4D2;
constexpr DWORD kCWndSetBackgrnd = 0x009E0AB2;
constexpr DWORD kCWndInvalidateRect = 0x009E04C9;
constexpr DWORD kCWndDraw = 0x009E0502;
constexpr DWORD kCWndGetCanvas = 0x00425C4C;
constexpr DWORD kCWndManPtr = 0x00BEC20C;
constexpr DWORD kCWndManSetFocus = 0x009E3264;
constexpr size_t kCWndManFocusOffset = 0x88;
constexpr DWORD kZXStringWConstructor = 0x00403382;
constexpr DWORD kItemInfo = 0x00BE78D8;
constexpr DWORD kGetItemIconCanvas = 0x005D3BD8;
constexpr DWORD kSendPacket = 0x0049637B;
constexpr DWORD kToolTipCreate = 0x008E49B5;
constexpr DWORD kToolTipClear = 0x008E6E23;
constexpr DWORD kToolTipSetString = 0x008E6E7D;
constexpr size_t kNativeWindowSize = 0x100;
constexpr int kWindowWidth = 491;
constexpr int kWindowHeight = 414;
constexpr int kStoredColumns = 8;
constexpr int kCarriedColumns = 4;
constexpr int kGridRows = 8;
constexpr int kStoredPageSize = kStoredColumns * kGridRows;
constexpr int kCarriedPageSize = kCarriedColumns * kGridRows;
constexpr int kStoredGridX = 10;
constexpr int kCarriedGridX = 326;
constexpr int kGridTop = 93;
constexpr int kCellWidth = 36;
constexpr int kCellHeight = 36;
constexpr int kPresetCount = 6;
constexpr int kPresetX = 324;
constexpr int kPresetY = 43;
constexpr int kPresetWidth = 22;
constexpr int kPresetHeight = 20;
constexpr int kPresetGap = 25;
constexpr int kExitX = 238;
constexpr int kExitY = 17;
constexpr int kGetX = 238;
constexpr int kGetY = 36;
constexpr int kSortX = 238;
constexpr int kSortY = 55;
constexpr int kPutX = 399;
constexpr int kPutY = 17;
constexpr int kDressX = 195;
constexpr int kDressY = 43;
constexpr int kSearchX = 16;
constexpr int kSearchY = 43;
constexpr int kSortingX = 139;
constexpr int kSortingY = 42;

enum class HitTarget
{
    None,
    Exit,
    Get,
    Sort,
    Put,
    Dress,
    Search,
    Sorting,
    StoredPrevious,
    StoredNext,
    CarriedPrevious,
    CarriedNext,
};

enum class Action : BYTE
{
    Open = 0,
    TakeOut = 1,
    PutIn = 2,
    Sort = 3,
    EquipDirect = 4,
    Close = 5,
    SavePreset = 6,
    ApplyPreset = 7,
    DeletePreset = 8,
};

struct PacketItem
{
    int key = 0;
    int itemId = 0;
    std::string name;
    DWORD icon = 0;
};

struct FashionPresetItem
{
    short position = 0;
    int itemId = 0;
};

struct FashionPreset
{
    bool defined = false;
    std::string name;
    std::vector<FashionPresetItem> items;
};

struct ButtonCanvases
{
    DWORD normal = 0;
    DWORD pressed = 0;
    DWORD disabled = 0;
    DWORD mouseOver = 0;
};

struct COutPacket
{
    int loopback;
    union
    {
        unsigned char* data;
        void* unknown;
        unsigned short* header;
    };
    unsigned long size;
    unsigned int offset;
    int encryptedByShanda;
};

struct CanvasVariant
{
    DWORD data[4];
};

struct PacketReader
{
    const unsigned char* cursor;
    const unsigned char* end;

    bool ReadByte(BYTE& value)
    {
        if (cursor >= end)
        {
            return false;
        }
        value = *cursor++;
        return true;
    }

    bool ReadShort(WORD& value)
    {
        if (end - cursor < 2)
        {
            return false;
        }
        value = static_cast<WORD>(cursor[0] | (cursor[1] << 8));
        cursor += 2;
        return true;
    }

    bool ReadInt(int& value)
    {
        if (end - cursor < 4)
        {
            return false;
        }
        value = static_cast<int>(
            static_cast<unsigned int>(cursor[0])
            | (static_cast<unsigned int>(cursor[1]) << 8)
            | (static_cast<unsigned int>(cursor[2]) << 16)
            | (static_cast<unsigned int>(cursor[3]) << 24));
        cursor += 4;
        return true;
    }

    bool ReadString(std::string& value)
    {
        WORD length = 0;
        if (!ReadShort(length) || end - cursor < length)
        {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(cursor), length);
        cursor += length;
        return true;
    }
};

struct State
{
    unsigned char* window = nullptr;
    DWORD primaryVtable[14] = {};
    DWORD uiVtable[19] = {};
    DWORD refVtable[1] = {};
    std::vector<PacketItem> stored;
    std::vector<PacketItem> carried;
    std::array<FashionPreset, kPresetCount> presets;
    std::string search;
    int sessionId = 0;
    int revision = 0;
    int capacity = 0;
    int storedPage = 0;
    int carriedPage = 0;
    int selectedStored = -1;
    int selectedCarried = -1;
    int hoveredStored = -1;
    int hoveredCarried = -1;
    int selectedPreset = -1;
    int hoveredPreset = -1;
    HitTarget hoveredTarget = HitTarget::None;
    HitTarget pressedTarget = HitTarget::None;
    int sortMode = 0;
    BYTE lastResult = 0;
    bool searchActive = false;
    bool shortcutDown = false;
    bool installed = false;
    bool shown = false;
    bool logEnabled = false;
    bool tooltipCreated = false;
    unsigned char tooltip[1304] = {};
    DWORD selectCanvas = 0;
    DWORD backgrnd2Canvas = 0;
    DWORD backgrnd3Canvas = 0;
    ButtonCanvases getButton;
    ButtonCanvases putButton;
    ButtonCanvases sortButton;
    ButtonCanvases exitButton;
    ButtonCanvases dressButton;
    ButtonCanvases previousButton;
    ButtonCanvases nextButton;
    DWORD searchCanvas = 0;
    std::array<DWORD, 3> sortingCanvases = {};
    std::array<DWORD, 3> sortingPressedCanvases = {};
    DWORD lockerTitleCanvas = 0;
    DWORD carriedTitleCanvas = 0;
    std::array<DWORD, kPresetCount> presetEmptyCanvases = {};
    std::array<DWORD, kPresetCount> presetFilledCanvases = {};
    std::array<DWORD, kPresetCount> presetSelectedCanvases = {};
};

State g_state;

using CreateWnd_t = void(__fastcall*)(void*, void*, int, int, int, int, int, int, void*, int);
using CWndConstructor_t = void*(__fastcall*)(void*, void*);
using SetBackgrnd_t = void(__fastcall*)(void*, void*, DWORD, int, int);
using InvalidateRect_t = void(__fastcall*)(void*, void*, const RECT*);
using DrawWnd_t = void(__fastcall*)(void*, void*, const RECT*);
using GetCanvas_t = DWORD*(__fastcall*)(void*, void*, DWORD*);
using SetFocus_t = void(__fastcall*)(void*, void*, void*);
using ZXStringWConstructor_t = DWORD*(__fastcall*)(DWORD*, void*, const wchar_t*);
using GetItemIconCanvas_t = DWORD*(__fastcall*)(DWORD, void*, DWORD*, DWORD, int, DWORD);
using SendPacket_t = void(__fastcall*)(void*, void*, COutPacket*);
using ToolTipCreate_t = void(__fastcall*)(void*, void*);
using ToolTipClear_t = void(__fastcall*)(void*, void*);
using ToolTipSetString_t = void(__fastcall*)(void*, void*, int, int, const char*);

CreateWnd_t g_createWnd = reinterpret_cast<CreateWnd_t>(kCWndCreateWnd);
CWndConstructor_t g_constructWnd = reinterpret_cast<CWndConstructor_t>(kCWndConstructor);
SetBackgrnd_t g_setBackgrnd = reinterpret_cast<SetBackgrnd_t>(kCWndSetBackgrnd);
InvalidateRect_t g_invalidateRect = reinterpret_cast<InvalidateRect_t>(kCWndInvalidateRect);
DrawWnd_t g_drawWnd = reinterpret_cast<DrawWnd_t>(kCWndDraw);
GetCanvas_t g_getCanvas = reinterpret_cast<GetCanvas_t>(kCWndGetCanvas);
SetFocus_t g_setFocus = reinterpret_cast<SetFocus_t>(kCWndManSetFocus);
ZXStringWConstructor_t g_constructZXStringW = reinterpret_cast<ZXStringWConstructor_t>(kZXStringWConstructor);
GetItemIconCanvas_t g_getItemIconCanvas = reinterpret_cast<GetItemIconCanvas_t>(kGetItemIconCanvas);
SendPacket_t g_sendPacket = reinterpret_cast<SendPacket_t>(kSendPacket);
ToolTipCreate_t g_createToolTip = reinterpret_cast<ToolTipCreate_t>(kToolTipCreate);
ToolTipClear_t g_clearToolTip = reinterpret_cast<ToolTipClear_t>(kToolTipClear);
ToolTipSetString_t g_setToolTipString = reinterpret_cast<ToolTipSetString_t>(kToolTipSetString);

template <typename T>
T ReadField(size_t offset)
{
    return *reinterpret_cast<T*>(g_state.window + offset);
}

bool IsShown()
{
    return g_state.window != nullptr && g_state.shown;
}

void Invalidate();

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
        CrashReporter::RecordEvent("fashionLocker.focus", "code=0x%08lX", GetExceptionCode());
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

void SetShow(bool show)
{
    if (!g_state.window)
    {
        return;
    }
    g_state.shown = show;
    g_state.hoveredTarget = HitTarget::None;
    g_state.pressedTarget = HitTarget::None;
    SetLayerVisible(ReadField<DWORD>(0x18), show);
    SetLayerVisible(ReadField<DWORD>(0x1C), show);
    SetLayerVisible(ReadField<DWORD>(0x20), show);
    if (show)
    {
        Invalidate();
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
        CrashReporter::RecordEvent("fashionLocker.invalidate", "code=0x%08lX", GetExceptionCode());
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
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void ShowToolTip(int x, int y, const std::string& text)
{
    if (!g_state.tooltipCreated || !g_state.window || text.empty())
    {
        return;
    }
    const int absoluteX = ReadField<int>(0x40) + x;
    const int absoluteY = ReadField<int>(0x44) + y;
    __try
    {
        g_setToolTipString(g_state.tooltip, nullptr, absoluteX, absoluteY, text.c_str());
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void AppendShort(std::vector<unsigned char>& payload, WORD value)
{
    payload.push_back(static_cast<unsigned char>(value & 0xFF));
    payload.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void AppendInt(std::vector<unsigned char>& payload, int value)
{
    const unsigned int raw = static_cast<unsigned int>(value);
    payload.push_back(static_cast<unsigned char>(raw & 0xFF));
    payload.push_back(static_cast<unsigned char>((raw >> 8) & 0xFF));
    payload.push_back(static_cast<unsigned char>((raw >> 16) & 0xFF));
    payload.push_back(static_cast<unsigned char>((raw >> 24) & 0xFF));
}

bool TrySendPacket(DWORD socket, COutPacket* packet, Action action)
{
    __try
    {
        g_sendPacket(reinterpret_cast<void*>(socket), nullptr, packet);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("fashionLocker.send", "action=%u code=0x%08lX",
            static_cast<unsigned int>(action), GetExceptionCode());
        return false;
    }
}

DWORD ReadClientSocket()
{
    __try
    {
        return *reinterpret_cast<DWORD*>(kClientSocketPtr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool SendAction(Action action, const std::vector<unsigned char>& arguments = {})
{
    const DWORD socket = ReadClientSocket();
    if (!socket)
    {
        return false;
    }

    std::vector<unsigned char> payload;
    payload.reserve(16 + arguments.size());
    AppendShort(payload, kActionOpcode);
    AppendShort(payload, kProtocolMagic);
    payload.push_back(kProtocolVersion);
    payload.push_back(static_cast<BYTE>(action));
    if (action != Action::Open)
    {
        AppendInt(payload, g_state.sessionId);
        if (action != Action::Close)
        {
            AppendInt(payload, g_state.revision);
        }
    }
    payload.insert(payload.end(), arguments.begin(), arguments.end());

    COutPacket packet{};
    packet.data = payload.data();
    packet.size = static_cast<unsigned long>(payload.size());
    if (!TrySendPacket(socket, &packet, action))
    {
        return false;
    }
    if (g_state.logEnabled)
    {
        CrashReporter::RecordRecentEvent("fashionLocker.send", "action=%u session=%d revision=%d",
            static_cast<unsigned int>(action), g_state.sessionId, g_state.revision);
    }
    return true;
}

bool SendPresetAction(Action action, int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= kPresetCount)
    {
        return false;
    }
    return SendAction(action, {static_cast<unsigned char>(presetIndex + 1)});
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
        return *reinterpret_cast<DWORD*>(holder + 0x68);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("fashionLocker.canvas", "code=0x%08lX", GetExceptionCode());
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
        g_getItemIconCanvas(itemInfo, nullptr, &icon, static_cast<DWORD>(itemId), 0, 0);
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

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return value;
}

std::vector<int> FilteredIndexes(const std::vector<PacketItem>& items)
{
    std::vector<int> indexes;
    indexes.reserve(items.size());
    const std::string query = LowerAscii(g_state.search);
    for (size_t index = 0; index < items.size(); ++index)
    {
        const PacketItem& item = items[index];
        if (query.empty()
            || LowerAscii(item.name).find(query) != std::string::npos
            || std::to_string(item.itemId).find(query) != std::string::npos)
        {
            indexes.push_back(static_cast<int>(index));
        }
    }
    std::stable_sort(indexes.begin(), indexes.end(), [&](int leftIndex, int rightIndex) {
        const PacketItem& left = items[leftIndex];
        const PacketItem& right = items[rightIndex];
        if (g_state.sortMode == 1 || g_state.sortMode == 2)
        {
            const std::string leftName = LowerAscii(left.name);
            const std::string rightName = LowerAscii(right.name);
            if (leftName != rightName)
            {
                return g_state.sortMode == 1 ? leftName < rightName : leftName > rightName;
            }
        }
        return left.itemId != right.itemId ? left.itemId < right.itemId : left.key < right.key;
    });
    return indexes;
}

int ItemAtCell(const std::vector<PacketItem>& items, int page, int pageSize, int cell)
{
    const std::vector<int> indexes = FilteredIndexes(items);
    const int filteredIndex = page * pageSize + cell;
    return filteredIndex >= 0 && filteredIndex < static_cast<int>(indexes.size())
        ? indexes[filteredIndex]
        : -1;
}

int PresetAtPoint(int x, int y)
{
    if (x < kPresetX || y < kPresetY || y >= kPresetY + kPresetHeight)
    {
        return -1;
    }
    const int relativeX = x - kPresetX;
    const int index = relativeX / kPresetGap;
    if (index < 0 || index >= kPresetCount || relativeX % kPresetGap >= kPresetWidth)
    {
        return -1;
    }
    return index;
}

int PageCount(const std::vector<PacketItem>& items, int pageSize)
{
    const int count = static_cast<int>(FilteredIndexes(items).size());
    return (std::max)(1, (count + pageSize - 1) / pageSize);
}

void ClampPages()
{
    g_state.storedPage = (std::max)(0,
        (std::min)(g_state.storedPage, PageCount(g_state.stored, kStoredPageSize) - 1));
    g_state.carriedPage = (std::max)(0,
        (std::min)(g_state.carriedPage, PageCount(g_state.carried, kCarriedPageSize) - 1));
}

short ResolveCashEquipTarget(int itemId)
{
    const int category = itemId / 10000;
    switch (category)
    {
    case 100: return -101;
    case 101: return -102;
    case 102: return -103;
    case 103: return -104;
    case 104:
    case 105: return -105;
    case 106: return -106;
    case 107: return -107;
    case 108: return -108;
    case 109: return -110;
    case 110: return -109;
    case 111: return -112;
    case 112: return -117;
    case 113: return -150;
    case 114: return -149;
    case 190: return -118;
    case 191: return -119;
    default:
        if (category >= 120 && category <= 179)
        {
            return -111;
        }
        return 0;
    }
}

bool ContainsPoint(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

HitTarget TargetAtPoint(int x, int y)
{
    if (ContainsPoint(x, y, kExitX, kExitY, 64, 16)) return HitTarget::Exit;
    if (ContainsPoint(x, y, kGetX, kGetY, 64, 16)) return HitTarget::Get;
    if (ContainsPoint(x, y, kSortX, kSortY, 64, 16)) return HitTarget::Sort;
    if (ContainsPoint(x, y, kPutX, kPutY, 75, 16)) return HitTarget::Put;
    if (ContainsPoint(x, y, kDressX, kDressY, 27, 26)) return HitTarget::Dress;
    if (ContainsPoint(x, y, kSearchX, kSearchY, 118, 20)) return HitTarget::Search;
    if (ContainsPoint(x, y, kSortingX, kSortingY, 50, 20)) return HitTarget::Sorting;
    if (ContainsPoint(x, y, 155, 377, 24, 16)) return HitTarget::StoredPrevious;
    if (ContainsPoint(x, y, 273, 377, 24, 16)) return HitTarget::StoredNext;
    if (ContainsPoint(x, y, 325, 377, 24, 16)) return HitTarget::CarriedPrevious;
    if (ContainsPoint(x, y, 448, 377, 24, 16)) return HitTarget::CarriedNext;
    return HitTarget::None;
}

bool IsTargetEnabled(HitTarget target)
{
    switch (target)
    {
    case HitTarget::Get:
        return g_state.selectedStored >= 0
            && g_state.selectedStored < static_cast<int>(g_state.stored.size());
    case HitTarget::Put:
        return g_state.selectedCarried >= 0
            && g_state.selectedCarried < static_cast<int>(g_state.carried.size());
    case HitTarget::Dress:
        return g_state.selectedStored >= 0
            && g_state.selectedStored < static_cast<int>(g_state.stored.size())
            && ResolveCashEquipTarget(g_state.stored[g_state.selectedStored].itemId) != 0;
    case HitTarget::StoredPrevious:
        return g_state.storedPage > 0;
    case HitTarget::StoredNext:
        return g_state.storedPage + 1 < PageCount(g_state.stored, kStoredPageSize);
    case HitTarget::CarriedPrevious:
        return g_state.carriedPage > 0;
    case HitTarget::CarriedNext:
        return g_state.carriedPage + 1 < PageCount(g_state.carried, kCarriedPageSize);
    case HitTarget::None:
        return false;
    default:
        return true;
    }
}

void DrawButton(DWORD destination, const ButtonCanvases& canvases,
    HitTarget target, int x, int y)
{
    DWORD canvas = canvases.normal;
    if (!IsTargetEnabled(target) && canvases.disabled)
    {
        canvas = canvases.disabled;
    }
    else if (g_state.pressedTarget == target && g_state.hoveredTarget == target
        && canvases.pressed)
    {
        canvas = canvases.pressed;
    }
    else if (g_state.hoveredTarget == target && canvases.mouseOver)
    {
        canvas = canvases.mouseOver;
    }
    DrawCanvas(destination, canvas, x, y);
}

void SendTakeOut()
{
    if (g_state.selectedStored < 0 || g_state.selectedStored >= static_cast<int>(g_state.stored.size()))
    {
        ShowToolTip(225, 82, "\xC7\xEB\xCF\xC8\xD1\xA1\xD4\xF1\xB1\xA3\xB9\xDC\xCF\xE4\xD6\xD0\xB5\xC4\xCA\xB1\xD7\xB0\xA1\xA3");
        return;
    }
    std::vector<unsigned char> arguments;
    AppendInt(arguments, g_state.stored[g_state.selectedStored].key);
    SendAction(Action::TakeOut, arguments);
}

void SendPutIn()
{
    if (g_state.selectedCarried < 0 || g_state.selectedCarried >= static_cast<int>(g_state.carried.size()))
    {
        ShowToolTip(285, 82, "\xC7\xEB\xCF\xC8\xD1\xA1\xD4\xF1\xCB\xE6\xC9\xED\xD0\xAF\xB4\xF8\xB5\xC4\xCF\xD6\xBD\xF0\xD7\xB0\xB1\xB8\xA1\xA3");
        return;
    }
    const PacketItem& item = g_state.carried[g_state.selectedCarried];
    std::vector<unsigned char> arguments;
    AppendShort(arguments, static_cast<WORD>(item.key));
    AppendInt(arguments, item.itemId);
    SendAction(Action::PutIn, arguments);
}

void SendEquipDirect()
{
    if (g_state.selectedStored < 0 || g_state.selectedStored >= static_cast<int>(g_state.stored.size()))
    {
        ShowToolTip(225, 82, "\xC7\xEB\xCF\xC8\xD1\xA1\xD4\xF1\xB1\xA3\xB9\xDC\xCF\xE4\xD6\xD0\xB5\xC4\xCA\xB1\xD7\xB0\xA1\xA3");
        return;
    }
    const PacketItem& item = g_state.stored[g_state.selectedStored];
    const short target = ResolveCashEquipTarget(item.itemId);
    if (!target)
    {
        ShowToolTip(225, 82, "\xB8\xC3\xCA\xB1\xD7\xB0\xC3\xBB\xD3\xD0\xBF\xC9\xD3\xC3\xB5\xC4\xCF\xD6\xBD\xF0\xD7\xB0\xB1\xB8\xC0\xB8\xCE\xBB\xA1\xA3");
        return;
    }
    std::vector<unsigned char> arguments;
    AppendInt(arguments, item.key);
    AppendShort(arguments, static_cast<WORD>(target));
    SendAction(Action::EquipDirect, arguments);
}

const char* ResultText(BYTE result)
{
    switch (result)
    {
    case 0: return "\xCA\xB1\xD7\xB0\xB1\xA3\xB9\xDC\xCF\xE4\xD2\xD1\xB8\xFC\xD0\xC2\xA1\xA3";
    case 1: return "\xCA\xB1\xD7\xB0\xB1\xA3\xB9\xDC\xCF\xE4\xC7\xEB\xC7\xF3\xCE\xDE\xD0\xA7\xA1\xA3";
    case 2: return "\xB1\xA3\xB9\xDC\xCF\xE4\xC4\xDA\xC8\xDD\xD2\xD1\xB1\xE4\xBB\xAF\xA3\xAC\xC1\xD0\xB1\xED\xD2\xD1\xCB\xA2\xD0\xC2\xA1\xA3";
    case 3: return "\xCA\xB1\xD7\xB0\xB1\xA3\xB9\xDC\xCF\xE4\xD2\xD1\xC2\xFA\xA1\xA3";
    case 4: return "\xD7\xB0\xB1\xB8\xC0\xB8\xD2\xD1\xC2\xFA\xA1\xA3";
    case 5: return "\xD6\xBB\xC4\xDC\xB1\xA3\xB9\xDC\xCF\xD6\xBD\xF0\xD7\xB0\xB1\xB8\xA1\xA3";
    case 6: return "\xCB\xF9\xD1\xA1\xB5\xC0\xBE\xDF\xD2\xD1\xB2\xBB\xB4\xE6\xD4\xDA\xA1\xA3";
    case 7: return "\xB8\xC3\xCA\xB1\xD7\xB0\xCE\xDE\xB7\xA8\xB4\xA9\xB4\xF7\xB5\xBD\xC4\xBF\xB1\xEA\xC0\xB8\xCE\xBB\xA1\xA3";
    case 8: return "\xBD\xE1\xBB\xE9\xBD\xE4\xD6\xB8\xBA\xCD\xB9\xD8\xCF\xB5\xB5\xC0\xBE\xDF\xB2\xBB\xC4\xDC\xB4\xE6\xC8\xEB\xA1\xA3";
    case 9: return "\xD4\xA4\xC9\xE8\xD2\xD1\xB1\xA3\xB4\xE6\xA1\xA3";
    case 10: return "\xD4\xA4\xC9\xE8\xD2\xD1\xD3\xA6\xD3\xC3\xA1\xA3";
    case 11: return "\xD4\xA4\xC9\xE8\xD2\xD1\xC9\xBE\xB3\xFD\xA1\xA3";
    case 12: return "\xB8\xC3\xD4\xA4\xC9\xE8\xC9\xD0\xCE\xB4\xB1\xA3\xB4\xE6\xA1\xA3";
    case 13: return "\xD2\xC2\xB9\xF1\xD6\xD0\xC8\xB1\xC9\xD9\xD4\xA4\xC9\xE8\xCB\xF9\xD0\xE8\xCA\xB1\xD7\xB0\xA1\xA3";
    case 14: return "\xD4\xA4\xC9\xE8\xBD\xF6\xB2\xBF\xB7\xD6\xD3\xA6\xD3\xC3\xA3\xAC\xC7\xEB\xBC\xEC\xB2\xE9\xD2\xC2\xB9\xF1\xA1\xA3";
    default: return "\xCE\xB4\xD6\xAA\xB5\xC4\xCA\xB1\xD7\xB0\xB1\xA3\xB9\xDC\xCF\xE4\xBD\xE1\xB9\xFB\xA1\xA3";
    }
}

void CloseWindow(bool notifyServer)
{
    if (notifyServer && g_state.sessionId)
    {
        SendAction(Action::Close);
    }
    ClearToolTip();
    g_state.searchActive = false;
    SetKeyboardFocus(false);
    SetShow(false);
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

void __fastcall WindowDraw(void* window, void*, const RECT* rect)
{
    __try
    {
        g_drawWnd(window, nullptr, rect);
        DWORD destination = 0;
        g_getCanvas(window, nullptr, &destination);
        if (!destination)
        {
            return;
        }

        // IWzCanvas::Draw uses bitmap coordinates directly; WZ origins are not applied here.
        DrawCanvas(destination, g_state.backgrnd2Canvas, 7, 7);
        DrawCanvas(destination, g_state.backgrnd3Canvas, 8, 11);
        DrawCanvas(destination, g_state.searchCanvas, kSearchX, kSearchY);
        const int sortMode = (std::max)(0, (std::min)(g_state.sortMode, 2));
        DrawCanvas(destination,
            g_state.pressedTarget == HitTarget::Sorting
                ? g_state.sortingPressedCanvases[sortMode]
                : g_state.sortingCanvases[sortMode],
            kSortingX, kSortingY);
        DrawCanvas(destination, g_state.lockerTitleCanvas, 16, 17);
        DrawCanvas(destination, g_state.carriedTitleCanvas, 324, 17);

        DrawButton(destination, g_state.exitButton, HitTarget::Exit, kExitX, kExitY);
        DrawButton(destination, g_state.getButton, HitTarget::Get, kGetX, kGetY);
        DrawButton(destination, g_state.sortButton, HitTarget::Sort, kSortX, kSortY);
        DrawButton(destination, g_state.putButton, HitTarget::Put, kPutX, kPutY);
        DrawButton(destination, g_state.dressButton, HitTarget::Dress, kDressX, kDressY);
        DrawButton(destination, g_state.previousButton,
            HitTarget::StoredPrevious, 155, 377);
        DrawButton(destination, g_state.nextButton, HitTarget::StoredNext, 273, 377);
        DrawButton(destination, g_state.previousButton,
            HitTarget::CarriedPrevious, 325, 377);
        DrawButton(destination, g_state.nextButton, HitTarget::CarriedNext, 448, 377);

        for (int index = 0; index < kPresetCount; ++index)
        {
            const DWORD canvas = index == g_state.selectedPreset || index == g_state.hoveredPreset
                ? g_state.presetSelectedCanvases[index]
                : g_state.presets[index].defined
                    ? g_state.presetFilledCanvases[index]
                    : g_state.presetEmptyCanvases[index];
            DrawCanvas(destination, canvas, kPresetX + index * kPresetGap, kPresetY);
        }

        for (int cell = 0; cell < kStoredPageSize; ++cell)
        {
            const int storedIndex = ItemAtCell(g_state.stored, g_state.storedPage, kStoredPageSize, cell);
            if (storedIndex >= 0)
            {
                const int column = cell % kStoredColumns;
                const int row = cell / kStoredColumns;
                const int cellX = kStoredGridX + column * kCellWidth;
                const int cellY = kGridTop + row * kCellHeight;
                if (storedIndex == g_state.selectedStored)
                {
                    DrawCanvas(destination, g_state.selectCanvas, cellX + 2, cellY);
                }
                PacketItem& item = g_state.stored[storedIndex];
                if (!item.icon)
                {
                    item.icon = LoadItemIcon(item.itemId);
                }
                DrawCanvas(destination, item.icon, cellX + 2, cellY);
            }
        }

        for (int cell = 0; cell < kCarriedPageSize; ++cell)
        {
            const int carriedIndex = ItemAtCell(g_state.carried, g_state.carriedPage, kCarriedPageSize, cell);
            if (carriedIndex >= 0)
            {
                const int column = cell % kCarriedColumns;
                const int row = cell / kCarriedColumns;
                const int cellX = kCarriedGridX + column * kCellWidth;
                const int cellY = kGridTop + row * kCellHeight;
                if (carriedIndex == g_state.selectedCarried)
                {
                    DrawCanvas(destination, g_state.selectCanvas, cellX + 2, cellY);
                }
                PacketItem& item = g_state.carried[carriedIndex];
                if (!item.icon)
                {
                    item.icon = LoadItemIcon(item.itemId);
                }
                DrawCanvas(destination, item.icon, cellX + 2, cellY);
            }
        }

        DWORD vtable = *reinterpret_cast<DWORD*>(destination);
        reinterpret_cast<ULONG(__stdcall*)(DWORD)>(*reinterpret_cast<DWORD*>(vtable + 8))(destination);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("fashionLocker.draw", "code=0x%08lX", GetExceptionCode());
    }
}

void __fastcall WindowOnKey(void* ui, void*, unsigned int key, unsigned int flags)
{
    if ((flags & 0x80000000u) != 0)
    {
        return;
    }
    if (key == VK_ESCAPE)
    {
        CloseWindow(true);
        return;
    }
    if (!g_state.searchActive && key >= '1' && key <= '6')
    {
        const int presetIndex = static_cast<int>(key - '1');
        g_state.selectedPreset = presetIndex;
        const Action action = (GetKeyState(VK_SHIFT) & 0x8000) != 0
                || !g_state.presets[presetIndex].defined
            ? Action::SavePreset
            : Action::ApplyPreset;
        SendPresetAction(action, presetIndex);
        Invalidate();
        return;
    }
    if (!g_state.searchActive)
    {
        return;
    }
    if (key == VK_RETURN)
    {
        g_state.searchActive = false;
        SetKeyboardFocus(false);
    }
    else if (key == VK_BACK && !g_state.search.empty())
    {
        const size_t length = g_state.search.size();
        if (length >= 2 && IsDBCSLeadByteEx(CP_ACP,
                static_cast<BYTE>(g_state.search[length - 2])))
        {
            g_state.search.resize(length - 2);
        }
        else
        {
            g_state.search.pop_back();
        }
    }
    else if (key == VK_DELETE)
    {
        g_state.search.clear();
    }
    else if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || key == VK_SPACE)
    {
        if (g_state.search.size() < 32)
        {
            g_state.search.push_back(static_cast<char>(key));
        }
    }
    else
    {
        return;
    }
    g_state.storedPage = 0;
    g_state.carriedPage = 0;
    ClampPages();
    ShowToolTip(300, 66, std::string("\xCB\xD1\xCB\xF7\xA3\xBA") + g_state.search);
    Invalidate();
    (void)ui;
}

void __fastcall WindowOnIMEResult(void*, void*, const char* result)
{
    if (!g_state.searchActive || !result || !*result || g_state.search.size() >= 32)
    {
        return;
    }
    const size_t resultLength = std::strlen(result);
    if (resultLength > 32 - g_state.search.size())
    {
        return;
    }
    g_state.search.append(result, resultLength);
    g_state.storedPage = 0;
    g_state.carriedPage = 0;
    ClampPages();
    ShowToolTip(300, 66, std::string("\xCB\xD1\xCB\xF7\xA3\xBA") + g_state.search);
    Invalidate();
}

void ActivateTarget(HitTarget target)
{
    switch (target)
    {
    case HitTarget::Exit:
        CloseWindow(true);
        break;
    case HitTarget::Get:
        SendTakeOut();
        break;
    case HitTarget::Sort:
        SendAction(Action::Sort);
        break;
    case HitTarget::Put:
        SendPutIn();
        break;
    case HitTarget::Dress:
        SendEquipDirect();
        break;
    case HitTarget::Search:
        g_state.searchActive = true;
        SetKeyboardFocus(true);
        ShowToolTip(kSearchX + 118, kSearchY + 18,
            std::string("\xCB\xD1\xCB\xF7\xA3\xBA") + g_state.search);
        break;
    case HitTarget::Sorting:
        g_state.sortMode = (g_state.sortMode + 1) % 3;
        g_state.storedPage = 0;
        g_state.carriedPage = 0;
        ShowToolTip(kSortingX + 50, kSortingY + 20,
            g_state.sortMode == 0 ? "\xC5\xC5\xD0\xF2\xA3\xBA\xB5\xC0\xBE\xDF\x20\x49\x44"
            : g_state.sortMode == 1 ? "\xC5\xC5\xD0\xF2\xA3\xBA\xC3\xFB\xB3\xC6\xC9\xFD\xD0\xF2"
            : "\xC5\xC5\xD0\xF2\xA3\xBA\xC3\xFB\xB3\xC6\xBD\xB5\xD0\xF2");
        break;
    case HitTarget::StoredPrevious:
        --g_state.storedPage;
        break;
    case HitTarget::StoredNext:
        ++g_state.storedPage;
        break;
    case HitTarget::CarriedPrevious:
        --g_state.carriedPage;
        break;
    case HitTarget::CarriedNext:
        ++g_state.carriedPage;
        break;
    default:
        break;
    }
    Invalidate();
}

void __fastcall WindowOnMouseButton(void*, void*, unsigned int message, unsigned int, int x, int y)
{
    const int presetIndex = PresetAtPoint(x, y);
    if (message == WM_LBUTTONDOWN)
    {
        g_state.hoveredTarget = TargetAtPoint(x, y);
        g_state.pressedTarget = IsTargetEnabled(g_state.hoveredTarget)
            ? g_state.hoveredTarget
            : HitTarget::None;
        if (presetIndex >= 0)
        {
            g_state.selectedPreset = presetIndex;
        }
        Invalidate();
        return;
    }
    if (message == WM_RBUTTONUP)
    {
        if (presetIndex >= 0 && g_state.presets[presetIndex].defined)
        {
            g_state.selectedPreset = presetIndex;
            SendPresetAction(Action::DeletePreset, presetIndex);
            Invalidate();
        }
        return;
    }
    if (message != WM_LBUTTONUP && message != WM_LBUTTONDBLCLK)
    {
        return;
    }

    const HitTarget releasedTarget = TargetAtPoint(x, y);
    const HitTarget pressedTarget = g_state.pressedTarget;
    g_state.pressedTarget = HitTarget::None;
    if (message == WM_LBUTTONUP && releasedTarget != HitTarget::None
        && releasedTarget == pressedTarget && IsTargetEnabled(releasedTarget))
    {
        ActivateTarget(releasedTarget);
        return;
    }
    if (presetIndex >= 0)
    {
        g_state.selectedPreset = presetIndex;
        if (message == WM_LBUTTONUP)
        {
            const Action action = (GetKeyState(VK_SHIFT) & 0x8000) != 0
                    || !g_state.presets[presetIndex].defined
                ? Action::SavePreset
                : Action::ApplyPreset;
            SendPresetAction(action, presetIndex);
        }
        Invalidate();
        return;
    }
    if (y >= kGridTop && y < kGridTop + kGridRows * kCellHeight)
    {
        const int row = (y - kGridTop) / kCellHeight;
        if (x >= kStoredGridX && x < kStoredGridX + kStoredColumns * kCellWidth)
        {
            const int column = (x - kStoredGridX) / kCellWidth;
            g_state.selectedStored = ItemAtCell(
                g_state.stored, g_state.storedPage, kStoredPageSize, row * kStoredColumns + column);
            g_state.selectedCarried = -1;
            if (message == WM_LBUTTONDBLCLK && g_state.selectedStored >= 0)
            {
                SendEquipDirect();
            }
            Invalidate();
            return;
        }
        if (x >= kCarriedGridX && x < kCarriedGridX + kCarriedColumns * kCellWidth)
        {
            const int column = (x - kCarriedGridX) / kCellWidth;
            g_state.selectedCarried = ItemAtCell(
                g_state.carried, g_state.carriedPage, kCarriedPageSize, row * kCarriedColumns + column);
            g_state.selectedStored = -1;
            if (message == WM_LBUTTONDBLCLK && g_state.selectedCarried >= 0)
            {
                SendPutIn();
            }
            Invalidate();
            return;
        }
    }
    Invalidate();
}

int __fastcall WindowOnMouseMove(void*, void*, int x, int y)
{
    int storedIndex = -1;
    int carriedIndex = -1;
    const int presetIndex = PresetAtPoint(x, y);
    const HitTarget hoveredTarget = TargetAtPoint(x, y);
    if (y >= kGridTop && y < kGridTop + kGridRows * kCellHeight)
    {
        const int row = (y - kGridTop) / kCellHeight;
        if (x >= kStoredGridX && x < kStoredGridX + kStoredColumns * kCellWidth)
        {
            const int column = (x - kStoredGridX) / kCellWidth;
            storedIndex = ItemAtCell(
                g_state.stored, g_state.storedPage, kStoredPageSize, row * kStoredColumns + column);
        }
        else if (x >= kCarriedGridX && x < kCarriedGridX + kCarriedColumns * kCellWidth)
        {
            const int column = (x - kCarriedGridX) / kCellWidth;
            carriedIndex = ItemAtCell(
                g_state.carried, g_state.carriedPage, kCarriedPageSize, row * kCarriedColumns + column);
        }
    }
    if (storedIndex != g_state.hoveredStored || carriedIndex != g_state.hoveredCarried
        || presetIndex != g_state.hoveredPreset || hoveredTarget != g_state.hoveredTarget)
    {
        g_state.hoveredStored = storedIndex;
        g_state.hoveredCarried = carriedIndex;
        g_state.hoveredPreset = presetIndex;
        g_state.hoveredTarget = hoveredTarget;
        ClearToolTip();
        const PacketItem* item = storedIndex >= 0 ? &g_state.stored[storedIndex]
            : carriedIndex >= 0 ? &g_state.carried[carriedIndex]
            : nullptr;
        if (item)
        {
            char label[512] = {};
            std::snprintf(label, sizeof(label), "%s (%d)", item->name.c_str(), item->itemId);
            ShowToolTip(x + 12, y + 18, label);
        }
        else if (presetIndex >= 0)
        {
            const FashionPreset& preset = g_state.presets[presetIndex];
            std::string label;
            if (preset.defined)
            {
                label = preset.name
                    + "\xA3\xBA\xB5\xE3\xBB\xF7\xD3\xA6\xD3\xC3\xA3\xAC\x53\x68\x69\x66\x74\x2B\xB5\xE3\xBB\xF7\xB8\xB2\xB8\xC7\xA3\xAC\xD3\xD2\xBC\xFC\xC9\xBE\xB3\xFD";
            }
            else
            {
                label = std::string("\xD4\xA4\xC9\xE8 ") + std::to_string(presetIndex + 1)
                    + "\xA3\xBA\xB5\xE3\xBB\xF7\xB1\xA3\xB4\xE6\xB5\xB1\xC7\xB0\xB4\xA9\xB4\xEE";
            }
            ShowToolTip(x + 12, y + 18, label);
        }
        Invalidate();
    }
    return 0;
}

void __fastcall WindowOnMouseEnter(void*, void*, int entered)
{
    if (!entered)
    {
        g_state.hoveredStored = -1;
        g_state.hoveredCarried = -1;
        g_state.hoveredPreset = -1;
        g_state.hoveredTarget = HitTarget::None;
        g_state.pressedTarget = HitTarget::None;
        ClearToolTip();
        Invalidate();
    }
}

void __fastcall WindowSetShow(void*, void*, int show)
{
    SetShow(show != 0);
}

int __fastcall WindowOnSetFocus(void*, void*, int focused)
{
    if (focused != 0)
    {
        // Mouse-down normally focuses every CWnd. Only the search box needs
        // keyboard input; rejecting other focus requests keeps gameplay keys live.
        return g_state.searchActive ? 1 : 0;
    }
    g_state.searchActive = false;
    return 1;
}

int __fastcall WindowIsShown(void*, void*)
{
    return IsShown() ? 1 : 0;
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

bool CreateNativeLockerWindow()
{
    if (g_state.window)
    {
        return true;
    }
    g_state.window = new unsigned char[kNativeWindowSize];
    std::memset(g_state.window, 0, kNativeWindowSize);
    __try
    {
        g_constructWnd(g_state.window, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("fashionLocker.construct", "code=0x%08lX", GetExceptionCode());
        delete[] g_state.window;
        g_state.window = nullptr;
        return false;
    }
    *reinterpret_cast<DWORD*>(g_state.window) = reinterpret_cast<DWORD>(g_state.primaryVtable);
    *reinterpret_cast<DWORD*>(g_state.window + 4) = reinterpret_cast<DWORD>(g_state.uiVtable);
    *reinterpret_cast<DWORD*>(g_state.window + 8) = reinterpret_cast<DWORD>(g_state.refVtable);

    DWORD background = 0;
    __try
    {
        g_constructZXStringW(&background, nullptr, L"UI/UIWindow.img/FashionLocker/backgrnd");
        g_setBackgrnd(g_state.window, nullptr, background, 0, 0);
        const int left = (std::max)(0, (Client::m_nGameWidth - kWindowWidth) / 2);
        const int top = (std::max)(0, (Client::m_nGameHeight - kWindowHeight) / 2);
        g_createWnd(g_state.window, nullptr, left, top, kWindowWidth, kWindowHeight, 10, 1, nullptr, 0);
        g_state.shown = true;
        g_createToolTip(g_state.tooltip, nullptr);
        g_state.tooltipCreated = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashReporter::RecordEvent("fashionLocker.create", "code=0x%08lX", GetExceptionCode());
        g_state.window = nullptr;
        return false;
    }

    g_state.selectCanvas = LoadCanvas(L"UI/UIWindow.img/MonsterBook/select");
    g_state.backgrnd2Canvas = LoadCanvas(L"UI/UIWindow.img/FashionLocker/backgrnd2");
    g_state.backgrnd3Canvas = LoadCanvas(L"UI/UIWindow.img/FashionLocker/backgrnd3");
    g_state.getButton = LoadButtonCanvases(L"UI/UIWindow.img/FashionLocker/BtGet");
    g_state.putButton = LoadButtonCanvases(L"UI/UIWindow.img/FashionLocker/BtPut");
    g_state.sortButton = LoadButtonCanvases(L"UI/UIWindow.img/FashionLocker/BtSort");
    g_state.exitButton = LoadButtonCanvases(L"UI/UIWindow.img/FashionLocker/BtExit");
    g_state.dressButton = LoadButtonCanvases(L"UI/UIWindow.img/FashionLocker/BtDress");
    g_state.previousButton = LoadButtonCanvases(L"UI/UIWindow.img/MonsterBook/arrowLeft");
    g_state.nextButton = LoadButtonCanvases(L"UI/UIWindow.img/MonsterBook/arrowRight");
    g_state.searchCanvas = LoadCanvas(L"UI/UIWindow.img/FashionLocker/storageSearch/backgrnd");
    g_state.sortingCanvases[0] = LoadCanvas(
        L"UI/UIWindow.img/FashionLocker/Sorting/Default/Bt/normal/0");
    g_state.sortingCanvases[1] = LoadCanvas(
        L"UI/UIWindow.img/FashionLocker/Sorting/NameAscending/Bt/normal/0");
    g_state.sortingCanvases[2] = LoadCanvas(
        L"UI/UIWindow.img/FashionLocker/Sorting/NameDescending/Bt/normal/0");
    g_state.sortingPressedCanvases[0] = LoadCanvas(
        L"UI/UIWindow.img/FashionLocker/Sorting/Default/Bt/pressed/0");
    g_state.sortingPressedCanvases[1] = LoadCanvas(
        L"UI/UIWindow.img/FashionLocker/Sorting/NameAscending/Bt/pressed/0");
    g_state.sortingPressedCanvases[2] = LoadCanvas(
        L"UI/UIWindow.img/FashionLocker/Sorting/NameDescending/Bt/pressed/0");
    g_state.lockerTitleCanvas = LoadCanvas(L"UI/UIWindow.img/FashionLocker/lockerTitle");
    g_state.carriedTitleCanvas = LoadCanvas(L"UI/UIWindow.img/FashionLocker/carriedTitle");
    for (int index = 0; index < kPresetCount; ++index)
    {
        wchar_t path[128] = {};
        swprintf_s(path, _countof(path), L"UI/UIWindow.img/FashionLocker/presets/%d/empty", index + 1);
        g_state.presetEmptyCanvases[index] = LoadCanvas(path);
        swprintf_s(path, _countof(path), L"UI/UIWindow.img/FashionLocker/presets/%d/filled", index + 1);
        g_state.presetFilledCanvases[index] = LoadCanvas(path);
        swprintf_s(path, _countof(path), L"UI/UIWindow.img/FashionLocker/presets/%d/selected", index + 1);
        g_state.presetSelectedCanvases[index] = LoadCanvas(path);
    }
    return true;
}

bool ReadItems(PacketReader& reader, std::vector<PacketItem>& items)
{
    WORD count = 0;
    if (!reader.ReadShort(count) || count > 4096)
    {
        return false;
    }
    std::vector<PacketItem> parsed;
    parsed.reserve(count);
    for (WORD index = 0; index < count; ++index)
    {
        PacketItem item;
        if (!reader.ReadInt(item.key) || !reader.ReadInt(item.itemId) || !reader.ReadString(item.name))
        {
            return false;
        }
        parsed.push_back(std::move(item));
    }
    items.swap(parsed);
    return true;
}

bool ReadPresets(PacketReader& reader, std::array<FashionPreset, kPresetCount>& presets)
{
    BYTE count = 0;
    if (!reader.ReadByte(count) || count != kPresetCount)
    {
        return false;
    }

    std::array<FashionPreset, kPresetCount> parsed;
    std::array<bool, kPresetCount> seen = {};
    for (int index = 0; index < kPresetCount; ++index)
    {
        BYTE slot = 0;
        BYTE defined = 0;
        if (!reader.ReadByte(slot) || !reader.ReadByte(defined)
            || slot < 1 || slot > kPresetCount || defined > 1 || seen[slot - 1])
        {
            return false;
        }
        seen[slot - 1] = true;
        FashionPreset& preset = parsed[slot - 1];
        preset.defined = defined != 0;
        if (!preset.defined)
        {
            continue;
        }

        BYTE itemCount = 0;
        if (!reader.ReadString(preset.name) || !reader.ReadByte(itemCount) || itemCount > 32)
        {
            return false;
        }
        preset.items.reserve(itemCount);
        for (BYTE itemIndex = 0; itemIndex < itemCount; ++itemIndex)
        {
            WORD rawPosition = 0;
            int itemId = 0;
            if (!reader.ReadShort(rawPosition) || !reader.ReadInt(itemId))
            {
                return false;
            }
            const short position = static_cast<short>(rawPosition);
            if (position > -100 || itemId <= 0)
            {
                return false;
            }
            preset.items.push_back({position, itemId});
        }
    }
    presets = std::move(parsed);
    return true;
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
}

bool FashionLockerWnd::Install(bool enableLog)
{
    g_state.logEnabled = enableLog;
    if (!MatchesExpectedClient())
    {
        CrashReporter::RecordEvent("fashionLocker.install", "unsupported client ABI");
        return false;
    }
    InitializeVtables();
    g_state.installed = true;
    return true;
}

bool FashionLockerWnd::HandlePacket(const unsigned char* data, unsigned short length)
{
    if (!g_state.installed || !data || length < 22)
    {
        return false;
    }
    const WORD opcode = static_cast<WORD>(data[4] | (data[5] << 8));
    if (opcode != kOpenOpcode && opcode != kUpdateOpcode)
    {
        return false;
    }

    PacketReader reader{data + 6, data + length};
    WORD magic = 0;
    BYTE version = 0;
    BYTE result = 0;
    int session = 0;
    int revision = 0;
    int capacity = 0;
    std::vector<PacketItem> stored;
    std::vector<PacketItem> carried;
    std::array<FashionPreset, kPresetCount> presets;
    if (!reader.ReadShort(magic) || !reader.ReadByte(version) || !reader.ReadByte(result)
        || !reader.ReadInt(session) || !reader.ReadInt(revision) || !reader.ReadInt(capacity)
        || magic != kProtocolMagic || version != kProtocolVersion
        || !ReadItems(reader, stored) || !ReadItems(reader, carried)
        || !ReadPresets(reader, presets) || reader.cursor != reader.end)
    {
        CrashReporter::RecordEvent("fashionLocker.packet", "invalid snapshot length=%u", length);
        return true;
    }

    g_state.sessionId = session;
    g_state.revision = revision;
    g_state.capacity = capacity;
    g_state.lastResult = result;
    g_state.stored.swap(stored);
    g_state.carried.swap(carried);
    g_state.presets = std::move(presets);
    g_state.selectedStored = -1;
    g_state.selectedCarried = -1;
    g_state.hoveredStored = -1;
    g_state.hoveredCarried = -1;
    g_state.hoveredPreset = -1;
    g_state.hoveredTarget = HitTarget::None;
    g_state.pressedTarget = HitTarget::None;
    ClampPages();

    if (!CreateNativeLockerWindow())
    {
        return true;
    }
    if (opcode == kOpenOpcode)
    {
        SetShow(true);
    }
    Invalidate();
    if (result != 0)
    {
        ShowToolTip(225, 82, ResultText(result));
    }
    if (g_state.logEnabled)
    {
        CrashReporter::RecordRecentEvent("fashionLocker.packet",
            "opcode=0x%04X result=%u session=%d revision=%d stored=%u carried=%u capacity=%d",
            opcode, result, session, revision, static_cast<unsigned int>(g_state.stored.size()),
            static_cast<unsigned int>(g_state.carried.size()), capacity);
    }
    return true;
}

void FashionLockerWnd::OnFieldUpdate()
{
    if (!g_state.installed)
    {
        return;
    }
    const bool shortcutDown = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (shortcutDown && !g_state.shortcutDown)
    {
        if (IsShown())
        {
            CloseWindow(true);
        }
        else
        {
            SendAction(Action::Open);
        }
    }
    g_state.shortcutDown = shortcutDown;
}

void FashionLockerWnd::OnFieldDispose()
{
    if (IsShown())
    {
        CloseWindow(false);
    }
    g_state.sessionId = 0;
    g_state.revision = 0;
    g_state.stored.clear();
    g_state.carried.clear();
    g_state.presets.fill(FashionPreset{});
    g_state.selectedStored = -1;
    g_state.selectedCarried = -1;
    g_state.selectedPreset = -1;
    g_state.hoveredPreset = -1;
    g_state.hoveredTarget = HitTarget::None;
    g_state.pressedTarget = HitTarget::None;
}
