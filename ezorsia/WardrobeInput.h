#pragma once
#include <array>

namespace WardrobeInput {
// Native keymaps share the left/right Shift slot; E0 lives outside this scan byte.
inline unsigned NormalizeScan(unsigned scan){return scan==0x36?0x2a:scan;}
// A key suppressed at a scope boundary cannot become active until it is physically released.
// This also covers keys held while opening, closing, changing focus, or leaving a text field.
class ReleaseGate {
    std::array<bool,256> blocked{};
public:
    void Hold(unsigned key) { if(key<blocked.size())blocked[key]=true; }
    bool Suppressed(unsigned key,bool down) {
        if(key>=blocked.size())return false;
        if(!down)blocked[key]=false;
        return blocked[key];
    }
    bool Read(unsigned key,bool down) {
        const bool suppressed=Suppressed(key,down);
        return key<blocked.size() && down && !suppressed;
    }
    bool Held(unsigned key)const{return key<blocked.size()&&blocked[key];}
    bool Pending()const{for(bool value:blocked)if(value)return true;return false;}
};
bool Install();
void Uninstall();
void Lock();
void Unlock();
bool Locked();
bool IsForeground();
void PreviewEnabled(bool enabled);
bool Pressed(unsigned key);
bool AcceptPreviewKey(unsigned key,unsigned flags);
bool ConsumeAfterClose(unsigned key,unsigned flags);
void UpdateFocus();
void PreviewDirection(int x,int y);
// Only the synchronous native preview controller sees private preview keys.
struct PreviewScope {
    PreviewScope();
    ~PreviewScope();
    PreviewScope(const PreviewScope&)=delete;
    PreviewScope& operator=(const PreviewScope&)=delete;
};
struct FieldScope {
    unsigned previous=0;
    FieldScope();~FieldScope();
    FieldScope(const FieldScope&)=delete;
    FieldScope& operator=(const FieldScope&)=delete;
};
}
