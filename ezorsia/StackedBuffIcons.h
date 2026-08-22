#pragma once

namespace StackedBuffIcons
{
	void Install(bool enableLog);
	bool HandlePacket(void* packet);
	void DrawCountdownOverlay(void* d3dDevice);
	void OnFieldUpdate();
	void OnFieldInit();
	void OnFieldDispose();
}
