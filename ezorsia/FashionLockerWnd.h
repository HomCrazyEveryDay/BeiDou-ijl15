#pragma once

namespace FashionLockerWnd
{
bool Install(bool enableLog);
bool HandlePacket(const unsigned char* data, unsigned short length);
void OnFieldUpdate();
void OnFieldDispose();
}
