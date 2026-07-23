#pragma once

namespace D3D8DisplayModeHook {

// The client asks D3D8 whether the configured window size exists in the adapter
// mode list before it creates the device. Some GPUs/drivers omit useful
// windowed sizes such as 1366x768, so we append exactly one synthetic mode.
struct DisplayMode {
	UINT Width;
	UINT Height;
	UINT RefreshRate;
	int Format;
};

typedef void* (WINAPI* Direct3DCreate8_t)(UINT sdkVersion);
typedef UINT(WINAPI* GetAdapterModeCount_t)(void* self, UINT adapter);
typedef HRESULT(WINAPI* EnumAdapterModes_t)(void* self, UINT adapter, UINT modeIndex, DisplayMode* mode);

static Direct3DCreate8_t s_direct3DCreate8 = nullptr;
static GetAdapterModeCount_t s_getAdapterModeCount = nullptr;
static EnumAdapterModes_t s_enumAdapterModes = nullptr;

// Avoid changing anything when the configured resolution is already advertised.
static bool HasConfiguredMode(void* d3d8, UINT adapter, UINT realCount) {
	if (!s_enumAdapterModes) {
		return false;
	}

	for (UINT i = 0; i < realCount; ++i) {
		DisplayMode mode{};
		if (s_enumAdapterModes(d3d8, adapter, i, &mode) >= 0 &&
			mode.Width == static_cast<UINT>(Client::m_nGameWidth) &&
			mode.Height == static_cast<UINT>(Client::m_nGameHeight)) {
			return true;
		}
	}

	return false;
}

static DisplayMode MakeConfiguredMode(void* d3d8, UINT adapter, UINT realCount) {
	// Default to the format used by this client in windowed mode, then copy the
	// first real mode's format/rate when available so the synthetic entry stays
	// close to the adapter's normal enumeration.
	DisplayMode configured{
		static_cast<UINT>(Client::m_nGameWidth),
		static_cast<UINT>(Client::m_nGameHeight),
		60,
		22 // D3DFMT_X8R8G8B8.
	};

	if (!s_enumAdapterModes) {
		return configured;
	}

	for (UINT i = 0; i < realCount; ++i) {
		DisplayMode mode{};
		if (s_enumAdapterModes(d3d8, adapter, i, &mode) >= 0) {
			configured.RefreshRate = mode.RefreshRate ? mode.RefreshRate : configured.RefreshRate;
			configured.Format = mode.Format;
			break;
		}
	}

	return configured;
}

static UINT WINAPI GetAdapterModeCount_Hook(void* self, UINT adapter) {
	const UINT realCount = s_getAdapterModeCount(self, adapter);
	return HasConfiguredMode(self, adapter, realCount) ? realCount : realCount + 1;
}

static HRESULT WINAPI EnumAdapterModes_Hook(void* self, UINT adapter, UINT modeIndex, DisplayMode* mode) {
	const UINT realCount = s_getAdapterModeCount ? s_getAdapterModeCount(self, adapter) : 0;
	// The added count points the client at index realCount; only synthesize that
	// one slot and forward every real adapter mode unchanged.
	if (modeIndex == realCount && !HasConfiguredMode(self, adapter, realCount)) {
		if (mode) {
			*mode = MakeConfiguredMode(self, adapter, realCount);
		}
		return S_OK;
	}

	return s_enumAdapterModes(self, adapter, modeIndex, mode);
}

static void InstallInterfaceHooks(void* d3d8) {
	if (!d3d8) {
		return;
	}

	// IDirect3D8 vtable: slot 6 = GetAdapterModeCount, slot 7 = EnumAdapterModes.
	void** vtable = *reinterpret_cast<void***>(d3d8);
	if (!s_getAdapterModeCount) {
		s_getAdapterModeCount = reinterpret_cast<GetAdapterModeCount_t>(vtable[6]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_getAdapterModeCount), GetAdapterModeCount_Hook);
	}
	if (!s_enumAdapterModes) {
		s_enumAdapterModes = reinterpret_cast<EnumAdapterModes_t>(vtable[7]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_enumAdapterModes), EnumAdapterModes_Hook);
	}
}

static void* WINAPI Direct3DCreate8_Hook(UINT sdkVersion) {
	void* d3d8 = s_direct3DCreate8(sdkVersion);
	// Hook the returned interface immediately; the client validates modes soon
	// after Direct3DCreate8 returns.
	InstallInterfaceHooks(d3d8);
	return d3d8;
}

static void Install() {
	if (s_direct3DCreate8) {
		return;
	}

	HMODULE d3d8 = LoadLibraryA("d3d8.dll");
	if (!d3d8) {
		return;
	}

	s_direct3DCreate8 = reinterpret_cast<Direct3DCreate8_t>(GetProcAddress(d3d8, "Direct3DCreate8"));
	if (s_direct3DCreate8) {
		Memory::SetHook(true, reinterpret_cast<void**>(&s_direct3DCreate8), Direct3DCreate8_Hook);
	}
}

} // namespace D3D8DisplayModeHook
