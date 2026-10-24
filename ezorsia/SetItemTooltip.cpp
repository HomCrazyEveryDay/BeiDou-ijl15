#include "stdafx.h"
#include "SetItemTooltip.h"
#include "ClientLog.h"
#include "DreamCanvas.h"
#include "ResourceVariant.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
using DreamCanvas::Method;
#ifdef SET_ITEM_TOOLTIP_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
constexpr int supportedSet = 5;
constexpr int panelWidth = 236;
constexpr int lineHeight = 16;
constexpr unsigned titleColor = 0xff7fff00;
constexpr unsigned activeColor = 0xffffffff;
constexpr unsigned inactiveColor = 0xff7b8c91;
constexpr unsigned backgroundColor = 0xa0000040;
const GUID vectorId = {0xf28bd1ed,0x3deb,0x4f92,{0x9e,0xec,0x10,0xef,0x5a,0x1c,0x3f,0xb4}};
const wchar_t* statKeys[] = {L"incPDD", L"incMDD", L"incACC", L"incEVA"};
const unsigned statLabels[] = {659, 660, 661, 662};
bool installed = false;
LONG failureLogged = 0, resourceLogged = 0;
DWORD uiThread = 0;
unsigned comparisonDepth = 0;
auto protectMemory = &VirtualProtect;

void Check(HRESULT result) { if (FAILED(result)) throw result; }

struct Object {
    void* value = nullptr;
    Object() = default;
    explicit Object(void* object) : value(object) {}
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&& other) noexcept : value(other.value) { other.value = nullptr; }
    ~Object() { DreamCanvas::Release(value); }
};
struct ComparisonScope;
ComparisonScope* comparisonScope = nullptr;
struct ComparisonScope {
    ComparisonScope* previous;
    void* owner;
    unsigned builds = 0;
    Object firstLayer;
    explicit ComparisonScope(void* ownerValue) : previous(comparisonScope), owner(ownerValue) {
        comparisonScope = this;
    }
    ~ComparisonScope() { comparisonScope = previous; }
};
struct Value {
    VARIANT value{};
    ~Value() { ResourceVariant::Clear(&value); }
};
struct Text {
    BSTR value;
    explicit Text(const std::wstring& text) : value(SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {
        if (!value) throw E_OUTOFMEMORY;
    }
    ~Text() { SysFreeString(value); }
};
struct Member { int item; int category; };
struct Effect { int threshold; std::array<int, 4> values; int setKey = 0; };
struct Definition {
    int id = 0, completeCount = 0;
    std::wstring name;
    std::vector<Member> members;
    std::map<int, std::wstring> categories;
    std::vector<Effect> effects;
};
struct Equipped {
    int count = 0;
    unsigned categories = 0;
    bool operator==(const Equipped& other) const { return count == other.count && categories == other.categories; }
};
struct Line {
    std::wstring left, right;
    unsigned color;
    int top;
    bool centered = false;
};
struct Layout {
    int width = panelWidth, height = 0, divider = 0;
    std::vector<Line> lines;
};
struct Position { int mainX, mainY, panelX, panelY; };
struct Point {
    int x = 0, y = 0;
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Point& other) const { return !(*this == other); }
};
struct Bounds { Point position; int width, height; };
struct GroupPosition { Point main, comparison, panel; };
struct Placement {
    bool initialized = false;
    Point nativeMain, nativeComparison, appliedMain, appliedComparison;
};
struct Frame {
    DWORD unused = 0;
    int kind = 1, height = 0, width = panelWidth;
    void* layer = nullptr;
    int hoverX = 0, hoverY = 0;
    ~Frame() { DreamCanvas::Release(layer); }
};
static_assert(offsetof(Frame, layer) == 0x10 && sizeof(Frame) == 0x1c, "083 tooltip frame header");
struct Panel {
    Frame frame;
    Object canvas, titleFont, activeFont, inactiveFont, comparisonLayer;
    void* mainLayer = nullptr;
    void* character = nullptr;
    int item = 0;
    Placement placement;
    Equipped equipped;
};
Definition definition;
bool definitionAttempted = false;
std::map<int, bool> verifiedMembers;
std::map<void*, std::unique_ptr<Panel>> panels;

