#pragma once

namespace StackedBuffIcons
{
	void Install(bool enableLog);
	bool HandlePacket(void* packet);
	void DrawCountdownOverlay(void* d3dDevice);
	void OnFieldInit();
	void OnFieldDispose();
}
