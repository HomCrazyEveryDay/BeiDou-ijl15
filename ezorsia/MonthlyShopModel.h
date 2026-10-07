#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <set>

namespace MonthlyShopModel {
struct Month { int id=0,multiplier=1;std::string label; };
struct Product { int sn=0,item=0,month=0,price=0,count=0;bool sale=false; };
struct Snapshot { int channel=0,token=0,month=0,multiplier=1;std::int64_t points=0;bool changed=false;std::vector<Month> months;std::vector<Product> products; };
struct Reader {
    const unsigned char* data;size_t size,pos=0;
    unsigned read(size_t n){if(n>4 || pos+n>size)throw 1;unsigned v=0;for(size_t i=0;i<n;++i)v|=unsigned(data[pos++])<<(8*i);return v;}
    std::string text(){size_t n=read(2);if(n>96 || pos+n>size)throw 1;std::string v(reinterpret_cast<const char*>(data+pos),n);pos+=n;return v;}
};
inline bool Decode(const unsigned char* data,size_t size,Snapshot& out) {
    try {
        if(!data || size<36)return false;Reader r{data,size};
        if(r.read(2)!=0x4d53 || r.read(1)!=1)return false;
        Snapshot s;s.channel=r.read(1);s.token=r.read(4);s.month=r.read(4);s.multiplier=r.read(4);
        auto low=r.read(4),high=r.read(4);s.points=(std::uint64_t(high)<<32)|low;
        s.changed=r.read(1)!=0;int n=r.read(1);
        if(s.channel<1 || s.channel>4 || s.token<=0 || s.multiplier<1 || s.multiplier>100 || n<1 || n>121 || s.points<0)return false;
        std::set<int> months,sns;
        for(int i=0;i<n;++i){Month m;m.id=r.read(4);m.multiplier=r.read(4);m.label=r.text();
            if(m.label.empty() || m.multiplier<1 || m.multiplier>100 || !months.insert(m.id).second)return false;s.months.push_back(m);}
        if(!months.count(s.month))return false;
        n=r.read(2);if(n>10000 || s.channel!=4 && n)return false;
        for(int i=0;i<n;++i){Product p;p.sn=r.read(4);p.item=r.read(4);p.month=r.read(4);p.price=r.read(4);p.count=r.read(2);p.sale=r.read(1)!=0;
            if(p.sn<=0 || p.item<1000000 || p.price<=0 || p.count<1 || p.count>1000 || !sns.insert(p.sn).second)return false;s.products.push_back(p);}
        if(r.pos!=size)return false;out=std::move(s);return true;
    }catch(...){return false;}
}
inline void Put(std::vector<unsigned char>& data,unsigned value,int bytes){for(int i=0;i<bytes;++i)data.push_back(static_cast<unsigned char>(value>>(i*8)));}
inline std::vector<unsigned char> Select(const Snapshot& s,int month) {
    std::vector<unsigned char> data;Put(data,0x1005,2);Put(data,0x4d53,2);Put(data,1,1);Put(data,s.channel,1);Put(data,s.token,4);Put(data,month,4);return data;
}
}