int Category(int item) {
    switch (item / 10000) {
    case 100: return 1;
    case 105: return 5;
    case 107: return 7;
    case 108: return 8;
    default: return 0;
    }
}
const wchar_t* Folder(int category) {
    switch (category) {
    case 1: return L"Cap";
    case 5: return L"Longcoat";
    case 7: return L"Shoes";
    case 8: return L"Glove";
    default: return nullptr;
    }
}
unsigned CategoryLabel(int category) {
    switch (category) {
    case 1: return 617;
    case 5: return 621;
    case 7: return 624;
    case 8: return 625;
    default: throw E_INVALIDARG;
    }
}
int Number(const VARIANT& value) {
    if (value.vt == VT_I4) return value.lVal;
    if (value.vt == VT_I2) return value.iVal;
    throw E_INVALIDARG;
}
std::wstring String(const VARIANT& value) {
    if (value.vt != VT_BSTR || !value.bstrVal || !SysStringLen(value.bstrVal)
        || SysStringLen(value.bstrVal) > 96) throw E_INVALIDARG;
    return std::wstring(value.bstrVal, SysStringLen(value.bstrVal));
}
void Resource(const wchar_t* path, VARIANT* result) {
    auto manager = *reinterpret_cast<void**>(Native(0x00BF14E8));
    if (!manager) throw E_POINTER;
    Text name(path);
    VARIANT missing{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    using Get = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
    Check(Method<Get>(manager, 0x1c)(manager, name.value, missing, missing, result));
}
Object Property(const VARIANT& value) {
    auto unknown = value.vt == VT_UNKNOWN ? value.punkVal : value.vt == VT_DISPATCH ? value.pdispVal : nullptr;
    if (!unknown) throw E_NOINTERFACE;
    static const GUID iid = {0x986515d9,0x0a0b,0x4929,{0x8b,0x4f,0x71,0x86,0x82,0x17,0x7b,0x92}};
    Object result;
    Check(unknown->QueryInterface(iid, &result.value));
    if (!result.value) throw E_POINTER;
    return result;
}
void Child(void* parent, const std::wstring& key, VARIANT* result) {
    Text name(key);
    using Get = HRESULT(__stdcall*)(void*, BSTR, VARIANT*);
    Check(Method<Get>(parent, 0x14)(parent, name.value, result));
}
Object ChildProperty(void* parent, const wchar_t* key) {
    Value child;
    Child(parent, key, &child.value);
    return Property(child.value);
}
std::vector<std::wstring> Names(void* property) {
    Object unknown, enumerator;
    using Get = HRESULT(__stdcall*)(void*, void**);
    Check(Method<Get>(property, 0x1c)(property, &unknown.value));
    if (!unknown.value) throw E_POINTER;
    Check(static_cast<IUnknown*>(unknown.value)->QueryInterface(IID_IEnumVARIANT, &enumerator.value));
    auto native = static_cast<IEnumVARIANT*>(enumerator.value);
    if (!native) throw E_POINTER;
    std::vector<std::wstring> result;
    for (unsigned index = 0; index < 128; ++index) {
        Value key;
        ULONG fetched = 0;
        Check(native->Next(1, &key.value, &fetched));
        if (!fetched) return result;
        result.push_back(String(key.value));
    }
    throw E_INVALIDARG;
}
int Index(const std::wstring& text) {
    if (text.empty() || text.size() > 3) throw E_INVALIDARG;
    int result = 0;
    for (const auto digit : text) {
        if (digit < L'0' || digit > L'9') throw E_INVALIDARG;
        result = result * 10 + digit - L'0';
    }
    return result;
}
bool Valid(const Definition& candidate) {
    if (candidate.id != supportedSet || candidate.completeCount != 4 || candidate.name.empty()
        || candidate.categories.size() != 4 || candidate.members.empty() || candidate.members.size() > 64
        || candidate.effects.empty() || candidate.effects.size() > 4) return false;
    std::vector<int> seen;
    for (const auto& member : candidate.members) {
        if (!member.category || !candidate.categories.count(member.category)
            || std::find(seen.begin(), seen.end(), member.item) != seen.end()) return false;
        seen.push_back(member.item);
    }
    for (const auto& category : candidate.categories)
        if (!Folder(category.first) || category.second.empty()) return false;
    for (const auto& effect : candidate.effects) {
        if (effect.threshold < 1 || effect.threshold > candidate.completeCount) return false;
        if (effect.setKey != 0 && (effect.setKey != 10000 || effect.threshold != 4)) return false;
        for (const auto value : effect.values) if (value < 0 || value > 32767) return false;
    }
    return true;
}
Definition LoadDefinition() {
    Value resource;
    Resource(L"Etc/SetItemInfo.img/5", &resource.value);
    auto property = Property(resource.value);
    Definition result;
    result.id = supportedSet;
    Value name, count;
    Child(property.value, L"setItemName", &name.value);
    Child(property.value, L"completeCount", &count.value);
    result.name = String(name.value);
    result.completeCount = Number(count.value);
    auto items = ChildProperty(property.value, L"ItemID");
    for (const auto& key : Names(items.value)) {
        Value item;
        Child(items.value, key, &item.value);
        const int id = Number(item.value);
        result.members.push_back({id, Category(id)});
    }
    auto categories = ChildProperty(property.value, L"categoryName");
    for (const auto& key : Names(categories.value)) {
        Value category;
        Child(categories.value, key, &category.value);
        if (!result.categories.emplace(Index(key), String(category.value)).second) throw E_INVALIDARG;
    }
    auto effects = ChildProperty(property.value, L"Effect");
    for (const auto& key : Names(effects.value)) {
        auto properties = ChildProperty(effects.value, key.c_str());
        Effect effect{Index(key), {}};
        for (const auto& field : Names(properties.value)) {
            Value value;
            Child(properties.value, field, &value.value);
            if (field == L"setKey") { effect.setKey = Number(value.value); continue; }
            auto found = std::find(std::begin(statKeys), std::end(statKeys), field);
            if (found == std::end(statKeys)) {
                if (Number(value.value)) throw E_INVALIDARG;
            } else effect.values[found - std::begin(statKeys)] = Number(value.value);
        }
        result.effects.push_back(effect);
    }
    std::sort(result.effects.begin(), result.effects.end(), [](const Effect& left, const Effect& right) {
        return left.threshold < right.threshold;
    });
    if (!Valid(result)) throw E_INVALIDARG;
    return result;
}
bool IsMember(const Definition& set, int item) {
    return std::any_of(set.members.begin(), set.members.end(), [item](const Member& member) { return member.item == item; });
}
bool EnabledItem(int item) {
    if (!definitionAttempted) {
        if (!*reinterpret_cast<void**>(Native(0x00BF14E8))) return false;
        definitionAttempted = true;
        definition = LoadDefinition();
    }
    if (definition.id != supportedSet || !IsMember(definition, item)) return false;
    const auto found = verifiedMembers.find(item);
    if (found != verifiedMembers.end()) return found->second;
    wchar_t path[128]{};
    swprintf_s(path, L"Character/%s/%08d.img/info/setItemID", Folder(Category(item)), item);
    Value value;
    Resource(path, &value.value);
    const bool enabled = (value.value.vt == VT_I4 || value.value.vt == VT_I2) && Number(value.value) == supportedSet;
    verifiedMembers.emplace(item, enabled);
    return enabled;
}
Equipped Count(const Definition& set, const std::vector<int>& items) {
    Equipped result;
    for (const auto item : items) {
        auto found = std::find_if(set.members.begin(), set.members.end(), [item](const Member& member) { return member.item == item; });
        if (found != set.members.end()) {
            ++result.count;
            result.categories |= 1u << found->category;
        }
    }
    return result;
}
void* Character() {
    auto context = *reinterpret_cast<BYTE**>(Native(0x00BE7918));
    return context ? *reinterpret_cast<void**>(context + 0x20b8) : nullptr;
}
int ItemId(void* item) {
    using Read = int(__thiscall*)(const void*);
    return item ? reinterpret_cast<Read>(Native(0x0042873D))(static_cast<BYTE*>(item) + 0xc) : 0;
}
Equipped ReadEquipped(void* character) {
    using Get = void*(__thiscall*)(void*, void*, int, int);
    using Release = void(__thiscall*)(void*);
    struct Slot {
        DWORD unused = 0;
        void* item = nullptr;
        ~Slot() { if (item) reinterpret_cast<Release>(Native(0x004288C1))(this); }
    };
    std::vector<int> items;
    const auto read = [&](int position) {
        Slot slot;
        reinterpret_cast<Get>(Native(0x004282F7))(character, &slot, 1, position);
        if (slot.item) items.push_back(ItemId(slot.item));
    };
    for (int slot = 1; slot <= 51; ++slot) { read(-slot); read(-slot - 100); }
    for (int slot = 1000; slot <= 1003; ++slot) read(-slot);
    return Count(definition, items);
}
std::wstring Pool(unsigned id) {
    struct NativeString {
        char* value = nullptr;
        ~NativeString() { reinterpret_cast<void(__thiscall*)(void*)>(Native(0x004062DF))(this); }
    } text;
    auto pool = reinterpret_cast<void*(__cdecl*)()>(Native(0x0079E805))();
    using Get = void*(__thiscall*)(void*, void*, unsigned, char);
    reinterpret_cast<Get>(Native(0x0079E993))(pool, &text, id, 0);
    if (!text.value) throw E_INVALIDARG;
    const int size = MultiByteToWideChar(936, 0, text.value, -1, nullptr, 0);
    if (size < 2 || size > 256) throw E_INVALIDARG;
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(936, 0, text.value, -1, &result[0], size)) throw E_INVALIDARG;
    result.pop_back();
    return result;
}
using LabelReader = std::wstring(*)(unsigned);
Layout Compose(const Definition& set, Equipped equipped, LabelReader label) {
    Layout result;
    result.lines.push_back({set.name, L"", titleColor, 8, true});
    int top = 35;
    for (const auto& category : set.categories) {
        result.lines.push_back({category.second, L"(" + label(CategoryLabel(category.first)) + L")",
            equipped.categories & (1u << category.first) ? activeColor : inactiveColor, top});
        top += lineHeight;
    }
    result.divider = static_cast<int>(set.categories.size()) * lineHeight + 40;
    top = result.divider + 9;
    for (const auto& effect : set.effects) {
        result.lines.push_back({std::to_wstring(effect.threshold) + L"\u4ef6\u5957\u88c5\u6548\u679c :", L"", titleColor, top});
        top += lineHeight;
        for (size_t index = 0; index < effect.values.size(); ++index) {
            if (!effect.values[index]) continue;
            result.lines.push_back({label(statLabels[index]) + L" +" + std::to_wstring(effect.values[index]), L"",
                equipped.count >= effect.threshold ? activeColor : inactiveColor, top});
            top += lineHeight;
        }
        if (effect.setKey == 10000) {
            result.lines.push_back({L"\u4e0d\u4f1a\u53d7\u5230\u7acb\u65b9\u4f53\u5185\u5947\u5f02\u529b\u91cf\u7684\u4f24\u5bb3\u3002", L"",
                equipped.count >= effect.threshold ? activeColor : inactiveColor, top});
            top += lineHeight;
        }
    }
    result.height = top + 6;
    return result;
}
Position Place(int left, int top, int mainWidth, int mainHeight, int width, int height, int screenWidth, int screenHeight) {
    const int combinedWidth = mainWidth + width;
    const int combinedHeight = (std::max)(mainHeight, height);
    if (left + combinedWidth > screenWidth) left = screenWidth - combinedWidth;
    if (left < 0) left = 0;
    if (top + combinedHeight > screenHeight) top = screenHeight - combinedHeight;
    if (top < 0) top = 0;
    return {left, top, left + mainWidth, top};
}
GroupPosition PlaceGroup(Bounds main, const Bounds* comparison, int width, int height, int screenWidth, int screenHeight) {
    int left = main.position.x, top = main.position.y;
    int panelLeft = left + main.width;
    int bottom = top + (std::max)(main.height, height);
    if (comparison) {
        left = (std::min)(left, comparison->position.x);
        top = (std::min)(top, comparison->position.y);
        panelLeft = (std::max)(panelLeft, comparison->position.x + comparison->width);
        bottom = (std::max)(bottom, comparison->position.y + comparison->height);
    }
    const auto fitted = Place(left, top, panelLeft - left, bottom - top, width, 0, screenWidth, screenHeight);
    const int offsetX = fitted.mainX - left, offsetY = fitted.mainY - top;
    return {{main.position.x + offsetX, main.position.y + offsetY},
        {comparison ? comparison->position.x + offsetX : 0, comparison ? comparison->position.y + offsetY : 0},
        {panelLeft + offsetX, main.position.y + offsetY}};
}
int Metric(void* object, unsigned offset) {
    LONG value = 0;
    Check(Method<HRESULT(__stdcall*)(void*, LONG*)>(object, offset)(object, &value));
    return value;
}
int Width(void* font, const std::wstring& text) {
    Text value(text);
    VARIANT missing{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    LONG width = 0;
    using Measure = HRESULT(__stdcall*)(void*, BSTR, VARIANT, LONG*);
    Check(Method<Measure>(font, 0x1c)(font, value.value, missing, &width));
    return width;
}
void DrawText(void* canvas, void* font, int left, int top, const std::wstring& text) {
    Text value(text);
    VARIANT missing{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    LONG result = 0;
    using Draw = HRESULT(__stdcall*)(void*, int, int, BSTR, void*, VARIANT, VARIANT, LONG*);
    Check(Method<Draw>(canvas, 0x98)(canvas, left, top, value.value, font, missing, missing, &result));
}
void Move(void* layer, int left, int top) {
    VARIANT missing{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    using RelMove = HRESULT(__stdcall*)(void*, int, int, VARIANT, VARIANT);
    Check(Method<RelMove>(layer, 0x90)(layer, left, top, missing, missing));
}
Point AbsolutePoint(void* layer) { return {Metric(layer, 0x20), Metric(layer, 0x28)}; }
Point ScreenOrigin() {
    auto manager = *reinterpret_cast<void**>(Native(0x00BEC20C));
    if (!manager) throw E_POINTER;
    Object origin;
    using Get = void*(__thiscall*)(void*, void**);
    reinterpret_cast<Get>(Native(0x0048BBA5))(manager, &origin.value);
    if (!origin.value) throw E_POINTER;
    return AbsolutePoint(origin.value);
}
Point ScreenPoint(void* layer, Point screenOrigin) {
    const auto absolute = AbsolutePoint(layer);
    return {absolute.x - screenOrigin.x, absolute.y - screenOrigin.y};
}
void MoveScreen(void* layer, Point position, Point screenOrigin) {
    const Point absolute{position.x + screenOrigin.x, position.y + screenOrigin.y};
    if (AbsolutePoint(layer) == absolute) return;
    Value origin;
    Check(Method<HRESULT(__stdcall*)(void*, VARIANT*)>(layer, 0x60)(layer, &origin.value));
    Point parent;
    if (origin.value.vt != VT_EMPTY && origin.value.vt != VT_NULL) {
        auto unknown = origin.value.vt == VT_UNKNOWN ? origin.value.punkVal
            : origin.value.vt == VT_DISPATCH ? origin.value.pdispVal : nullptr;
        if (!unknown) throw E_NOINTERFACE;
        Object vector;
        Check(unknown->QueryInterface(vectorId, &vector.value));
        if (!vector.value) throw E_POINTER;
        parent = AbsolutePoint(vector.value);
    }
    Move(layer, absolute.x - parent.x, absolute.y - parent.y);
}
void SetOrigin(void* layer, void* origin) {
    VARIANT value{};
    value.vt = VT_UNKNOWN;
    value.punkVal = static_cast<IUnknown*>(origin);
    Check(Method<HRESULT(__stdcall*)(void*, VARIANT)>(layer, 0x64)(layer, value));
}
void Borrow(void* owner, unsigned offset, Object& result) {
    result.value = *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + offset);
    if (!result.value) throw E_POINTER;
    static_cast<IUnknown*>(result.value)->AddRef();
}
void CreateTitleFont(Object& font) {
    auto factory = *reinterpret_cast<DreamCanvas::Factory*>(Native(0x00BF0CC0));
    if (!factory) throw E_POINTER;
    static const GUID iid = {0x2bef046d,0xccd6,0x445a,{0x88,0xc4,0x92,0x9f,0xc3,0x5d,0x30,0xac}};
    Check(factory(L"Canvas#Font", &iid, &font.value, nullptr));
    if (!font.value) throw E_POINTER;
    Text face(Pool(5527));
    VARIANT missing{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    using Create = HRESULT(__stdcall*)(void*, BSTR, int, unsigned, VARIANT);
    Check(Method<Create>(font.value, 0xc)(font.value, face.value, 12, titleColor, missing));
}
void PositionPanel(Panel& panel) {
    const int mainWidth = Metric(panel.mainLayer, 0xb8);
    const int mainHeight = Metric(panel.mainLayer, 0xc0);
    if (mainWidth <= 0 || mainWidth > 4096 || mainHeight <= 0 || mainHeight > 8192) throw E_INVALIDARG;
    const auto screenOrigin = ScreenOrigin();
    const auto currentMain = ScreenPoint(panel.mainLayer, screenOrigin);
    Bounds comparison{};
    if (panel.comparisonLayer.value) {
        comparison = {ScreenPoint(panel.comparisonLayer.value, screenOrigin), Metric(panel.comparisonLayer.value, 0xb8),
            Metric(panel.comparisonLayer.value, 0xc0)};
        if (comparison.width <= 0 || comparison.width > 4096 || comparison.height <= 0 || comparison.height > 8192) throw E_INVALIDARG;
    }
    auto& placement = panel.placement;
    if (!placement.initialized || currentMain != placement.appliedMain
        || (panel.comparisonLayer.value && comparison.position != placement.appliedComparison)) {
        placement.nativeMain = currentMain;
        placement.nativeComparison = comparison.position;
    }
    comparison.position = placement.nativeComparison;
    const auto position = PlaceGroup({placement.nativeMain, mainWidth, mainHeight}, panel.comparisonLayer.value ? &comparison : nullptr,
        panel.frame.width, panel.frame.height,
        Client::m_nGameWidth, Client::m_nGameHeight);
    if (panel.comparisonLayer.value) MoveScreen(panel.comparisonLayer.value, position.comparison, screenOrigin);
    MoveScreen(panel.mainLayer, position.main, screenOrigin);
    MoveScreen(panel.frame.layer, position.panel, screenOrigin);
    placement.initialized = true;
    placement.appliedMain = position.main;
    placement.appliedComparison = position.comparison;
}
std::unique_ptr<Panel> CreatePanel(void* owner, int item, void* character, Equipped equipped, const Panel* previous) {
    auto panel = std::make_unique<Panel>();
    auto layout = Compose(definition, equipped, Pool);
    Borrow(owner, 0x440, panel->activeFont);
    Borrow(owner, 0x448, panel->inactiveFont);
    CreateTitleFont(panel->titleFont);
    for (const auto& line : layout.lines) {
        void* font = line.color == titleColor ? panel->titleFont.value : panel->activeFont.value;
        layout.width = (std::max)(layout.width, Width(font, line.left) + (line.right.empty() ? 20 : Width(font, line.right) + 30));
    }
    if (layout.width > 512 || layout.height > 512) throw E_INVALIDARG;
    panel->frame.width = layout.width;
    panel->frame.height = layout.height;
    using CreateFrame = void*(__thiscall*)(void*, void**, int, int, int, int, int, unsigned);
    reinterpret_cast<CreateFrame>(Native(0x008F3141))(&panel->frame, &panel->canvas.value, 0, 0, 1, 0, 0, backgroundColor);
    if (!panel->frame.layer || !panel->canvas.value) throw E_POINTER;
    using Rectangle = HRESULT(__stdcall*)(void*, int, int, int, int, unsigned);
    Check(Method<Rectangle>(panel->canvas.value, 0x8c)(panel->canvas.value, 6, layout.divider, layout.width - 12, 1, 0xffffffff));
    for (const auto& line : layout.lines) {
        void* font = line.color == titleColor ? panel->titleFont.value
            : line.color == activeColor ? panel->activeFont.value : panel->inactiveFont.value;
        DrawText(panel->canvas.value, font, line.centered ? (layout.width - Width(font, line.left)) / 2 : 10, line.top, line.left);
        if (!line.right.empty()) DrawText(panel->canvas.value, font, layout.width - 10 - Width(font, line.right), line.top, line.right);
    }
    panel->mainLayer = *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + 0x10);
    panel->character = character;
    panel->item = item;
    panel->equipped = equipped;
    if (comparisonScope && comparisonScope->owner == owner && comparisonScope->builds == 2
        && comparisonScope->firstLayer.value != panel->mainLayer) {
        panel->comparisonLayer.value = comparisonScope->firstLayer.value;
    } else if (!comparisonDepth && previous && previous->mainLayer == panel->mainLayer) {
        panel->comparisonLayer.value = previous->comparisonLayer.value;
        panel->placement = previous->placement;
    }
    if (panel->comparisonLayer.value) static_cast<IUnknown*>(panel->comparisonLayer.value)->AddRef();
    SetOrigin(panel->frame.layer, panel->mainLayer);
    if (!comparisonDepth) PositionPanel(*panel);
    return panel;
}
void Sync(void* owner, int item) {
    auto character = Character();
    auto mainLayer = owner ? *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + 0x10) : nullptr;
    if (!character || !mainLayer || !EnabledItem(item)) { panels.erase(owner); return; }
    const auto found = panels.find(owner);
    if (found != panels.end() && found->second->character != character) { panels.erase(found); return; }
    const auto equipped = ReadEquipped(character);
    if (found != panels.end() && found->second->mainLayer == mainLayer && found->second->character == character
        && found->second->item == item && found->second->equipped == equipped) {
        if (!comparisonDepth) PositionPanel(*found->second);
        return;
    }
    if (panels.size() >= 8 && found == panels.end()) return;
    auto panel = CreatePanel(owner, item, character, equipped, found == panels.end() ? nullptr : found->second.get());
    panels[owner] = std::move(panel);
}
void SafeSync(void* owner, int item) {
    const DWORD error = GetLastError();
    try { Sync(owner, item); }
    catch (...) {
        panels.erase(owner);
        if (InterlockedCompareExchange(&resourceLogged, 1, 0) == 0)
            ClientLog::Append(ClientLog::Component::Lifecycle, "event=set_item_tooltip_resource_failed item=%d", item);
    }
    SetLastError(error);
}
struct EquipArguments { DWORD values[18]; };
struct CompareArguments { DWORD values[6]; };
static_assert(sizeof(EquipArguments) == 0x48 && sizeof(CompareArguments) == 0x18, "native callee stack cleanup");
using EquipBuilder = void(__thiscall*)(void*, EquipArguments);
using CompareBuilder = void(__thiscall*)(void*, CompareArguments);
using ClearTooltip = void(__thiscall*)(void*);
EquipBuilder originalEquip = nullptr;
CompareBuilder originalCompare = nullptr;
ClearTooltip originalClear = nullptr;

void __fastcall EquipHook(void* owner, void*, EquipArguments arguments) {
    originalEquip(owner, arguments);
    if (!installed) return;
    if (!uiThread) uiThread = GetCurrentThreadId();
    if (uiThread != GetCurrentThreadId()) return;
    if (comparisonScope && comparisonScope->owner == owner && comparisonScope->builds++ == 0) {
        comparisonScope->firstLayer.value = *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + 0x10);
        if (comparisonScope->firstLayer.value) static_cast<IUnknown*>(comparisonScope->firstLayer.value)->AddRef();
    }
    SafeSync(owner, ItemId(reinterpret_cast<void*>(arguments.values[2])));
}
void RunComparison(void* owner, CompareArguments arguments) {
    ++comparisonDepth;
    __try { originalCompare(owner, arguments); }
    __finally { --comparisonDepth; }
}
void __fastcall CompareHook(void* owner, void*, CompareArguments arguments) {
    ComparisonScope scope(owner);
    RunComparison(owner, arguments);
    SetItemTooltip::Refresh();
}
void __fastcall ClearHook(void* owner, void*) {
    panels.erase(owner);
    originalClear(owner);
}
struct Patch {
    DWORD address;
    const char* bytes;
    size_t size;
    void* target;
    void* trampoline = nullptr;
};
Patch patches[] = {
    {Native(0x008E8252), "\xb8\x96\x45\xad\x00", 5, reinterpret_cast<void*>(EquipHook)},
    {Native(0x008E6E23), "\x56\x8b\xf1\x57\x33\xff", 6, reinterpret_cast<void*>(ClearHook)},
    {Native(0x008EBA5B), "\xb8\x5c\x49\xad\x00", 5, reinterpret_cast<void*>(CompareHook)}
};
struct Signature { DWORD address; const char* bytes; size_t size; };
const Signature signatures[] = {
    {Native(0x0048BBA5), "\x55\x8b\xec\x51\x8b\x81\xdc\x00\x00\x00", 10},
    {Native(0x008F3141), "\xb8\x6c\x57\xad\x00\xe8\x4d\xda\x16\x00", 10},
    {Native(0x004282F7), "\x55\x8b\xec\x51\x8b\x45\x0c\x83\x65\xfc\x00", 11},
    {Native(0x0042873D), "\x55\x8b\xec\x83\xec\x10\x8b\x41\x08", 9}
};
bool Failure(const char* reason, DWORD address) {
    if (InterlockedCompareExchange(&failureLogged, 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle, "event=set_item_tooltip_install success=0 reason=%s address=%08lX", reason, address);
    return false;
}
void Jump(BYTE* site, const void* target, size_t size) {
    site[0] = 0xe9;
    const DWORD relative = reinterpret_cast<DWORD>(target) - reinterpret_cast<DWORD>(site) - 5;
    std::memcpy(site + 1, &relative, 4);
    for (size_t index = 5; index < size; ++index) site[index] = 0x90;
}
}

