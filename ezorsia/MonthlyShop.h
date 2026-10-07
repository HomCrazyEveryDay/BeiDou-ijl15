#pragma once
namespace MonthlyShop {
bool Install();
bool HandlePacket(const unsigned char* data,unsigned short length);
void BeforeNativePacket(unsigned short opcode);
bool DrawPointCurrency(void* canvas,int x,int y);
}
