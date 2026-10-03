#pragma once
namespace MixedDyeWnd {
bool Install();
bool HandlePacket(const unsigned char* data,unsigned short length);
void OnFieldUpdate(void* localUser);
void OnFieldDispose();
}