void SetItemTooltip::Refresh() {
    if (!installed || !uiThread || uiThread != GetCurrentThreadId() || comparisonDepth) return;
    std::vector<std::pair<void*, int>> active;
    for (const auto& entry : panels) active.emplace_back(entry.first, entry.second->item);
    for (const auto& entry : active) SafeSync(entry.first, entry.second);
}

bool SetItemTooltip::Install(HMODULE module) {
    if (reinterpret_cast<DWORD>(module) != Native(0x00400000)) return Failure("base", 0);
    if (installed) return true;
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return Failure("dos", 0);
    auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (image->Signature != IMAGE_NT_SIGNATURE || image->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || image->FileHeader.TimeDateStamp != 0x4b7c15c9 || image->OptionalHeader.SizeOfImage != 0xa94000)
        return Failure("image", 0);
    for (const auto& patch : patches)
        if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.bytes, patch.size)) return Failure("patch signature", patch.address);
    for (const auto& signature : signatures)
        if (std::memcmp(reinterpret_cast<void*>(signature.address), signature.bytes, signature.size)) return Failure("ABI signature", signature.address);
    auto* trampolines = static_cast<BYTE*>(VirtualAlloc(nullptr, 96, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampolines) return Failure("allocate", 0);
    for (size_t index = 0; index < _countof(patches); ++index) {
        auto& patch = patches[index];
        patch.trampoline = trampolines + index * 32;
        std::memcpy(patch.trampoline, patch.bytes, patch.size);
        Jump(static_cast<BYTE*>(patch.trampoline) + patch.size, reinterpret_cast<void*>(patch.address + patch.size), 5);
    }
    DWORD previous = 0;
    if (!protectMemory(trampolines, 96, PAGE_EXECUTE_READ, &previous)
        || !FlushInstructionCache(GetCurrentProcess(), trampolines, 96)) {
        VirtualFree(trampolines, 0, MEM_RELEASE);
        return Failure("trampoline protect", 0);
    }
    originalEquip = reinterpret_cast<EquipBuilder>(patches[0].trampoline);
    originalClear = reinterpret_cast<ClearTooltip>(patches[1].trampoline);
    originalCompare = reinterpret_cast<CompareBuilder>(patches[2].trampoline);
    std::array<DWORD, 3> protections{};
    size_t opened = 0;
    for (; opened < _countof(patches); ++opened) {
        const auto& patch = patches[opened];
        if (!protectMemory(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READWRITE, &protections[opened])) break;
    }
    bool success = opened == _countof(patches);
    if (success) {
        for (const auto& patch : patches) Jump(reinterpret_cast<BYTE*>(patch.address), patch.target, patch.size);
        success = FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage) != FALSE;
    }
    bool restored = true;
    for (size_t index = 0; index < opened; ++index) {
        const auto& patch = patches[index];
        DWORD ignored;
        if (!protectMemory(reinterpret_cast<void*>(patch.address), patch.size, protections[index], &ignored)) restored = false;
    }
    if (!success || !restored) {
        bool reverted = true;
        for (size_t index = 0; index < opened; ++index) {
            const auto& patch = patches[index];
            DWORD ignored;
            if (!protectMemory(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READWRITE, &ignored)) {
                reverted = false;
                continue;
            }
            std::memcpy(reinterpret_cast<void*>(patch.address), patch.bytes, patch.size);
            if (!protectMemory(reinterpret_cast<void*>(patch.address), patch.size, protections[index], &ignored)) reverted = false;
        }
        if (!FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage)) reverted = false;
        if (reverted) VirtualFree(trampolines, 0, MEM_RELEASE);
        return Failure("patch protection/cache", 0);
    }
    installed = true;
    ClientLog::Append(ClientLog::Component::Lifecycle, "event=set_item_tooltip_install success=1 protectionRestored=1 set=5");
    return true;
}
