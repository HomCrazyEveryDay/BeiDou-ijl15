#pragma once
#include "GameLaunchSettings.h"

namespace LauncherGate
{
	bool Authorize();
	const game_settings::Snapshot& Settings();
	bool ShouldShowUnauthorizedLaunchMessage();
	void ShowUnauthorizedLaunchMessage();
}
