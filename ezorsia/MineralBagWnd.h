#pragma once

namespace MineralBagWnd
{
bool Install();
bool HandlePacket(const unsigned char* data, unsigned short length);
void OnFieldUpdate();
void OnFieldDispose();
}
