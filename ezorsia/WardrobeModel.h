#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace WardrobeModel {
constexpr unsigned Magic=0x5744,Version=2;
constexpr std::array<short,15> Slots={-101,-102,-103,-104,-105,-106,-107,-108,-109,-110,-111,-112,-113,-115,-116};
enum Action {Open,Sync,Store,Take,Apply,Save,Delete,Rename,Close,Heartbeat};
struct Item {
    std::uint64_t uid=0;int id=0;short position=0,slot=0;unsigned flags=0,reason=0,ringEffects=0;
    std::string name;std::vector<unsigned char> native;
};
struct Scope {short slot=0;unsigned mode=0;std::uint64_t uid=0;int itemId=0;bool valid=true;};
struct Outfit {bool defined=false;std::string name;std::vector<Scope> entries;};
struct ResolvedOutfit {
    std::map<short,std::uint64_t> choices;
    int missing=0,restricted=0,ambiguous=0,excluded=0;
    bool Blocked()const{return missing || restricted || ambiguous;}
};
struct Snapshot {
    bool busy=false;unsigned outcome=0;int token=0,revision=0,request=0,capacity=0,freeBag=0;
    std::string message,name;std::array<std::vector<Item>,3> items;std::array<Outfit,6> outfits;
};
struct Reader {
    const unsigned char* p;const unsigned char* end;bool ok=true;
    std::uint64_t number(unsigned n) {
        if(!ok || static_cast<std::size_t>(end-p)<n){ok=false;return 0;}
        std::uint64_t value=0;for(unsigned i=0;i<n;i++)value|=std::uint64_t(*p++)<<(i*8);return value;
    }
    std::string text(unsigned limit=1024) {
        auto n=number(2);if(!ok || n>limit || std::uint64_t(end-p)<n){ok=false;return {};}
        std::string s(reinterpret_cast<const char*>(p),static_cast<std::size_t>(n));p+=n;return s;
    }
};
inline bool Decode(const unsigned char* data,unsigned length,Snapshot& result) {
    if(!data || length<28 || data[4]!=0x05 || data[5]!=0x10)return false;
    Reader r{data+6,data+length};if(r.number(2)!=Magic || r.number(1)!=Version)return false;
    Snapshot s;s.busy=r.number(1)!=0;s.outcome=unsigned(r.number(1));
    s.token=int(r.number(4));s.revision=int(r.number(4));s.request=int(r.number(4));
    s.capacity=int(r.number(2));s.freeBag=int(r.number(2));s.message=r.text(512);s.name=r.text(64);
    if(s.token<=0 || s.revision<=0 || s.capacity>1024 || s.outcome>Heartbeat)return false;
    for(int source=0;source<3;source++) {
        auto count=r.number(2);if(!r.ok || count>1024)return false;
        for(unsigned n=0;n<count;n++) {
            Item item;item.uid=r.number(8);item.position=short(r.number(2));item.slot=short(r.number(2));
            item.flags=unsigned(r.number(1));item.reason=unsigned(r.number(1));
            auto bytes=r.number(2);
            if(!r.ok || bytes<14 || bytes>512 || bytes>std::uint64_t(r.end-r.p) || r.p[0]!=1)return false;
            item.native.assign(r.p,r.p+bytes);r.p+=bytes;
            Reader native{item.native.data()+1,item.native.data()+item.native.size()};item.id=int(native.number(4));
            if(item.id<1000000 || item.id>=2000000 || item.reason>7 || item.flags>15)return false;
            s.items[source].push_back(std::move(item));
        }
    }
    if(r.number(1)!=6)return false;
    for(auto& outfit:s.outfits) {
        outfit.defined=r.number(1)!=0;if(!outfit.defined)continue;
        outfit.name=r.text(64);auto count=r.number(1);if(count!=Slots.size())return false;std::set<short> used;
        for(unsigned i=0;i<count;i++) {
            Scope entry;entry.slot=short(r.number(2));entry.mode=unsigned(r.number(1));entry.uid=r.number(8);entry.itemId=int(r.number(4));entry.valid=r.number(1)!=0;
            if(entry.mode>2 || std::find(Slots.begin(),Slots.end(),entry.slot)==Slots.end() || !used.insert(entry.slot).second)return false;
            if(entry.mode==2 && (!entry.uid || !entry.itemId) || entry.mode!=2 && entry.uid)return false;
            outfit.entries.push_back(entry);
        }
    }
    if(!r.ok || r.p!=r.end)return false;result=std::move(s);return true;
}
inline void Number(std::vector<unsigned char>& bytes,std::uint64_t value,int n) {for(int i=0;i<n;i++)bytes.push_back(static_cast<unsigned char>(value>>(i*8)));}
inline void String(std::vector<unsigned char>& bytes,const std::string& text) {Number(bytes,text.size(),2);bytes.insert(bytes.end(),text.begin(),text.end());}
inline int Category(int id) {
    switch(id/10000) {case 100:return 1;case 104:case 105:case 106:return 2;case 110:return 3;case 170:return 4;
        case 107:return 5;case 108:return 6;default:return 7;}
}
struct Model {
    Snapshot current;std::map<short,std::uint64_t> draft;bool original=false;bool pantsAuto=false;
    std::uint64_t pantsBeforeOverall=0;
    const Item* Find(std::uint64_t uid)const {
        if(!uid)return nullptr;for(const auto& list:current.items)for(const auto& i:list)if(i.uid==uid)return &i;return nullptr;
    }
    const Item* OutfitItem(std::uint64_t uid)const {
        return Find(uid);
    }
    const Item* DetailItem(const Scope& entry)const {
        if(entry.mode!=2)return nullptr;
        if(auto item=Find(entry.uid))return item->id==entry.itemId?item:nullptr;
        const Item* match=nullptr;
        for(const auto& source:current.items)for(const auto& item:source)if(item.id==entry.itemId){if(match)return nullptr;match=&item;}
        return match;
    }
    const Item* Worn(short slot)const {for(const auto& i:current.items[2])if(i.position==slot)return &i;return nullptr;}
    const Item* PreviewItem(short slot)const {
        auto change=draft.find(slot);
        return original || change==draft.end()?Worn(slot):Find(change->second);
    }
    std::uint64_t Base(short slot)const {auto i=Worn(slot);return i?i->uid:0;}
    std::uint64_t Desired(short slot)const {auto i=draft.find(slot);return i==draft.end()?Base(slot):i->second;}
    bool Locked(short slot)const {auto i=Worn(slot);return i && (!i->uid || (i->flags&4));}
    int StorableCount()const {return int(std::count_if(current.items[1].begin(),current.items[1].end(),[](const Item& i){return i.uid && (i.flags&2);}));}
    void Set(short slot,std::uint64_t uid) {if(uid==Base(slot))draft.erase(slot);else draft[slot]=uid;}
    bool IsPreviewed(std::uint64_t uid)const {
        if(!uid)return false;
        for(short slot:Slots)if(Desired(slot)==uid)return true;
        return false;
    }
    bool Toggle(std::uint64_t uid) {
        const auto* item=Find(uid);if(!item || !(item->flags&1))return false;
        for(short slot:Slots)if(Desired(slot)==uid) {
            if(Locked(slot))return false;
            Set(slot,0);
            if(slot==-105 && item->id/10000==105 && pantsAuto){Set(-106,pantsBeforeOverall);pantsAuto=false;}
            original=false;return true;
        }
        return Try(uid);
    }
    bool Try(std::uint64_t uid) {
        const auto* item=Find(uid);if(!item || !(item->flags&1))return false;
        short slot=item->slot;
        if(item->id/10000==111){
            for(short candidate:{short(-112),short(-113),short(-115),short(-116)})if(!Locked(candidate)){slot=candidate;break;}
            for(short candidate:{short(-112),short(-113),short(-115),short(-116)})if(!Desired(candidate)&&!Locked(candidate)){slot=candidate;break;}
            std::vector<short> conflicts;
            for(short candidate:{short(-112),short(-113),short(-115),short(-116)}){
                auto worn=Find(Desired(candidate));
                if(worn && (worn->ringEffects & item->ringEffects)){
                    if(Locked(candidate))return false;
                    conflicts.push_back(candidate);
                }
            }
            // Replace the effect the user is looking at, before considering an
            // empty ring slot. Remove existing same-effect duplicates atomically.
            if(!conflicts.empty()){slot=conflicts.front();for(short other:conflicts)if(other!=slot)Set(other,0);}
        }
        if(Locked(slot))return false;
        for(short other:Slots)if(other!=slot && Desired(other)==uid && Locked(other))return false;
        auto top=Find(Desired(-105));bool wasOverall=top && top->id/10000==105;
        if(item->id/10000==105) {
            if(Locked(-106))return false;if(!pantsAuto)pantsBeforeOverall=Desired(-106);Set(-106,0);pantsAuto=true;
        } else if(item->id/10000==104 && wasOverall && pantsAuto) {Set(-106,pantsBeforeOverall);pantsAuto=false;}
        else if(item->id/10000==106 && wasOverall) {
            if(Locked(-105))return false;auto base=Find(Base(-105));Set(-105,base && base->id/10000!=105?base->uid:0);pantsAuto=false;
        }
        for(short other:Slots)if(other!=slot && Desired(other)==uid)Set(other,0);
        Set(slot,uid);original=false;return true;
    }
    bool Valid()const {
        std::set<std::uint64_t> used;
        for(short slot:Slots) {
            auto uid=Desired(slot);auto item=Find(uid);
            if(uid && (!item || !used.insert(uid).second))return false;
            if(draft.count(slot) && (Locked(slot) || uid && !(item->flags&1)))return false;
        }
        auto top=Find(Desired(-105));return !(top && top->id/10000==105 && Desired(-106));
    }
    // partial keeps the resolvable pieces of a blocked outfit; only for previewing, never applying.
    ResolvedOutfit Resolve(const Outfit& outfit,bool partial=false)const {
        ResolvedOutfit result;if(!outfit.defined)return result;
        std::set<short> slots;
        for(const auto& e:outfit.entries)
            if(e.mode>2 || std::find(Slots.begin(),Slots.end(),e.slot)==Slots.end() || !slots.insert(e.slot).second){++result.restricted;return result;}
        if(slots.size()!=Slots.size()){++result.restricted;return result;}
        std::map<short,const Item*> items;
        std::set<std::uint64_t> reserved;
        for(const auto& e:outfit.entries)if(e.mode==2){
            const auto* exact=OutfitItem(e.uid);
            if(exact && exact->id==e.itemId){items[e.slot]=exact;reserved.insert(exact->uid);}
        }
        for(const auto& e:outfit.entries)if(e.mode==2 && !items.count(e.slot)){
            // Older login DTOs discarded persistent instance IDs. Recover only a
            // unique same-ID instance in the locker, bag or currently worn slots.
            // Never substitute another instance when the original is still here.
            if(OutfitItem(e.uid)){++result.restricted;continue;}
            const Item* replacement=nullptr;std::set<std::uint64_t> candidates;
            for(int source:{0,1,2})for(const auto& item:current.items[source])
                if(item.uid && item.id==e.itemId){candidates.insert(item.uid);replacement=&item;}
            if(candidates.empty()){++result.missing;continue;}
            if(candidates.size()!=1){++result.ambiguous;continue;}
            if(reserved.count(replacement->uid)){++result.missing;continue;}
            items[e.slot]=replacement;reserved.insert(replacement->uid);
        }
        std::set<std::uint64_t> used;
        for(const auto& e:outfit.entries){
            // Presets are complete looks. Legacy KEEP entries mean empty for
            // editable cash slots, not inheritance from the previous outfit.
            // Only currently protected equipment is outside wardrobe control.
            if(e.mode!=2){
                const auto* worn=Worn(e.slot);
                // SaveScope excludes non-manageable worn items too (for example timed cash).
                if(Locked(e.slot) || e.mode==0 && worn && !(worn->flags&1)){++result.excluded;continue;}
                result.choices[e.slot]=0;continue;
            }
            auto found=items.find(e.slot);if(found==items.end())continue;
            const auto* item=found->second;
            bool ring=item->id/10000==111;
            bool fits=ring?(e.slot==-112 || e.slot==-113 || e.slot==-115 || e.slot==-116):item->slot==e.slot;
            bool locker=std::any_of(current.items[0].begin(),current.items[0].end(),[&](const Item& stored){return stored.uid==item->uid;});
            if(!used.insert(item->uid).second || !fits || Base(e.slot)!=item->uid && (!(item->flags&1) || Locked(e.slot) || locker && !(item->flags&2))){++result.restricted;continue;}
            result.choices[e.slot]=item->uid;
        }
        // Removed pieces must be eligible for the account wardrobe. Do not
        // silently divert bound/timed equipment into the equipment bag.
        for(const auto& e:result.choices){
            auto previous=Worn(e.first);
            if(previous && previous->uid!=e.second && !used.count(previous->uid) && !(previous->flags&2))++result.restricted;
        }
        auto desired=[&](short slot)->const Item*{auto i=result.choices.find(slot);return i==result.choices.end()?Worn(slot):OutfitItem(i->second);};
        auto top=desired(-105);
        if(top && top->id/10000==105 && desired(-106))++result.restricted;
        // Never apply half an outfit or silently label that mixture as current.
        if(result.Blocked() && !partial)result.choices.clear();
        return result;
    }
    // Undo the try-on of one slot (back to what is worn). Refuse if that breaks the look,
    // e.g. restoring worn pants under a previewed overall.
    bool Revert(short slot) {
        if(!draft.count(slot) || Locked(slot))return false;
        auto old=draft;bool oldAuto=pantsAuto;draft.erase(slot);
        if(slot==-105 && pantsAuto){auto top=Find(Desired(-105));if(!top || top->id/10000!=105){Set(-106,pantsBeforeOverall);pantsAuto=false;}}
        if(!Valid()){draft=std::move(old);pantsAuto=oldAuto;return false;}
        original=false;return true;
    }
    void Restore(){draft.clear();original=false;pantsAuto=false;}
    // Look of a saved outfit, for preview only. Unavailable pieces show as empty, never as the
    // current wear; only protected/excluded slots keep what is worn. One instance per slot, and
    // never an overall over pants.
    std::map<short,std::uint64_t> PreviewOutfit(const Outfit& outfit)const {
        std::map<short,std::uint64_t> view;if(!outfit.defined)return view;
        const auto resolved=Resolve(outfit,true);std::set<short> kept,fallback;
        for(const auto& e:outfit.entries){
            if(std::find(Slots.begin(),Slots.end(),e.slot)==Slots.end())continue;
            std::uint64_t uid=0;auto choice=resolved.choices.find(e.slot);
            if(choice!=resolved.choices.end())uid=choice->second;
            else if(e.mode==2 && !Locked(e.slot)){auto item=DetailItem(e);uid=item?item->uid:0;if(uid)fallback.insert(e.slot);}
            else {kept.insert(e.slot);continue;}
            if(uid!=Base(e.slot))view[e.slot]=uid;
        }
        auto pick=[&](short slot){auto i=view.find(slot);return i==view.end()?Base(slot):i->second;};
        auto set=[&](short slot,std::uint64_t uid){if(uid==Base(slot))view.erase(slot);else view[slot]=uid;};
        // Each instance is shown once: kept wear first, then resolved choices, then fallbacks.
        std::set<std::uint64_t> seen;
        for(int pass=0;pass<3;pass++)for(short slot:Slots){
            if((pass==0)!=(kept.count(slot)!=0) || pass>0 && (pass==2)!=(fallback.count(slot)!=0))continue;
            auto uid=pick(slot);if(uid && !seen.insert(uid).second)set(slot,0);
        }
        // Worn pieces can be protected with uid 0, so read what each slot actually shows.
        auto shown=[&](short slot)->const Item*{auto i=view.find(slot);return i==view.end()?Worn(slot):Find(i->second);};
        auto top=shown(-105);
        if(top && top->id/10000==105 && shown(-106)){
            if(kept.count(-106) || Locked(-106))set(-105,0);   // protected pants stay; the overall cannot
            else set(-106,0);
        }
        return view;
    }
    bool Load(const Outfit& outfit) {
        if(!outfit.defined)return false;
        auto resolved=Resolve(outfit);if(resolved.Blocked())return false;
        auto old=draft;draft.clear();
        for(const auto& e:resolved.choices)Set(e.first,e.second);
        if(!Valid()){draft=std::move(old);return false;}original=false;pantsAuto=false;return true;
    }
    bool MatchesCurrent(const Outfit& outfit)const {
        if(!outfit.defined || outfit.entries.size()!=Slots.size())return false;
        const auto resolved=Resolve(outfit);if(resolved.Blocked() || resolved.choices.empty())return false;
        for(const auto& e:resolved.choices)if(Base(e.first)!=e.second)return false;
        return true;
    }
    int CurrentPreset(int preferred=-1)const {
        if(preferred>=0 && preferred<6 && MatchesCurrent(current.outfits[preferred]))return preferred;
        for(int i=0;i<6;i++)if(MatchesCurrent(current.outfits[i]))return i;
        return -1;
    }
    void Refresh(Snapshot next,bool applied) {
        current=std::move(next);if(applied){draft.clear();original=false;pantsAuto=false;}
        else {
            for(auto i=draft.begin();i!=draft.end();) {
                auto item=Find(i->second);
                if(i->second==Base(i->first) || Locked(i->first) || i->second && (!item || !(item->flags&1)))i=draft.erase(i);else ++i;
            }
            auto top=Find(Desired(-105));
            if(pantsAuto && (!top || top->id/10000!=105)) {
                auto pants=Find(pantsBeforeOverall);
                if(!Locked(-106))Set(-106,pants && pants->flags&1?pantsBeforeOverall:Base(-106));
                pantsAuto=false;
            }
        }
    }
    std::vector<Scope> SaveScope()const {
        std::vector<Scope> result;
        for(short slot:Slots) {
            auto uid=original?Base(slot):Desired(slot);auto item=Find(uid);unsigned mode=uid?2:1;
            if(Locked(slot) || uid && (!item || !(item->flags&1)))mode=0;
            result.push_back({slot,mode,mode==2?uid:0,mode==2?item->id:0,true});
        }
        return result;
    }
};
}
