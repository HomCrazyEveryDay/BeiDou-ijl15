#include "../ezorsia/SetItemTooltip.cpp"
#include "VisitorsShuttleTest.h"
#include <thread>

int Client::m_nGameWidth = 800;
int Client::m_nGameHeight = 600;

namespace {
using TestSupport::Require;
using TestSupport::Set;
int logCount = 0, frameCount = 0, liveLayers = 0, inventoryReferences = 0;
void* layerMethods[80]{};
DreamCanvas::Factory pcomFactory = nullptr;
std::wstring outputPath;
BYTE owner[1304]{}, context[0x2200]{}, character[16]{}, otherCharacter[16]{};
struct Item { DWORD padding[3]{}; int id; };
Item helmet{{},1003116}, suit{{},1052278}, shoes{{},1072450}, gloves{{},1082278}, basic{{},1003115};
std::map<int, Item*> inventory;
EquipArguments expectedEquip{};
CompareArguments expectedCompare{};
int equipCalls = 0, compareCalls = 0;
bool comparisonCached = false;
int layerMoves = 0;
BYTE windowManager[0xe0]{};
void* uiOrigin = nullptr;

struct Layer {
    void** methods = layerMethods;
    ULONG refs = 1;
    int left = 0, top = 0, width, height;
    Layer* origin = nullptr;
    Layer(int widthValue, int heightValue) : width(widthValue), height(heightValue) { if (width) ++liveLayers; }
};
ULONG __stdcall AddRef(Layer* layer) { return ++layer->refs; }
HRESULT __stdcall QueryLayer(Layer* layer, REFIID iid, void** result) {
    if (iid != vectorId && iid != IID_IUnknown) { *result = nullptr; return E_NOINTERFACE; }
    *result = layer;
    AddRef(layer);
    return S_OK;
}
ULONG __stdcall Release(Layer* layer) {
    const ULONG result = --layer->refs;
    if (!result) {
        if (layer->origin) Release(layer->origin);
        if (layer->width) --liveLayers;
        delete layer;
    }
    return result;
}
HRESULT __stdcall Left(Layer* layer, LONG* value) {
    *value = layer->left + (layer->origin ? Metric(layer->origin,0x20) : 0);
    return S_OK;
}
HRESULT __stdcall Top(Layer* layer, LONG* value) {
    *value = layer->top + (layer->origin ? Metric(layer->origin,0x28) : 0);
    return S_OK;
}
HRESULT __stdcall LayerWidth(Layer* layer, LONG* value) { *value = layer->width; return S_OK; }
HRESULT __stdcall LayerHeight(Layer* layer, LONG* value) { *value = layer->height; return S_OK; }
HRESULT __stdcall GetOrigin(Layer* layer, VARIANT* value) {
    VariantInit(value);
    if (layer->origin) {
        value->vt = VT_UNKNOWN;
        value->punkVal = reinterpret_cast<IUnknown*>(layer->origin);
        AddRef(layer->origin);
    }
    return S_OK;
}
HRESULT __stdcall Origin(Layer* layer, VARIANT value) {
    Require(value.vt == VT_UNKNOWN, "native vector origin variant");
    if (layer->origin) Release(layer->origin);
    layer->origin = reinterpret_cast<Layer*>(value.punkVal);
    if (layer->origin) AddRef(layer->origin);
    return S_OK;
}
HRESULT __stdcall RelMove(Layer* layer, int left, int top, VARIANT first, VARIANT second) {
    Require(first.vt == VT_ERROR && second.vt == VT_ERROR, "RelMove ABI missing optional arguments");
    layer->left = left;
    layer->top = top;
    ++layerMoves;
    return S_OK;
}
std::wstring Label(unsigned id) {
    switch (id) {
    case 617: return L"\u5e3d\u5b50";
    case 621: return L"\u5957\u670d";
    case 624: return L"\u978b\u5b50";
    case 625: return L"\u624b\u5957";
    case 659: return L"\u9632\u5fa1\u529b :";
    case 660: return L"\u9b54\u6cd5\u9632\u5fa1\u529b :";
    case 661: return L"\u547d\u4e2d\u7387 :";
    case 662: return L"\u56de\u907f\u7387 :";
    case 5527: return L"Arial";
    default: Require(false, "only verified StringPool labels requested"); return L"";
    }
}
void* __cdecl StringPool() { return reinterpret_cast<void*>(1); }
void* __fastcall GetString(void* pool, void*, char** result, unsigned id, char formal) {
    Require(pool == reinterpret_cast<void*>(1) && formal == 0, "StringPool thiscall ABI");
    const auto text = Label(id);
    const int size = WideCharToMultiByte(936, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    *result = static_cast<char*>(std::malloc(size));
    Require(*result != nullptr, "allocate test ZXString");
    WideCharToMultiByte(936, 0, text.c_str(), -1, *result, size, nullptr, nullptr);
    return result;
}
void __fastcall FreeString(char** value, void*) { std::free(*value); *value = nullptr; }
int __fastcall ReadItem(const int* value, void*) { return *value; }
void* __fastcall GetItem(void* data, void*, DWORD* result, int type, int position) {
    Require(data == character && type == 1 && position < 0, "only local equipped inventory is queried");
    const auto found = inventory.find(position);
    result[1] = found == inventory.end() ? 0 : reinterpret_cast<DWORD>(found->second);
    if (result[1]) ++inventoryReferences;
    return result;
}
void __fastcall ReleaseItem(DWORD* result, void*) {
    Require(result[1] && inventoryReferences > 0, "balanced native ZRef lifetime");
    --inventoryReferences;
    result[1] = 0;
}
Object NewObject(const wchar_t* name, const GUID& iid) {
    Object result;
    Check(pcomFactory(name, &iid, &result.value, nullptr));
    Require(result.value != nullptr, "real PCOM factory result");
    return result;
}
Object NewCanvas(int width, int height) {
    static const GUID iid = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    auto canvas = NewObject(L"Canvas", iid);
    VARIANT magnification{}, format{};
    magnification.vt = format.vt = VT_I4;
    format.lVal = 2;
    using Create = HRESULT(__stdcall*)(void*, int, int, VARIANT, VARIANT);
    Check(Method<Create>(canvas.value, 0x2c)(canvas.value, width, height, magnification, format));
    return canvas;
}
void VectorTests() {
    const GUID vectorId = {0xf28bd1ed,0x3deb,0x4f92,{0x9e,0xec,0x10,0xef,0x5a,0x1c,0x3f,0xb4}};
    auto origin = NewObject(L"Shape2D#Vector2D", vectorId);
    auto child = NewObject(L"Shape2D#Vector2D", vectorId);
    Move(origin.value, -640, -360);
    SetOrigin(child.value, origin.value);
    Move(child.value, 1000, 200);
    Require(AbsolutePoint(child.value) == Point{360,-160}, "real PCOM get_x/y includes the parent, not relative coordinates");
    Move(child.value, 1000, 200);
    Require(AbsolutePoint(child.value) == Point{360,-160}, "real PCOM RelMove is an absolute relative position, not an additive delta");
    Value parent;
    Check(Method<HRESULT(__stdcall*)(void*,VARIANT*)>(child.value,0x60)(child.value,&parent.value));
    Require(parent.value.vt == VT_UNKNOWN && parent.value.punkVal, "native get_origin returns an owned VT_UNKNOWN");
    Object parentVector;
    Check(parent.value.punkVal->QueryInterface(vectorId, &parentVector.value));
    Require(Metric(parentVector.value,0x20) == -640 && Metric(parentVector.value,0x28) == -360,
        "native get_origin resolves the parent vector, not the viewport or the actor");
    Require(ScreenPoint(child.value, {-640,-360}) == Point{1000,200}, "real PCOM conversion uses viewport top-left coordinates");
    MoveScreen(child.value, {784,200}, {-640,-360});
    Require(AbsolutePoint(child.value) == Point{144,-160}, "production MoveScreen converts the target back to the actual parent origin");
    for (int repeat = 0; repeat < 100; ++repeat) MoveScreen(child.value, {784,200}, {-640,-360});
    Require(ScreenPoint(child.value, {-640,-360}) == Point{784,200}, "real PCOM repeated movement never subtracts the UI origin twice");
    auto grandchild = NewObject(L"Shape2D#Vector2D", vectorId);
    SetOrigin(grandchild.value, child.value);
    MoveScreen(grandchild.value, {1044,200}, {-640,-360});
    Require(AbsolutePoint(grandchild.value) == Point{404,-160}, "real PCOM nested parent conversion places the set at the main right edge");
    MoveScreen(child.value, {100,80}, {-640,-360});
    MoveScreen(grandchild.value, {360,80}, {-640,-360});
    Require(ScreenPoint(grandchild.value, {-640,-360}) == Point{360,80}, "nested positioning remains correct even with same-clock PCOM child snapshot caching");
    std::puts("PASS: real PCOM vector absolute getters, owned origin getter, relative RelMove, nested origins and 100-call idempotence");
}
void* __fastcall FrameMock(Frame* frame, void*, void** canvas, int left, int top, int border, int high, int origin, unsigned color) {
    Require(frame->kind == 1 && frame->width >= 236 && frame->height == 231 && border == 1 && high == 0 && origin == 0
        && color == backgroundColor && left == 0 && top == 0, "native frame thiscall and all seven arguments");
    auto result = NewCanvas(frame->width, frame->height);
    using Rectangle = HRESULT(__stdcall*)(void*, int, int, int, int, unsigned);
    Check(Method<Rectangle>(result.value, 0x8c)(result.value, 0, 0, frame->width, frame->height, color));
    *canvas = result.value;
    result.value = nullptr;
    frame->layer = new Layer(frame->width, frame->height);
    ++frameCount;
    return canvas;
}
void __fastcall EquipMock(void* receiver, void*, EquipArguments arguments) {
    Require(receiver == owner && !std::memcmp(&arguments, &expectedEquip, sizeof(arguments)), "all 18 equip stack words pass unchanged");
    ++equipCalls;
    if (*reinterpret_cast<void**>(owner + 0x10) && (arguments.values[0] != *reinterpret_cast<DWORD*>(owner + 0x14)
        || arguments.values[1] != *reinterpret_cast<DWORD*>(owner + 0x18)))
        reinterpret_cast<ClearTooltip>(Native(0x008E6E23))(owner);
    if (!*reinterpret_cast<void**>(owner + 0x10)) {
        auto layer = new Layer(260, 360);
        layer->left = (std::min)((std::max)(static_cast<int>(arguments.values[0]), 0), Client::m_nGameWidth - 1 - layer->width);
        layer->top = (std::min)((std::max)(static_cast<int>(arguments.values[1]), 0), Client::m_nGameHeight - 1 - layer->height);
        SetOrigin(layer, uiOrigin);
        Set(owner, 0x10, layer);
        Set(owner, 0x14, arguments.values[0]);
        Set(owner, 0x18, arguments.values[1]);
    }
}
void __fastcall CompareMock(void* receiver, void*, CompareArguments arguments) {
    Require(receiver == owner && !std::memcmp(&arguments, &expectedCompare, sizeof(arguments)), "all six comparison stack words pass unchanged");
    ++compareCalls;
    if (comparisonCached) return;
    reinterpret_cast<ClearTooltip>(Native(0x008E6E23))(owner);
    const auto saved = expectedEquip;
    expectedEquip.values[0] = expectedEquip.values[1] = 0;
    reinterpret_cast<EquipBuilder>(Native(0x008E8252))(receiver, expectedEquip);
    auto first = *reinterpret_cast<Layer**>(owner + 0x10);
    first->height = 180;
    Set(owner, 0x10, static_cast<void*>(nullptr));
    reinterpret_cast<ClearTooltip>(Native(0x008E6E23))(owner);
    expectedEquip.values[0] = arguments.values[0];
    expectedEquip.values[1] = arguments.values[1];
    reinterpret_cast<EquipBuilder>(Native(0x008E8252))(receiver, expectedEquip);
    auto second = *reinterpret_cast<Layer**>(owner + 0x10);
    SetOrigin(second, first);
    const bool vertical = arguments.values[4] != 0;
    const auto native = Place(static_cast<int>(arguments.values[0]), static_cast<int>(arguments.values[1]),
        vertical ? (std::max)(first->width, second->width) : first->width + second->width,
        vertical ? first->height + second->height : (std::max)(first->height, second->height), 0, 0, Client::m_nGameWidth, Client::m_nGameHeight);
    Move(first, native.mainX + (vertical ? 0 : second->width), native.mainY + (vertical ? second->height : 0));
    Move(second, vertical ? 0 : -second->width, vertical ? -second->height : 0);
    Release(first);
    expectedEquip = saved;
}
void ClearOwner() { reinterpret_cast<ClearTooltip>(Native(0x008E6E23))(owner); }
void Hover(Item* item = &helmet, int left = 100, int top = 80) {
    for (size_t index = 0; index < _countof(expectedEquip.values); ++index)
        expectedEquip.values[index] = 0x12340000 + static_cast<DWORD>(index);
    expectedEquip.values[0] = left;
    expectedEquip.values[1] = top;
    expectedEquip.values[2] = reinterpret_cast<DWORD>(item);
    reinterpret_cast<EquipBuilder>(Native(0x008E8252))(owner, expectedEquip);
}
void SaveBitmap(void* canvas, const std::wstring& path) {
    const int width = Metric(canvas, 0x40), height = Metric(canvas, 0x48);
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER header{};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(header);
    fileHeader.bfSize = fileHeader.bfOffBits + width * height * 4;
    header.biSize = sizeof(header);
    header.biWidth = width;
    header.biHeight = -height;
    header.biPlanes = 1;
    header.biBitCount = 32;
    std::vector<DWORD> pixels(width * height);
    using Pixel = HRESULT(__stdcall*)(void*, int, int, DWORD*);
    DWORD background = 0;
    Check(Method<Pixel>(canvas, 0x88)(canvas, 0, 0, &background));
    unsigned textPixels = 0;
    for (int top = 0; top < height; ++top) for (int left = 0; left < width; ++left) {
        DWORD value = 0;
        Check(Method<Pixel>(canvas, 0x88)(canvas, left, top, &value));
        if ((value & 0xffffff) != (background & 0xffffff)) ++textPixels;
        pixels[top * width + left] = value | 0xff000000;
    }
    Require(textPixels > 1000, "real native fonts draw visible Chinese and effect glyphs");
    FILE* file = nullptr;
    Require(_wfopen_s(&file, path.c_str(), L"wb") == 0, "open offline content preview");
    std::fwrite(&fileHeader, sizeof(fileHeader), 1, file);
    std::fwrite(&header, sizeof(header), 1, file);
    std::fwrite(pixels.data(), 4, pixels.size(), file);
    std::fclose(file);
}
int protectCall = 0, protectFailure = 0;
BOOL WINAPI ProtectFailure(void* address, SIZE_T size, DWORD protection, PDWORD previous) {
    if (++protectCall == protectFailure) return FALSE;
    return VirtualProtect(address, size, protection, previous);
}
void InstallTests(HMODULE image) {
    Require(!SetItemTooltip::Install(nullptr), "wrong base refused");
    for (auto& patch : patches) for (size_t index = 0; index < patch.size; ++index) {
        auto* site = reinterpret_cast<BYTE*>(patch.address);
        Require(!std::memcmp(site, patch.bytes, patch.size), "patch original bytes from shipped EXE");
        site[index] ^= 1;
        Require(!SetItemTooltip::Install(image), "every mismatched original byte refused");
        site[index] ^= 1;
        for (const auto& other : patches)
            Require(!std::memcmp(reinterpret_cast<void*>(other.address), other.bytes, other.size), "failed preflight never partially patches");
    }
    for (const auto& signature : signatures) {
        auto* site = reinterpret_cast<BYTE*>(signature.address);
        Require(!std::memcmp(site, signature.bytes, signature.size), "dependency ABI original bytes");
        site[0] ^= 1;
        Require(!SetItemTooltip::Install(image), "dependency signature mismatch refused");
        site[0] ^= 1;
    }
    Require(logCount == 1, "signature failures logged once");
    for (const auto& patch : patches) {
        DWORD previous;
        Require(VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READ, &previous) != FALSE, "make hook pages RX");
    }
    protectMemory = ProtectFailure;
    for (protectFailure = 1; protectFailure <= 7; ++protectFailure) {
        protectCall = 0;
        Require(!SetItemTooltip::Install(image) && !installed, "protection failure refuses installation");
        for (const auto& patch : patches) {
            Require(!std::memcmp(reinterpret_cast<void*>(patch.address), patch.bytes, patch.size), "protection failure rolls back original bytes");
            MEMORY_BASIC_INFORMATION information{};
            VirtualQuery(reinterpret_cast<void*>(patch.address), &information, sizeof(information));
            Require(information.Protect == PAGE_EXECUTE_READ, "protection failure rollback restores RX");
        }
    }
    protectMemory = VirtualProtect;
    Require(SetItemTooltip::Install(image) && SetItemTooltip::Install(image), "install is idempotent");
    for (const auto& patch : patches) {
        MEMORY_BASIC_INFORMATION information{};
        VirtualQuery(reinterpret_cast<void*>(patch.address), &information, sizeof(information));
        Require(information.Protect == PAGE_EXECUTE_READ, "hook page protection restored");
        auto* bytes = reinterpret_cast<BYTE*>(patch.address);
        Require(bytes[0] == 0xe9 && bytes + 5 + *reinterpret_cast<int*>(bytes + 1) == patch.target, "relative hook target");
        Require(!std::memcmp(patch.trampoline, patch.bytes, patch.size), "trampoline replays complete instructions");
        auto* tail = static_cast<BYTE*>(patch.trampoline) + patch.size;
        Require(tail[0] == 0xe9 && tail + 5 + *reinterpret_cast<int*>(tail + 1) == bytes + patch.size, "trampoline resumes after preserved prologue");
        VirtualQuery(patch.trampoline, &information, sizeof(information));
        Require(information.Protect == PAGE_EXECUTE_READ, "trampoline has RX not RWX protection");
        DWORD previous;
        VirtualProtect(bytes, 4096, PAGE_EXECUTE_READWRITE, &previous);
    }
    TestSupport::Bytes(0x008EA096, "\xc2\x48\x00", 3);
    TestSupport::Bytes(0x008EBE01, "\xc2\x18\x00", 3);
    TestSupport::Bytes(0x008E6BC2, "\xe8\x5c\x02\x00\x00", 5);
    TestSupport::Jump(Native(0x008E8257), reinterpret_cast<void*>(EquipMock));
    TestSupport::Jump(Native(0x008EBA60), reinterpret_cast<void*>(CompareMock));
    TestSupport::Jump(Native(0x008F3141), reinterpret_cast<void*>(FrameMock));
    TestSupport::Jump(Native(0x004282F7), reinterpret_cast<void*>(GetItem));
    TestSupport::Jump(Native(0x004288C1), reinterpret_cast<void*>(ReleaseItem));
    TestSupport::Jump(Native(0x0042873D), reinterpret_cast<void*>(ReadItem));
    TestSupport::Jump(Native(0x0079E805), reinterpret_cast<void*>(StringPool));
    TestSupport::Jump(Native(0x0079E993), reinterpret_cast<void*>(GetString));
    TestSupport::Jump(Native(0x004062DF), reinterpret_cast<void*>(FreeString));
    std::puts("PASS: original bytes, per-byte fail-closed preflight, seven protection fault rollbacks, RX restore, trampoline bytes and ret 48/18 ABIs");
}
void ModelTests() {
    const auto set = LoadDefinition();
    Require(Valid(set) && set.id == 5 && set.completeCount == 4 && set.members.size() == 16, "native PCOM reads patched set 5");
    Require(set.name == L"\u8fdb\u5316\u6218\u7532\u5957\u88c5", "patched Chinese title");
    Require(set.effects.size() == 2 && set.effects[0].threshold == 2 && set.effects[0].values == std::array<int,4>{20,20,0,0}
        && set.effects[1].threshold == 4 && set.effects[1].values == std::array<int,4>{0,0,20,20}
        && set.effects[0].setKey == 0 && set.effects[1].setKey == 10000, "native PCOM reads unchanged bonuses and cube protection setKey");
    for (const auto& member : set.members) Require(EnabledItem(member.item), "every member also has native info/setItemID=5");
    for (int item : {1003115,1052277,1072449,1082277,1003108,1000000}) Require(!EnabledItem(item), "base pieces, other sets and ordinary equips excluded");
    for (int id : {0,1,2,3,4,6}) { auto other = set; other.id = id; Require(!Valid(other), "server-aligned enabled-set whitelist"); }
    std::vector<int> equipped;
    const int ids[] = {1003116,1052278,1072450,1082278};
    for (int count = 0; count <= 4; ++count) {
        auto state = Count(set, equipped);
        Require(state.count == count, "count every equipped member instance once");
        const auto layout = Compose(set, state, Label);
        Require(layout.width == 236 && layout.height == 231 && layout.divider == 104 && layout.lines.size() == 12, "092 base geometry with localized cube immunity line");
        Require(layout.lines[11].top == 209 && layout.lines[11].color == (count >= 4 ? activeColor : inactiveColor), "cube immunity activates at four pieces");
        Require(layout.lines[11].left == L"\u4e0d\u4f1a\u53d7\u5230\u7acb\u65b9\u4f53\u5185\u5947\u5f02\u529b\u91cf\u7684\u4f24\u5bb3\u3002", "reviewed cube protection translation");
        Require(layout.lines[0].centered && layout.lines[0].top == 8 && layout.lines[0].color == titleColor, "centered green title without invented x/N suffix");
        for (int index = 0; index < 4; ++index)
            Require(layout.lines[index + 1].top == 35 + 16 * index
                && layout.lines[index + 1].color == (index < count ? activeColor : inactiveColor), "member categories highlight independently");
        Require(layout.lines[5].color == titleColor && layout.lines[8].color == titleColor, "092 headers stay green even for inactive thresholds");
        for (int index : {6,7}) Require(layout.lines[index].color == (count >= 2 ? activeColor : inactiveColor), "two-piece block activates cumulatively");
        for (int index : {9,10}) Require(layout.lines[index].color == (count >= 4 ? activeColor : inactiveColor), "four-piece block activates at four");
        if (count != 4) equipped.push_back(ids[count]);
    }
    Require(Count(set, {ids[0],ids[0],1003115}).count == 2, "server counts duplicate instances, not unique IDs or unique categories");
    auto position = Place(100,80,260,360,236,231,800,600);
    Require(position.mainX == 100 && position.mainY == 80 && position.panelX == 360 && position.panelY == 80, "right-hand default");
    position = Place(540,400,260,360,236,231,800,600);
    Require(position.mainX == 304 && position.mainY == 240 && position.panelX == 564 && position.panelY == 240, "092 shifts the whole group left and up without flipping");
    position = Place(304,240,260,360,236,231,800,600);
    Require(position.mainX == 304 && position.mainY == 240 && position.panelX == 564, "exact viewport boundary is unchanged");
    position = Place(210,100,400,360,236,231,800,600);
    Require(position.mainX == 164 && position.panelX == 564, "combined width controls horizontal shift");
    position = Place(1000,600,260,360,236,231,1280,720);
    Require(position.mainX == 784 && position.panelX == 1044 && position.panelY == 360, "configured high-resolution edges");
    position = Place(-30,-40,260,360,236,231,800,600);
    Require(position.mainX == 0 && position.mainY == 0 && position.panelX == 260, "negative coordinates clamp to zero");
    position = Place(100,80,700,700,236,231,800,600);
    Require(position.mainX == 0 && position.mainY == 0 && position.panelX == 700, "oversized group keeps the panel on the right instead of hiding it");
    position = Place(100,80,260,360,236,900,800,600);
    Require(position.mainX == 100 && position.mainY == 0 && position.panelY == 0, "oversized panel height clamps the whole group to the top");
    Bounds first{{540,420},260,180};
    auto comparison = PlaceGroup({{540,60},260,360}, &first, 236,231,800,600);
    Require(comparison.main == Point{304,60} && comparison.comparison == Point{304,420} && comparison.panel == Point{564,60},
        "vertical comparison retains native offsets and aligns the set to the main top");
    first = {{540,240},260,180};
    comparison = PlaceGroup({{280,240},260,360}, &first, 236,231,800,600);
    Require(comparison.main == Point{44,240} && comparison.comparison == Point{304,240} && comparison.panel == Point{564,240},
        "horizontal comparison preserves the two native equip positions modulo one necessary group shift");
    first = {{500,240},500,180};
    comparison = PlaceGroup({{0,240},500,360}, &first, 236,231,800,600);
    Require(comparison.main == Point{0,240} && comparison.comparison == Point{500,240} && comparison.panel == Point{1000,240},
        "oversized comparison is not hidden");
    std::puts("PASS: real PCOM patched XML/IMG fields, all 16 member IMG links, whitelist, 0/1/2/3/4 counts, duplicates and 092 layout model");
}
void LifecycleTests() {
    auto* root = static_cast<Layer*>(uiOrigin);
    Move(root, -400, -300);
    *reinterpret_cast<void**>(Native(0x00BE7918)) = context;
    Set(context, 0x20b8, static_cast<void*>(character));
    Object white, gray;
    const GUID fontId = {0x2bef046d,0xccd6,0x445a,{0x88,0xc4,0x92,0x9f,0xc3,0x5d,0x30,0xac}};
    Check(pcomFactory(L"Canvas#Font", &fontId, &white.value, nullptr));
    Check(pcomFactory(L"Canvas#Font", &fontId, &gray.value, nullptr));
    for (auto pair : {std::make_pair(white.value, activeColor), std::make_pair(gray.value, inactiveColor)}) {
        Text face(L"Arial");
        VARIANT missing{};
        missing.vt = VT_ERROR;
        missing.scode = DISP_E_PARAMNOTFOUND;
        Check(Method<HRESULT(__stdcall*)(void*,BSTR,int,unsigned,VARIANT)>(pair.first,0xc)(pair.first,face.value,12,pair.second,missing));
    }
    Set(owner, 0x440, white.value);
    Set(owner, 0x448, gray.value);
    Client::m_nGameWidth = 1280;
    Client::m_nGameHeight = 720;
    Move(root, -640, -360);
    struct PositionCase { Point request, expected; };
    for (const auto& test : {PositionCase{{100,80},{100,80}}, PositionCase{{400,180},{400,180}},
        PositionCase{{1000,200},{784,200}}, PositionCase{{1100,500},{784,359}}, PositionCase{{400,600},{400,359}}}) {
        ClearOwner();
        Hover(&helmet, test.request.x, test.request.y);
        auto* main = *reinterpret_cast<Layer**>(owner + 0x10);
        auto& current = *panels.at(owner);
        Require(ScreenPoint(main, ScreenOrigin()) == test.expected
            && ScreenPoint(current.frame.layer, ScreenOrigin()) == Point{test.expected.x + 260,test.expected.y},
            "1280x720 native left/middle/right/bottom positions are corrected only by necessary group overflow");
        const auto baseline = current.placement.nativeMain;
        const int stableMoves = layerMoves, stableFrames = frameCount;
        for (int repeat = 0; repeat < 100; ++repeat) {
            Hover(&helmet, test.request.x, test.request.y);
            SetItemTooltip::Refresh();
        }
        Require(layerMoves == stableMoves && frameCount == stableFrames && current.placement.nativeMain == baseline
            && ScreenPoint(main, ScreenOrigin()) == test.expected, "cached hover and stationary refresh are idempotent with no RelMove calls");
        inventory[-1] = &helmet;
        SetItemTooltip::Refresh();
        Require(panels.at(owner)->placement.nativeMain == baseline && ScreenPoint(main, ScreenOrigin()) == test.expected,
            "equipment count repaint preserves the native positioning baseline");
        inventory.clear();
    }
    ClearOwner();
    for (int pixel = 0; pixel < 16; ++pixel) {
        Hover(&helmet, 1080 + pixel, 200);
        Require(ScreenPoint(*reinterpret_cast<void**>(owner + 0x10), ScreenOrigin()) == Point{784,200},
            "native rebuild while moving inside a right-side slot never jumps to the left edge");
    }
    auto* moved = *reinterpret_cast<Layer**>(owner + 0x10);
    Move(moved, 120, 100);
    SetItemTooltip::Refresh();
    Require(panels.at(owner)->placement.nativeMain == Point{120,100} && ScreenPoint(moved, ScreenOrigin()) == Point{120,100},
        "a new native position on the same layer supersedes the previous baseline");
    Object parent(new Layer(0,0));
    SetOrigin(parent.value, uiOrigin);
    Move(parent.value, 30, 40);
    SetOrigin(moved, parent.value);
    Move(moved, 970, 160);
    SetItemTooltip::Refresh();
    Require(moved->left == 754 && moved->top == 160 && ScreenPoint(moved, ScreenOrigin()) == Point{784,200},
        "writeback uses the actual parent origin instead of assuming every main layer is a UI-root child");
    Move(root, -513, -271);
    const int stableOriginMoves = layerMoves;
    for (int repeat = 0; repeat < 20; ++repeat) SetItemTooltip::Refresh();
    Require(layerMoves == stableOriginMoves && ScreenPoint(moved, ScreenOrigin()) == Point{784,200},
        "screen conversion reads the real UI origin and does not hardcode half the viewport");
    ClearOwner();
    Hover(&helmet,400,600);
    auto& resized = *panels.at(owner);
    resized.frame.height = 500;
    PositionPanel(resized);
    Require(ScreenPoint(resized.mainLayer, ScreenOrigin()) == Point{400,220}, "a taller set shifts the group up just enough");
    resized.frame.height = 231;
    PositionPanel(resized);
    Require(ScreenPoint(resized.mainLayer, ScreenOrigin()) == Point{400,359}, "a shorter set returns to the retained native baseline without cumulative offset");
    ClearOwner();
    Client::m_nGameWidth = 800;
    Client::m_nGameHeight = 600;
    Move(root,-400,-300);
    std::puts("PASS: 1280x720 left/middle/right/bottom, 100 cached hover+refresh calls with zero moves, moving slots, repaint, native relocation, nested parent and noncenter UI origin");
    std::wstring reference = L"You don\u2019t get damaged from the strange force inside the Cube.";
    int referenceLines = 0, referenceWidth = 236;
    while (!reference.empty()) {
        Text text(reference);
        VARIANT missing{};
        missing.vt = VT_ERROR;
        missing.scode = DISP_E_PARAMNOTFOUND;
        LONG skipped = 0, consumed = 0;
        using Break = HRESULT(__stdcall*)(void*,BSTR,int,LONG*,VARIANT,LONG*);
        Check(Method<Break>(white.value,0x28)(white.value,text.value,320,&skipped,missing,&consumed));
        Require(consumed > 0 && skipped >= 0 && static_cast<size_t>(consumed+skipped) <= reference.size(), "092 native 320-pixel line break ABI");
        referenceWidth = (std::max)(referenceWidth, Width(white.value, reference.substr(0,consumed)) + 20);
        reference.erase(0, consumed+skipped);
        ++referenceLines;
    }
    std::printf("EVIDENCE: 092 English cube text uses %d line(s); compatible PCOM reference panel %dx%d; Chinese panel 236x231\n",
        referenceLines, referenceWidth, 104 + (6+referenceLines)*16 + 15);
    inventory.clear();
    for (int count = 0; count <= 4; ++count) {
        if (count == 1) inventory[-1] = &helmet;
        if (count == 2) inventory[-5] = &suit;
        if (count == 3) inventory[-7] = &shoes;
        if (count == 4) inventory[-8] = &gloves;
        Hover();
        Require(panels.size() == 1 && panels.at(owner)->equipped.count == count && inventoryReferences == 0, "live panel reflects local equipped ZRefs");
        auto* layer = static_cast<Layer*>(panels.at(owner)->frame.layer);
        Require(layer->origin == *reinterpret_cast<Layer**>(owner+0x10) && layer->left == 260 && layer->top == 0, "panel follows the native main layer origin");
        SaveBitmap(panels.at(owner)->canvas.value, outputPath + L"/content-" + std::to_wstring(count) + L".bmp");
        const int frames = frameCount;
        for (int repeat = 0; repeat < 20; ++repeat) Hover();
        Require(frameCount == frames && liveLayers == 2, "unchanged hover does not recreate, flicker or leak layers");
    }
    inventory.clear();
    SetItemTooltip::Refresh();
    Require(panels.at(owner)->equipped.count == 0 && liveLayers == 2, "packet callback refreshes stationary tooltip without leaking");
    const int before = frameCount;
    std::thread worker([] { SetItemTooltip::Refresh(); });
    worker.join();
    Require(frameCount == before, "no PCOM work on a foreign packet thread");
    for (size_t index = 0; index < _countof(expectedCompare.values); ++index) expectedCompare.values[index] = 0x9000 + static_cast<DWORD>(index);
    expectedCompare.values[0] = 540;
    expectedCompare.values[1] = 400;
    for (bool vertical : {false, true}) {
        expectedCompare.values[4] = vertical ? 1 : 0;
        reinterpret_cast<CompareBuilder>(Native(0x008EBA5B))(owner, expectedCompare);
        auto* main = *reinterpret_cast<Layer**>(owner + 0x10);
        auto* first = main->origin;
        auto* panel = static_cast<Layer*>(panels.at(owner)->frame.layer);
        Require(first && panel->origin == main && panels.at(owner)->comparisonLayer.value == first
            && liveLayers == 3 && !comparisonScope && comparisonDepth == 0, "two native equip layers retain their origin graph and release the capture scope");
        Require(first->left == 304 && first->top == (vertical ? 420 : 240)
            && main->left == (vertical ? 0 : -260) && main->top == (vertical ? -360 : 0)
            && panel->left == (vertical ? 260 : 520) && panel->top == 0, "comparison post-layout uses absolute root and relative child coordinates");
        const int stableFrames = frameCount;
        const int stableMoves = layerMoves;
        comparisonCached = true;
        for (int repeat = 0; repeat < 20; ++repeat) reinterpret_cast<CompareBuilder>(Native(0x008EBA5B))(owner, expectedCompare);
        comparisonCached = false;
        Require(frameCount == stableFrames && liveLayers == 3 && layerMoves == stableMoves, "native cached comparison neither redraws nor moves any layer");
        inventory[-1] = &helmet;
        SetItemTooltip::Refresh();
        panel = static_cast<Layer*>(panels.at(owner)->frame.layer);
        Require(panels.at(owner)->equipped.count == 1 && panels.at(owner)->comparisonLayer.value == first && panel->origin == main
            && first->left == 304 && first->top == (vertical ? 420 : 240)
            && main->left == (vertical ? 0 : -260) && main->top == (vertical ? -360 : 0)
            && panel->left == (vertical ? 260 : 520) && panel->top == 0 && liveLayers == 3,
            "stationary comparison rebuild preserves metadata and never drifts relative coordinates");
        ClearOwner();
        inventory.clear();
        Require(panels.empty() && liveLayers == 0, "comparison root, main and set layers all release on native clear");
    }
    Require(compareCalls == 42, "both comparison branches and cached paths exercised");
    Client::m_nGameWidth = 1280;
    Client::m_nGameHeight = 720;
    Move(root,-640,-360);
    expectedCompare.values[0] = 1080;
    expectedCompare.values[1] = 400;
    for (bool vertical : {false, true}) {
        expectedCompare.values[4] = vertical ? 1 : 0;
        reinterpret_cast<CompareBuilder>(Native(0x008EBA5B))(owner,expectedCompare);
        auto& current = *panels.at(owner);
        const Point expectedMain{vertical ? 784 : 524, vertical ? 180 : 360};
        Require(ScreenPoint(current.mainLayer,ScreenOrigin()) == expectedMain
            && ScreenPoint(current.frame.layer,ScreenOrigin()) == Point{1044,expectedMain.y},
            "right-side high-resolution comparison stays near the hovered inventory and uses the native final layout");
        const int stableMoves = layerMoves;
        comparisonCached = true;
        for (int repeat = 0; repeat < 100; ++repeat) reinterpret_cast<CompareBuilder>(Native(0x008EBA5B))(owner,expectedCompare);
        comparisonCached = false;
        Require(layerMoves == stableMoves, "high-resolution cached comparison does not fight the native positioning path");
        Move(current.comparisonLayer.value, 300, 400);
        SetItemTooltip::Refresh();
        const Point relocated = ScreenPoint(current.mainLayer,ScreenOrigin());
        const int relocatedMoves = layerMoves;
        for (int repeat = 0; repeat < 100; ++repeat) SetItemTooltip::Refresh();
        Require(layerMoves == relocatedMoves && ScreenPoint(current.mainLayer,ScreenOrigin()) == relocated,
            "comparison root relocation is observed once, never repeatedly accumulated");
        ClearOwner();
    }
    Client::m_nGameWidth = 800;
    Client::m_nGameHeight = 600;
    Move(root,-400,-300);
    Hover();
    auto* oversized = *reinterpret_cast<Layer**>(owner + 0x10);
    oversized->width = 700;
    Client::m_nGameHeight = 200;
    SetItemTooltip::Refresh();
    auto* oversizedPanel = static_cast<Layer*>(panels.at(owner)->frame.layer);
    Require(oversized->left == 0 && oversized->top == 0 && oversizedPanel->left == 700 && oversizedPanel->top == 0
        && liveLayers == 2, "oversized main plus set remains visible and clamps to zero on both axes");
    Client::m_nGameHeight = 600;
    ClearOwner();
    Hover(&basic);
    Require(panels.empty() && liveLayers == 1, "ordinary item hides secondary panel without hiding main");
    Hover();
    Require(panels.size() == 1, "supported hover restores secondary panel");
    ClearOwner();
    Require(panels.empty() && liveLayers == 0 && !*reinterpret_cast<void**>(owner+0x10), "actual native ClearToolTip + detour frees both layers");
    Hover();
    Set(context, 0x20b8, static_cast<void*>(otherCharacter));
    inventory.clear();
    SetItemTooltip::Refresh();
    Require(panels.empty(), "old-character panel is removed on identity change");
    ClearOwner();
    Set(context, 0x20b8, static_cast<void*>(character));
    Require(liveLayers == 0 && inventoryReferences == 0, "no retained per-hover native references");
    std::puts("PASS: real PCOM fonts/canvas, 18/6-word hook forwarding, native ClearToolTip, repeated hover, refresh, comparison, foreign thread and balanced references");
}
void RunNative(const wchar_t* client, const wchar_t* fixture) {
    Require(SetCurrentDirectoryW(client) != FALSE, "use installed PCOM directory read-only");
    Check(CoInitialize(nullptr));
    auto module = LoadLibraryW(L"PCOM.dll");
    Require(module != nullptr, "load PCOM without executing BeiDou.exe");
    Check(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(module,"PcInitModule"))());
    pcomFactory = reinterpret_cast<DreamCanvas::Factory>(GetProcAddress(module,"PcCreateObject"));
    *reinterpret_cast<DreamCanvas::Factory*>(Native(0x00BF0CC0)) = pcomFactory;
    VectorTests();
    Object viewport(new Layer(0,0));
    uiOrigin = viewport.value;
    Set(windowManager, 0xdc, uiOrigin);
    *reinterpret_cast<void**>(Native(0x00BEC20C)) = windowManager;
    {
        const GUID managerId = {0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
        const GUID namespaceId = {0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
        const GUID filesystemId = {0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
        auto manager = NewObject(L"ResMan", managerId);
        auto root = NewObject(L"NameSpace", namespaceId);
        auto filesystem = NewObject(L"NameSpace#FileSystem", filesystemId);
        Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(manager.value,0x14)(manager.value,0x11,-1,-1));
        Check(reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(module,"PcRootNameSpace"))(&root.value,1));
        Text path(fixture), slash(L"/");
        Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(filesystem.value,0x34)(filesystem.value,path.value));
        Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(root.value,0x18)(root.value,slash.value,filesystem.value,0));
        *reinterpret_cast<void**>(Native(0x00BF14E8)) = manager.value;
        ModelTests();
        LifecycleTests();
        Require(static_cast<Layer*>(uiOrigin)->refs == 1, "native UI-origin getter and all nested parent references balance after every tooltip clears");
        *reinterpret_cast<void**>(Native(0x00BF14E8)) = nullptr;
    }
    reinterpret_cast<void(__cdecl*)()>(GetProcAddress(module,"PcTermModule"))();
    CoUninitialize();
}
}

