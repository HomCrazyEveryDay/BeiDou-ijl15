#pragma once
#include <cstdint>

// A render-only ID in the existing 32-bit avatar fields. Real style IDs remain
// unchanged on the server. No extra packet bytes, shared canvas edits or IMG copies.
namespace MixedDye {
constexpr std::uint32_t HairTag = 0x40000000, FaceTag = 0x50000000;
struct Style { int primary=0, secondary=0; bool face=false; };
inline bool IsFace(int id) {
    return (id>=20000 && id<30000) || (id>=50000 && id<60000)
        || (id>=80000 && id<90000);
}
inline bool IsHair(int id) {
    // Stale 409xx/421xx copies in Face/ have islot=Hr and are actual hair.
    return (id>=30000 && id<50000) || (id>=60000 && id<80000);
}
inline int Color(int id,bool face) { return face ? id/100%10 : id%10; }
inline int WithColor(int id,int color,bool face) { return id+(color-Color(id,face))*(face?100:1); }
inline std::uint32_t Encode(int primary,int secondaryColor,bool face) {
    if (!(face?IsFace(primary):IsHair(primary)) || Color(primary,face)>7
        || secondaryColor<0 || secondaryColor>7 || secondaryColor==Color(primary,face)) return primary;
    return (face?FaceTag:HairTag) | (secondaryColor<<17) | primary;
}
inline bool Decode(std::uint32_t id,Style& result) {
    const auto tag=id&0xfff00000;
    if(tag!=HairTag && tag!=FaceTag)return false;
    const bool face=tag==FaceTag;const int primary=id&0x1ffff,color=(id>>17)&7;
    if(Encode(primary,color,face)!=id)return false;
    result={primary,WithColor(primary,color,face),face};return true;
}
bool Install();
bool AttachResourceManager(void* manager);
inline bool IsCoupon(int id) { return id==5151040 || id==5152302; }
void SendCoupon(int position,int id);
}
