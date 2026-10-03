#pragma once
#include "MixedDye.h"
#include <cstring>

namespace MixedDyeWindowModel {
struct Snapshot { int result=0,token=0,coupon=0,original=0,secondary=-1,mask=0; };
inline bool Decode(const unsigned char* data,unsigned size,Snapshot& out) {
    if(!data || size!=24 || data[4]!=0x0e || data[5]!=0x10 || data[6]!=0x4d || data[7]!=0x44 || data[8]!=1)return false;
    Snapshot value;value.result=data[9];std::memcpy(&value.token,data+10,4);
    std::memcpy(&value.coupon,data+14,4);std::memcpy(&value.original,data+18,4);
    value.secondary=data[22]==255?-1:data[22];value.mask=data[23];
    if(value.result>3 || value.token<=0)return false;
    if(value.result==0) {
        const bool face=value.coupon==5152302;
        if(!MixedDye::IsCoupon(value.coupon) || !(face?MixedDye::IsFace(value.original):MixedDye::IsHair(value.original))
            || value.secondary>7 || value.mask==0 || !(value.mask&(value.mask-1)))return false;
    } else if(value.coupon || value.original || value.secondary!=-1 || value.mask)return false;
    out=value;return true;
}
inline bool Selectable(int mask,int color,int other) { return color>=0 && color<8 && color!=other && (mask&(1<<color)); }
inline bool Ready(int mask,int primary,int secondary) {return Selectable(mask,primary,secondary) && Selectable(mask,secondary,primary);}
inline void Action(unsigned char (&data)[12],int action,int token,int primary,int secondary) {
    const unsigned char header[]={4,0x10,0x4d,0x44,1};std::memcpy(data,header,5);
    data[5]=static_cast<unsigned char>(action);std::memcpy(data+6,&token,4);
    data[10]=static_cast<unsigned char>(primary);data[11]=static_cast<unsigned char>(secondary);
}
}
