#include "../ezorsia/MonthlyShopModel.h"
#include <cassert>
#include <iostream>
using namespace MonthlyShopModel;
int main(){
    std::vector<unsigned char> bytes;
    Put(bytes,0x4d53,2);Put(bytes,1,1);Put(bytes,4,1);Put(bytes,12,4);Put(bytes,202610,4);Put(bytes,1,4);
    Put(bytes,0xffffffff,4);Put(bytes,1,4);Put(bytes,0,1);Put(bytes,2,1);
    Put(bytes,202610,4);Put(bytes,1,4);Put(bytes,7,2);for(char ch:std::string("October"))bytes.push_back(ch);
    Put(bytes,0,4);Put(bytes,1,4);Put(bytes,9,2);for(char ch:std::string("Permanent"))bytes.push_back(ch);
    Put(bytes,1,2);Put(bytes,20300398,4);Put(bytes,1051139,4);Put(bytes,202609,4);Put(bytes,2000000,4);Put(bytes,1,2);Put(bytes,1,1);
    Snapshot s;assert(Decode(bytes.data(),bytes.size(),s));assert(s.points==8589934591LL);assert(s.products[0].price==2000000);
    for(size_t n=0;n<bytes.size();++n){Snapshot rejected;assert(!Decode(bytes.data(),n,rejected));}
    auto bad=bytes;bad.push_back(0);assert(!Decode(bad.data(),bad.size(),s));
    bad=bytes;bad[0]=0;assert(!Decode(bad.data(),bad.size(),s));
    bad=bytes;bad[3]=9;assert(!Decode(bad.data(),bad.size(),s));
    bad=bytes;bad[25]=255;assert(!Decode(bad.data(),bad.size(),s));
    bad=bytes;bad[24]=255;assert(Decode(bad.data(),bad.size(),s));assert(s.changed);
    auto select=Select(s,202609);assert(select.size()==14);Reader r{select.data(),select.size()};assert(r.read(2)==0x1005);assert(r.read(2)==0x4d53);assert(r.read(1)==1);assert(r.read(1)==4);assert(r.read(4)==12);assert(r.read(4)==202609);
    auto all=bytes;all[3]=1;
    for(size_t i=8;i<12;++i)all[i]=255;
    for(size_t i=43;i<47;++i)all[i]=255;
    all.resize(64);all[62]=all[63]=0;
    assert(Decode(all.data(),all.size(),s) && s.month==-1 && s.months[1].id==-1);
    auto allSelect=Select(s,-1);Reader ar{allSelect.data(),allSelect.size()};ar.read(4);ar.read(4);ar.read(2);assert(ar.read(4)==0xffffffff);
    std::cout<<"PASS monthly shop protocol: truncation, count bounds, quote token, 64-bit points, final price\n";
}