void ClientLog::Append(Component, const char*, ...) { ++logCount; }

int wmain(int argc, wchar_t** argv) {
    Require(argc == 5, "EXE, client PCOM directory, scratch fixture and output required");
    char filename[MAX_PATH]{};
    WideCharToMultiByte(CP_ACP, 0, argv[1], -1, filename, sizeof(filename), nullptr, nullptr);
    auto image = TestSupport::LoadImage(filename);
    outputPath = argv[4];
    layerMethods[0] = reinterpret_cast<void*>(QueryLayer);
    layerMethods[1] = reinterpret_cast<void*>(AddRef);
    layerMethods[2] = reinterpret_cast<void*>(Release);
    layerMethods[0x20/4] = reinterpret_cast<void*>(Left);
    layerMethods[0x28/4] = reinterpret_cast<void*>(Top);
    layerMethods[0xb8/4] = reinterpret_cast<void*>(LayerWidth);
    layerMethods[0xc0/4] = reinterpret_cast<void*>(LayerHeight);
    layerMethods[0x64/4] = reinterpret_cast<void*>(Origin);
    layerMethods[0x60/4] = reinterpret_cast<void*>(GetOrigin);
    layerMethods[0x90/4] = reinterpret_cast<void*>(RelMove);
    InstallTests(image);
    try { RunNative(argv[2], argv[3]); }
    catch (HRESULT result) { std::fprintf(stderr, "FAIL HRESULT=%08lx\n", result); return 1; }
    std::puts("PASS: no client/server launch, no network, no write to client resources");
    return 0;
}
