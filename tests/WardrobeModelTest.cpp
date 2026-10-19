#include "../ezorsia/WardrobeModel.h"
#include "../ezorsia/WardrobeInput.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <iterator>
using namespace WardrobeModel;
static Item item(std::uint64_t uid,int id,short slot,short pos=1,unsigned flags=3){Item i;i.uid=uid;i.id=id;i.slot=slot;i.position=pos;i.flags=flags;return i;}
static Outfit emptyOutfit(){Outfit o;o.defined=true;for(short slot:Slots)o.entries.push_back({slot,1,0,0,true});return o;}
int main(int argc,char** argv){
    {
    Model rings;
    auto nameOld=item(301,1112100,-112,-112),nameNew=item(302,1112101,-112),chat=item(303,1112200,-113,-113),plain=item(304,1112000,-115,-115);
    nameOld.ringEffects=nameNew.ringEffects=1;chat.ringEffects=2;
    rings.current.items[2]={nameOld,chat,plain};rings.current.items[0]={nameNew};
    assert(rings.Toggle(302)&&rings.Desired(-112)==302&&rings.Desired(-113)==303&&rings.Desired(-115)==304&&!rings.Desired(-116));
    assert(rings.Toggle(302)&&!rings.Desired(-112)); // Second double click removes the previewed ring.
    rings.draft.clear();auto duplicate=nameOld;duplicate.uid=305;duplicate.position=-116;rings.current.items[2].push_back(duplicate);
    assert(rings.Try(302)&&rings.Desired(-112)==302&&!rings.Desired(-116));
    rings.draft.clear();rings.current.items[2].back().flags=7;
    assert(!rings.Try(302)&&rings.draft.empty()); // Never partially clear duplicates when a matching ring is protected.
    auto details=Scope{-112,2,302,1112101,true};assert(rings.DetailItem(details)==&rings.current.items[0][0]);
    details.uid=999;assert(rings.DetailItem(details)==&rings.current.items[0][0]);
    rings.current.items[1]={nameNew};rings.current.items[1][0].uid=306;assert(!rings.DetailItem(details));
    details.itemId=1112999;assert(!rings.DetailItem(details));
    }
    WardrobeInput::ReleaseGate gate;
    gate.Hold(32);assert(!gate.Read(32,true));assert(!gate.Read(32,true));
    assert(!gate.Read(32,false));assert(gate.Read(32,true));
    gate.Hold(65);assert(!gate.Read(65,true));assert(gate.Read(32,true));
    assert(!gate.Suppressed(65,false));assert(gate.Read(65,true));
    assert(!gate.Read(256,true));
    assert(Category(1000000)==1 && Category(1040000)==2 && Category(1050000)==2 && Category(1060000)==2);
    assert(Category(1100000)==3 && Category(1700000)==4 && Category(1070000)==5 && Category(1080000)==6 && Category(1110000)==7);
    Model m;m.current.items[2]={item(1,1040000,-105,-105),item(2,1060000,-106,-106)};
    m.current.items[1]={item(3,1050000,-105),item(4,1040001,-105),item(5,1060001,-106)};
    assert(m.Try(3));assert(m.Desired(-105)==3 && m.Desired(-106)==0 && m.Valid());
    assert(m.current.items[2][0].uid==1); // Preview never mutates inventory.
    assert(m.Try(4));assert(m.Desired(-105)==4 && m.Desired(-106)==2 && m.Valid());
    assert(m.Try(3) && m.Try(5));assert(m.Desired(-105)==1 && m.Desired(-106)==5 && m.Valid());
    auto scope=m.SaveScope();assert(scope.size()==15);for(auto e:scope)if(e.slot==-112 || e.slot==-102)assert(e.mode==1);
    Outfit missing=emptyOutfit();missing.entries[0]={-101,2,999,1000000,true};auto oldDraft=m.draft;
    assert(!m.Load(missing));assert(m.draft==oldDraft&&m.Resolve(missing).missing==1); // Failed preset cannot mutate the home preview either.
    Snapshot fresh=m.current;fresh.items[1].clear();m.Refresh(fresh,false);assert(m.Valid()&&m.draft.empty()); // Missing preview UIDs are removed, never substituted by same ID.
    m.Refresh(fresh,true);assert(m.draft.empty() && m.Valid());
    m.current.items[2].push_back(item(6,1000000,-101,-101,4));m.current.items[1].push_back(item(7,1000001,-101));assert(!m.Try(7));
    Model limited;limited.current.items[2]={item(60,1000000,-101,-101,4)};
    limited.current.items[0]={item(61,1000001,-101)};
    assert(limited.Locked(-101)&&!limited.Toggle(60)&&!limited.Try(61));
    Outfit removeLimited=emptyOutfit();
    assert(limited.Load(removeLimited)&&limited.draft.empty()&&limited.Resolve(removeLimited).excluded==1); // Protected originals are outside wardrobe control.
    Model timed;timed.current.items[2]={item(62,1000000,-101,-101,0)};timed.current.items[0]={item(63,1000001,-101)};
    timed.current.items[2][0].reason=2;
    assert(!timed.Locked(-101)&&!timed.Load(emptyOutfit())&&timed.draft.empty()); // Timed clothes cannot be returned to the shared wardrobe.
    auto replacement=emptyOutfit();replacement.entries[0]={-101,2,63,1000001,true};
    assert(!timed.Load(replacement)&&timed.Desired(-101)==62&&timed.Valid());
    replacement.entries[0]={-101,2,62,1000000,false};assert(timed.MatchesCurrent(replacement)); // Already worn timed original need not move.
    Model rings;
    rings.current.items[2]={item(0,1115395,-112,-112,4),item(11,1115389,-113,-113)};
    rings.current.items[0]={item(12,1115390,-112)};
    assert(rings.PreviewItem(-112)->id==1115395); // Locked UID-less originals still show their name tag.
    rings.Set(-113,12);assert(rings.PreviewItem(-113)->id==1115390);
    rings.original=true;assert(rings.PreviewItem(-113)->id==1115389);
    rings.original=false;rings.Set(-113,0);assert(!rings.PreviewItem(-113));
    assert(rings.current.items[2][1].id==1115389); // Preview removal does not alter equipped rings.
    rings.draft.clear();
    assert(rings.Toggle(12) && rings.Desired(-115)==12); // First free ring slot, locked ring is preserved.
    assert(rings.Toggle(12) && !rings.Desired(-115) && rings.Desired(-113)==11);
    assert(rings.Toggle(11) && !rings.Desired(-113));
    assert(rings.Toggle(11) && rings.Desired(-113)==11); // Worn item toggles back on by UID.
    auto ringScope=rings.SaveScope();
    for(auto e:ringScope){if(e.slot==-112)assert(e.mode==0);if(e.slot==-113)assert(e.mode==2&&e.uid==11);}
    assert(!rings.Toggle(0));
    Model toggle;toggle.current.items[2]={item(1,1040000,-105,-105),item(2,1060000,-106,-106)};
    toggle.current.items[0]={item(3,1050000,-105),item(5,1060001,-106),item(8,1000000,-101),item(9,1000000,-101)};
    assert(toggle.Toggle(8) && toggle.IsPreviewed(8));
    assert(toggle.Toggle(8) && !toggle.IsPreviewed(8) && toggle.draft.empty());
    assert(toggle.Toggle(8) && toggle.Toggle(9) && !toggle.IsPreviewed(8) && toggle.IsPreviewed(9)); // Same ID is not the same item.
    assert(toggle.Toggle(5) && toggle.Toggle(3) && toggle.Desired(-106)==0);
    assert(toggle.Toggle(3) && toggle.Desired(-105)==0 && toggle.Desired(-106)==5 && toggle.Valid());
    assert(toggle.current.items[2][0].uid==1 && toggle.current.items[2][1].uid==2);
    Model selection;selection.current.items[2]={item(31,1000000,-101,-101)};
    Outfit actual;actual.defined=true;actual.entries=selection.SaveScope();selection.current.outfits[1]=actual;
    selection.current.outfits[4]=actual;
    assert(selection.CurrentPreset()==1 && selection.CurrentPreset(4)==4);
    selection.current.items[0]={item(32,1000000,-101)};assert(selection.Toggle(32));
    assert(selection.CurrentPreset()==1); // Unsaved preview must not change actual-wear selection.
    selection.original=true;auto actualScope=selection.SaveScope();for(auto e:actualScope)if(e.slot==-101)assert(e.uid==31);
    assert(selection.draft.at(-101)==32); // Viewing current outfit does not discard the home preview.
    selection.original=false;
    selection.current.items[2][0].uid=32;assert(selection.CurrentPreset()==1); // Unique legacy same-ID identity can recover the saved appearance.
    for(auto& e:actual.entries){e.mode=0;e.uid=0;}assert(!selection.MatchesCurrent(actual));
    actual.entries.clear();assert(!selection.MatchesCurrent(actual));
    Model legacy;legacy.current.items[2]={item(71,1082162,-108,-108),item(72,1102156,-109,-109)};
    legacy.current.items[0]={item(73,1002186,-101),item(74,1003013,-101)};
    Outfit recovered;recovered.defined=true;recovered.entries=legacy.SaveScope();
    for(auto& e:recovered.entries){
        if(e.slot==-101)e={e.slot,2,900,1002186,false};
        if(e.slot==-108)e={e.slot,2,901,1082162,false};
        if(e.slot==-109)e={e.slot,2,902,1102156,false};
    }
    auto resolved=legacy.Resolve(recovered);assert(!resolved.Blocked()&&resolved.choices.at(-101)==73);
    assert(legacy.Load(recovered)&&legacy.Desired(-101)==73&&legacy.Desired(-108)==71&&legacy.Desired(-109)==72);
    legacy.current.outfits[0]=recovered;assert(legacy.CurrentPreset()==-1); // Preview is not real wear.
    auto after=legacy.current;after.items[2].push_back(item(73,1002186,-101,-101));after.items[0].erase(after.items[0].begin());legacy.Refresh(after,true);
    assert(legacy.CurrentPreset()==0); // Legacy invalid bits do not hide actual matches.
    auto partial=recovered;for(auto& e:partial.entries)if(e.slot==-109)e={e.slot,2,903,1109999,false};
    assert(legacy.Resolve(partial).missing==1&&!legacy.Load(partial)&&legacy.Desired(-109)==72);
    legacy.current.outfits[1]=partial;assert(legacy.CurrentPreset()==0&&legacy.CurrentPreset(1)==0); // Missing pieces cannot masquerade as the current full outfit.
    auto unique=recovered;for(auto& e:unique.entries)if(e.slot==-101)e.uid=73;
    legacy.current.items[0].push_back(item(75,1002186,-101));
    assert(legacy.Resolve(recovered).ambiguous==1); // Ambiguous same-ID replacements never pick arbitrarily.
    assert(legacy.Resolve(unique).ambiguous==0); // An exact UID wins even when another copy exists.
    legacy.current.items[2][0].flags=4;
    assert(legacy.MatchesCurrent(unique)); // Already worn locked piece is still visually matched.
    Model subset;subset.current.items[2]={item(80,1000000,-101,-101,4),item(81,1100000,-109,-109)};
    subset.current.items[0]={item(82,1000001,-101),item(83,1100001,-109)};
    Outfit mixed;mixed.defined=true;mixed.entries=subset.SaveScope();
    for(auto& e:mixed.entries){if(e.slot==-101)e={e.slot,2,82,1000001,false};if(e.slot==-109)e={e.slot,2,83,1100001,true};}
    assert(!subset.Load(mixed)&&subset.Desired(-101)==80&&subset.Desired(-109)==81&&subset.Valid());
    assert(subset.Resolve(mixed).restricted==1);
    Model boundLocker;boundLocker.current.items[0]={item(84,1000000,-101,1,1)};
    Outfit blocked;blocked.defined=true;blocked.entries=boundLocker.SaveScope();
    for(auto& e:blocked.entries)if(e.slot==-101)e={e.slot,2,84,1000000,true};
    assert(boundLocker.Resolve(blocked).restricted==1&&!boundLocker.Load(blocked)&&boundLocker.draft.empty());
    Model relocated;relocated.current.items[2]={item(85,1112119,-115,-115)};
    Outfit ringMove;ringMove.defined=true;ringMove.entries=relocated.SaveScope();
    for(auto& e:ringMove.entries){if(e.slot==-115)e={e.slot,0,0,0,true};if(e.slot==-112)e={e.slot,2,85,1112119,true};}
    assert(relocated.Load(ringMove)&&relocated.Desired(-112)==85&&!relocated.Desired(-115)&&relocated.Valid());
    relocated.current.items[2].push_back(item(89,1112118,-112,-112));
    for(auto& e:ringMove.entries)if(e.slot==-115)e={e.slot,2,89,1112118,true};
    assert(relocated.Load(ringMove)&&relocated.Desired(-112)==85&&relocated.Desired(-115)==89&&relocated.Valid());
    Model layers;layers.current.items[2]={item(86,1060000,-106,-106,4)};
    layers.current.items[0]={item(87,1050000,-105),item(88,1000000,-101)};
    Outfit layered;layered.defined=true;layered.entries=layers.SaveScope();
    for(auto& e:layered.entries){if(e.slot==-105)e={e.slot,2,87,1050000,true};if(e.slot==-101)e={e.slot,2,88,1000000,true};}
    assert(!layers.Load(layered)&&!layers.Desired(-101)&&layers.Desired(-106)==86&&!layers.Desired(-105)&&layers.Valid());
    for(auto& e:layered.entries)if(e.slot==-101)e={e.slot,1,0,0,true};
    assert(layers.Resolve(layered).choices.empty()); // The only requested overall cannot strip anything when pants are protected.
    Model absentOverall;absentOverall.current.items[2]={item(95,1040000,-105,-105),item(96,1060000,-106,-106)};
    absentOverall.current.items[0]={item(97,1000000,-101)};
    Outfit absent;absent.defined=true;absent.entries=absentOverall.SaveScope();
    for(auto& e:absent.entries){if(e.slot==-105)e={e.slot,2,999,1050000,false};if(e.slot==-106)e={e.slot,1,0,0,true};if(e.slot==-101)e={e.slot,2,97,1000000,true};}
    assert(!absentOverall.Load(absent)&&absentOverall.draft.empty());
    assert(absentOverall.Desired(-105)==95&&absentOverall.Desired(-106)==96); // Missing overall rejects the entire change, preserving top and pants.
    Model bagOnly;bagOnly.current.items[1]={item(99,1000000,-101)};
    auto bagPreset=emptyOutfit();bagPreset.entries[0]={-101,2,99,1000000,true};
    assert(!bagOnly.Resolve(bagPreset).Blocked()&&bagOnly.Load(bagPreset));
    bagPreset.entries[0].uid=999;
    assert(bagOnly.Resolve(bagPreset).choices.at(-101)==99); // Unique legacy recovery includes the bag.
    bagOnly.current.items[0]={item(98,1000000,-101)};
    assert(bagOnly.Resolve(bagPreset).ambiguous==1); // Never guess between two owned copies.
    bagPreset.entries[0].uid=99; // A known instance stays unambiguous across all locations.
    bagOnly.current.freeBag=0;assert(bagOnly.Load(bagPreset));
    Model equipmentEdit;equipmentEdit.current.items[2]={item(201,1000000,-101,-101),item(202,1100000,-109,-109)};
    Outfit original;original.defined=true;original.entries=equipmentEdit.SaveScope();equipmentEdit.current.outfits[0]=original;
    assert(equipmentEdit.CurrentPreset()==0);
    auto edited=equipmentEdit.current;
    edited.items[1]={edited.items[2][0]};edited.items[1][0].position=1;
    edited.items[2][0]=item(203,1000001,-101,-101);equipmentEdit.Refresh(edited,false);
    assert(equipmentEdit.CurrentPreset()==-1&&!equipmentEdit.Resolve(original).Blocked());
    assert(equipmentEdit.current.outfits[0].entries[0].uid==201); // E changes actual wear, never saved presets.
    Outfit second;second.defined=true;second.entries=equipmentEdit.SaveScope();equipmentEdit.current.outfits[1]=second;
    assert(equipmentEdit.CurrentPreset()==1);
    equipmentEdit.current.items[2].push_back(item(0,1302000,-11,-11,4));
    assert(equipmentEdit.CurrentPreset()==1); // Ordinary equipment is outside the appearance scope.
    auto secondWear=equipmentEdit.current;
    assert(equipmentEdit.Load(original)&&equipmentEdit.Desired(-101)==201&&equipmentEdit.CurrentPreset()==1);
    secondWear.items[1]={item(203,1000001,-101,1)};secondWear.items[2][0]=item(201,1000000,-101,-101);
    equipmentEdit.Refresh(secondWear,true);assert(equipmentEdit.CurrentPreset()==0&&!equipmentEdit.Resolve(second).Blocked());
    Model initialWithTimed;initialWithTimed.current.items[2]={item(211,1000000,-101,-101),item(212,1100000,-109,-109,0)};
    Outfit timedInitial;timedInitial.defined=true;timedInitial.entries=initialWithTimed.SaveScope();
    initialWithTimed.current.outfits[0]=timedInitial;
    assert(initialWithTimed.CurrentPreset()==0&&initialWithTimed.Load(timedInitial)&&initialWithTimed.draft.empty());
    assert(initialWithTimed.Resolve(timedInitial).excluded==1); // Excluded timed wear does not invalidate the initial current marker.
    Model complete;
    complete.current.items[2]={item(101,1002186,-101,-101),item(102,1050000,-105,-105),item(103,1070000,-107,-107),item(104,1080000,-108,-108),item(105,1100000,-109,-109),item(0,1112800,-112,-112,4),item(0,1302000,-11,-11,4)};
    Outfit five;five.defined=true;five.entries=complete.SaveScope();
    Outfit one=emptyOutfit();one.entries[0]={-101,2,101,1002186,true};
    complete.current.outfits[0]=five;complete.current.outfits[1]=one;
    assert(complete.CurrentPreset()==0&&!complete.MatchesCurrent(one));
    assert(complete.Load(one)&&complete.draft.size()==4&&complete.Valid());
    for(short slot:{short(-105),short(-107),short(-108),short(-109)})assert(complete.draft.at(slot)==0);
    for(const auto& entry:complete.draft)assert(std::find(Slots.begin(),Slots.end(),entry.first)!=Slots.end()); // Never address ordinary equipment positions.
    assert(complete.PreviewItem(-112)->id==1112800&&complete.PreviewItem(-11)->id==1302000);
    auto legacyOne=one;for(auto& e:legacyOne.entries)if(e.mode==1)e.mode=0;
    auto expected=complete.draft;assert(complete.Load(legacyOne)&&complete.draft==expected); // Old KEEP fields cannot inherit editable cash equipment.
    auto another=complete;another.current.items[2].push_back(item(106,1020000,-103,-103));
    assert(another.Load(legacyOne)&&another.draft.at(-103)==0&&!another.Desired(-109)); // Same saved look regardless of previous outfit.
    auto wornOne=complete.current;wornOne.items[2].clear();
    for(const auto& i:complete.current.items[2]){if(complete.draft.count(i.position))wornOne.items[0].push_back(i);else wornOne.items[2].push_back(i);}
    complete.Refresh(wornOne,true);assert(complete.CurrentPreset()==1&&complete.MatchesCurrent(legacyOne));
    assert(complete.Load(five)&&complete.draft.size()==4&&complete.Valid()); // Switching back restores all four removed pieces.
    assert(complete.Load(emptyOutfit())&&complete.Desired(-101)==0&&complete.draft.size()==1);
    for(const auto& e:complete.SaveScope())assert(e.slot==-112?e.mode==0:e.mode==1); // Empty presets explicitly store no cash equipment.
    auto invalid=one;invalid.entries.pop_back();assert(!complete.Load(invalid));
    std::cout<<"PASS complete outfit replacement (five to one and back), legacy KEEP normalization, protected equipment, missing rejection and exact selection\n";
    std::cout<<"PASS double-click UID toggles, ring scope, overall/pants restoration and actual outfit selection\n";
    std::vector<unsigned char> bytes(4);Number(bytes,0x1005,2);Number(bytes,Magic,2);Number(bytes,Version,1);Number(bytes,0,1);Number(bytes,0,1);
    Number(bytes,1,4);Number(bytes,2,4);Number(bytes,3,4);Number(bytes,240,2);Number(bytes,4,2);String(bytes,"ready");String(bytes,"player");
    for(int i=0;i<3;i++)Number(bytes,0,2);Number(bytes,6,1);for(int i=0;i<6;i++)Number(bytes,0,1);
    Snapshot decoded;assert(Decode(bytes.data(),unsigned(bytes.size()),decoded));assert(decoded.capacity==240 && decoded.request==3);
    for(unsigned size=0;size<bytes.size();size++)assert(!Decode(bytes.data(),size,decoded));
    bytes.push_back(0);assert(!Decode(bytes.data(),unsigned(bytes.size()),decoded));
    if(argc==2){
        std::ifstream input(argv[1],std::ios::binary);assert(input.good());
        std::vector<unsigned char> fixture(4);fixture.insert(fixture.end(),std::istreambuf_iterator<char>(input),{});
        assert(Decode(fixture.data(),unsigned(fixture.size()),decoded));
        assert(decoded.items[1].size()==1);const auto& equipment=decoded.items[1][0];
        assert(equipment.uid==11 && equipment.id==1000000 && equipment.slot==-101 && equipment.position==1);
        const auto& record=equipment.native;assert(record.size()>70 && record[0]==1 && record[5]==1);
        Reader cashSerial{record.data()+6,record.data()+14};assert(cashSerial.number(8)>0);
        assert(record[22]==3 && record[23]==2 && record[24]==17 && record[44]==21 && record[50]==9 && record[52]==5);
        Reader fields{fixture.data()+27,fixture.data()+fixture.size()};fields.text();fields.text();
        assert(fields.number(2)==0 && fields.number(2)==1);fields.number(8);fields.number(2);fields.number(2);fields.number(1);fields.number(1);
        auto lengthAt=fields.p-fixture.data();assert(fields.number(2)==record.size());auto recordAt=fields.p-fixture.data();
        for(unsigned size=0;size<fixture.size();size++)assert(!Decode(fixture.data(),size,decoded));
        auto malformed=fixture;malformed[lengthAt]=1;malformed[lengthAt+1]=2;assert(!Decode(malformed.data(),unsigned(malformed.size()),decoded));
        malformed=fixture;malformed[recordAt]=2;assert(!Decode(malformed.data(),unsigned(malformed.size()),decoded));
        malformed=fixture;for(int i=1;i<=4;i++)malformed[recordAt+i]=0;assert(!Decode(malformed.data(),unsigned(malformed.size()),decoded));
        assert(decoded.items[1][0].id==1000000); // Malformed updates must not replace the current snapshot.
        std::cout<<"PASS Java inventory serialization -> C++ wardrobe snapshot, native attributes and malformed records\n";
    }
    std::cout<<"PASS wardrobe preview, scope, identity, conflicts, locked slots and packet truncation\n";
}
