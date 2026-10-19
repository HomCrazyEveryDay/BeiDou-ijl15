#pragma once
namespace WardrobeWnd {
bool Install();
bool HandlePacket(const unsigned char* data,unsigned short length);
void OnFieldUpdate(void* localUser);
void OnFieldDispose();
bool IsEditing();
}
