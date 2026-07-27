#pragma once
#include "StackedBuffIcons.h"

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

// Minimal IDirect3DDevice8 creation parameters used only for opt-in diagnostics.
struct PresentParameters {
	UINT BackBufferWidth;
	UINT BackBufferHeight;
	int BackBufferFormat;
	UINT BackBufferCount;
	int MultiSampleType;
	int SwapEffect;
	HWND hDeviceWindow;
	BOOL Windowed;
	BOOL EnableAutoDepthStencil;
	int AutoDepthStencilFormat;
	DWORD Flags;
	UINT FullScreen_RefreshRateInHz;
	UINT FullScreen_PresentationInterval;
};

typedef void* (WINAPI* Direct3DCreate8_t)(UINT sdkVersion);
typedef UINT(WINAPI* GetAdapterModeCount_t)(void* self, UINT adapter);
typedef HRESULT(WINAPI* EnumAdapterModes_t)(void* self, UINT adapter, UINT modeIndex, DisplayMode* mode);
typedef HRESULT(WINAPI* GetAdapterDisplayMode_t)(void* self, UINT adapter, DisplayMode* mode);
typedef HRESULT(WINAPI* CheckDeviceType_t)(void* self, UINT adapter, int deviceType, int adapterFormat, int backBufferFormat, BOOL windowed);
typedef HRESULT(WINAPI* CheckDeviceFormat_t)(void* self, UINT adapter, int deviceType, int adapterFormat, DWORD usage, int resourceType, int checkFormat);
typedef HRESULT(WINAPI* GetDeviceCaps_t)(void* self, UINT adapter, int deviceType, void* caps);
typedef HRESULT(WINAPI* CreateDevice_t)(
	void* self,
	UINT adapter,
	int deviceType,
	HWND focusWindow,
	DWORD behaviorFlags,
	PresentParameters* presentationParameters,
	void** returnedDeviceInterface);
typedef HRESULT(WINAPI* EndScene_t)(void* self);

static Direct3DCreate8_t s_direct3DCreate8 = nullptr;
static GetAdapterModeCount_t s_getAdapterModeCount = nullptr;
static EnumAdapterModes_t s_enumAdapterModes = nullptr;
static GetAdapterDisplayMode_t s_getAdapterDisplayMode = nullptr;
static CheckDeviceType_t s_checkDeviceType = nullptr;
static CheckDeviceFormat_t s_checkDeviceFormat = nullptr;
static GetDeviceCaps_t s_getDeviceCaps = nullptr;
static CreateDevice_t s_createDevice = nullptr;
static EndScene_t s_endScene = nullptr;
static bool s_createDeviceStarted = false;

static HRESULT WINAPI EndScene_Hook(void* self);

// Startup logging is disabled by default; enable [debug] enableStartupLog=true
// when diagnosing a remote machine without leaving noisy logs in normal clients.
static void AppendStartupLog(const char* line) {
	if (!Client::enableStartupLog) {
		return;
	}

	WCHAR logPath[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, logPath, MAX_PATH) == 0) {
		lstrcpynW(logPath, L"ijl15_startup.log", MAX_PATH);
	}

	int slash = -1;
	for (int i = lstrlenW(logPath) - 1; i >= 0; i--) {
		if (logPath[i] == L'\\' || logPath[i] == L'/') {
			slash = i;
			break;
		}
	}

	if (slash >= 0) {
		logPath[slash + 1] = L'\0';
		lstrcpynW(logPath + slash + 1, L"ijl15_startup.log", MAX_PATH - slash - 1);
	}
	else {
		lstrcpynW(logPath, L"ijl15_startup.log", MAX_PATH);
	}

	HANDLE file = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	DWORD written = 0;
	WriteFile(file, line, lstrlenA(line), &written, nullptr);
	CloseHandle(file);
}

static void AppendStartupLogF(const char* format, ...) {
	if (!Client::enableStartupLog) {
		return;
	}

	char line[512]{};
	va_list args;
	va_start(args, format);
	wvsprintfA(line, format, args);
	va_end(args);
	AppendStartupLog(line);
}

// Avoid changing anything when the configured resolution is already advertised.
static bool IsConfiguredMode(const DisplayMode& mode) {
	return mode.Width == static_cast<UINT>(Client::m_nGameWidth) &&
		mode.Height == static_cast<UINT>(Client::m_nGameHeight);
}

static bool HasConfiguredMode(void* d3d8, UINT adapter, UINT realCount) {
	if (!s_enumAdapterModes) {
		return false;
	}

	for (UINT i = 0; i < realCount; ++i) {
		DisplayMode mode{};
		if (s_enumAdapterModes(d3d8, adapter, i, &mode) >= 0 && IsConfiguredMode(mode)) {
			return true;
		}
	}

	return false;
}

static DisplayMode MakeConfiguredMode(void* d3d8, UINT adapter, UINT realCount) {
	// Keep the synthetic mode at 60 Hz. Some cloud display adapters report odd
	// rates such as 64 Hz, but old Gr2D validation expects a conventional mode.
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
			configured.Format = mode.Format;
			break;
		}
	}

	return configured;
}

static UINT WINAPI GetAdapterModeCount_Hook(void* self, UINT adapter) {
	const UINT realCount = s_getAdapterModeCount(self, adapter);
	const bool hasConfiguredMode = HasConfiguredMode(self, adapter, realCount);
	AppendStartupLogF(
		"d3d8.GetAdapterModeCount adapter=%u real=%u final=%u configured=%dx%d hasConfigured=%s\r\n",
		adapter,
		realCount,
		hasConfiguredMode ? realCount : realCount + 1,
		Client::m_nGameWidth,
		Client::m_nGameHeight,
		hasConfiguredMode ? "true" : "false");
	return hasConfiguredMode ? realCount : realCount + 1;
}

static HRESULT WINAPI EnumAdapterModes_Hook(void* self, UINT adapter, UINT modeIndex, DisplayMode* mode) {
	const UINT realCount = s_getAdapterModeCount ? s_getAdapterModeCount(self, adapter) : 0;
	// The added count points the client at index realCount; only synthesize that
	// one slot and forward every real adapter mode unchanged.
	if (modeIndex == realCount && !HasConfiguredMode(self, adapter, realCount)) {
		if (mode) {
			*mode = MakeConfiguredMode(self, adapter, realCount);
			AppendStartupLogF(
				"d3d8.EnumAdapterModes synthetic adapter=%u index=%u mode=%ux%u rate=%u format=%d\r\n",
				adapter,
				modeIndex,
				mode->Width,
				mode->Height,
				mode->RefreshRate,
				mode->Format);
		}
		return S_OK;
	}

	const HRESULT hr = s_enumAdapterModes(self, adapter, modeIndex, mode);
	if (hr >= 0 && mode && IsConfiguredMode(*mode) && mode->RefreshRate != 60) {
		const UINT originalRefreshRate = mode->RefreshRate;
		mode->RefreshRate = 60;
		AppendStartupLogF(
			"d3d8.EnumAdapterModes normalized adapter=%u index=%u mode=%ux%u rate=%u->%u format=%d\r\n",
			adapter,
			modeIndex,
			mode->Width,
			mode->Height,
			originalRefreshRate,
			mode->RefreshRate,
			mode->Format);
	}
	return hr;
}

static HRESULT WINAPI GetAdapterDisplayMode_Hook(void* self, UINT adapter, DisplayMode* mode) {
	const HRESULT hr = s_getAdapterDisplayMode(self, adapter, mode);
	if (hr < 0 || mode) {
		AppendStartupLogF(
			"d3d8.GetAdapterDisplayMode adapter=%u hr=0x%08lX mode=%ux%u rate=%u format=%d\r\n",
			adapter,
			static_cast<unsigned long>(hr),
			mode ? mode->Width : 0,
			mode ? mode->Height : 0,
			mode ? mode->RefreshRate : 0,
			mode ? mode->Format : 0);
	}
	return hr;
}

static HRESULT WINAPI CheckDeviceType_Hook(void* self, UINT adapter, int deviceType, int adapterFormat, int backBufferFormat, BOOL windowed) {
	const HRESULT hr = s_checkDeviceType(self, adapter, deviceType, adapterFormat, backBufferFormat, windowed);
	AppendStartupLogF(
		"d3d8.CheckDeviceType adapter=%u type=%d adapterFormat=%d backBufferFormat=%d windowed=%d hr=0x%08lX\r\n",
		adapter,
		deviceType,
		adapterFormat,
		backBufferFormat,
		windowed,
		static_cast<unsigned long>(hr));
	return hr;
}

static HRESULT WINAPI CheckDeviceFormat_Hook(
	void* self,
	UINT adapter,
	int deviceType,
	int adapterFormat,
	DWORD usage,
	int resourceType,
	int checkFormat) {
	const HRESULT hr = s_checkDeviceFormat(self, adapter, deviceType, adapterFormat, usage, resourceType, checkFormat);
	if (!s_createDeviceStarted || hr < 0) {
		AppendStartupLogF(
			"d3d8.CheckDeviceFormat adapter=%u type=%d adapterFormat=%d usage=0x%08lX resourceType=%d checkFormat=%d hr=0x%08lX\r\n",
			adapter,
			deviceType,
			adapterFormat,
			static_cast<unsigned long>(usage),
			resourceType,
			checkFormat,
			static_cast<unsigned long>(hr));
	}
	return hr;
}

static HRESULT WINAPI GetDeviceCaps_Hook(void* self, UINT adapter, int deviceType, void* caps) {
	const HRESULT hr = s_getDeviceCaps(self, adapter, deviceType, caps);
	AppendStartupLogF(
		"d3d8.GetDeviceCaps adapter=%u type=%d hr=0x%08lX\r\n",
		adapter,
		deviceType,
		static_cast<unsigned long>(hr));
	return hr;
}

static HRESULT WINAPI CreateDevice_Hook(
	void* self,
	UINT adapter,
	int deviceType,
	HWND focusWindow,
	DWORD behaviorFlags,
	PresentParameters* presentationParameters,
	void** returnedDeviceInterface) {
	s_createDeviceStarted = true;
	if (presentationParameters) {
		AppendStartupLogF(
			"d3d8.CreateDevice begin adapter=%u type=%d focus=0x%08lX behavior=0x%08lX backBuffer=%ux%u format=%d count=%u windowed=%d refresh=%u interval=%u flags=0x%08lX\r\n",
			adapter,
			deviceType,
			reinterpret_cast<unsigned long>(focusWindow),
			static_cast<unsigned long>(behaviorFlags),
			presentationParameters->BackBufferWidth,
			presentationParameters->BackBufferHeight,
			presentationParameters->BackBufferFormat,
			presentationParameters->BackBufferCount,
			presentationParameters->Windowed,
			presentationParameters->FullScreen_RefreshRateInHz,
			presentationParameters->FullScreen_PresentationInterval,
			static_cast<unsigned long>(presentationParameters->Flags));
	}
	else {
		AppendStartupLogF(
			"d3d8.CreateDevice begin adapter=%u type=%d focus=0x%08lX behavior=0x%08lX present=null\r\n",
			adapter,
			deviceType,
			reinterpret_cast<unsigned long>(focusWindow),
			static_cast<unsigned long>(behaviorFlags));
	}

	const HRESULT hr = s_createDevice(
		self,
		adapter,
		deviceType,
		focusWindow,
		behaviorFlags,
		presentationParameters,
		returnedDeviceInterface);
	AppendStartupLogF(
		"d3d8.CreateDevice end hr=0x%08lX device=0x%08lX\r\n",
		static_cast<unsigned long>(hr),
		returnedDeviceInterface ? reinterpret_cast<unsigned long>(*returnedDeviceInterface) : 0);
	if (hr >= 0 && returnedDeviceInterface && *returnedDeviceInterface) {
		void** deviceVtable = *reinterpret_cast<void***>(*returnedDeviceInterface);
		if (deviceVtable && !s_endScene) {
			s_endScene = reinterpret_cast<EndScene_t>(deviceVtable[35]);
			Memory::SetHook(true, reinterpret_cast<void**>(&s_endScene), EndScene_Hook);
		}
	}
	return hr;
}

static HRESULT WINAPI EndScene_Hook(void* self) {
	StackedBuffIcons::DrawCountdownOverlay(self);
	return s_endScene(self);
}

static void InstallInterfaceHooks(void* d3d8) {
	if (!d3d8) {
		return;
	}

	// IDirect3D8 vtable: 6 = GetAdapterModeCount, 7 = EnumAdapterModes,
	// 8 = GetAdapterDisplayMode, 9 = CheckDeviceType, 10 = CheckDeviceFormat,
	// 13 = GetDeviceCaps, 15 = CreateDevice.
	void** vtable = *reinterpret_cast<void***>(d3d8);
	if (!s_getAdapterModeCount) {
		s_getAdapterModeCount = reinterpret_cast<GetAdapterModeCount_t>(vtable[6]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_getAdapterModeCount), GetAdapterModeCount_Hook);
	}
	if (!s_enumAdapterModes) {
		s_enumAdapterModes = reinterpret_cast<EnumAdapterModes_t>(vtable[7]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_enumAdapterModes), EnumAdapterModes_Hook);
	}
	if (!s_createDevice) {
		s_createDevice = reinterpret_cast<CreateDevice_t>(vtable[15]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_createDevice), CreateDevice_Hook);
	}
	// Mode count/enumeration hooks above are the compatibility fix. The hooks
	// below are diagnostics only, so keep them out of the normal startup path.
	if (!Client::enableStartupLog) {
		return;
	}
	if (!s_getAdapterDisplayMode) {
		s_getAdapterDisplayMode = reinterpret_cast<GetAdapterDisplayMode_t>(vtable[8]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_getAdapterDisplayMode), GetAdapterDisplayMode_Hook);
	}
	if (!s_checkDeviceType) {
		s_checkDeviceType = reinterpret_cast<CheckDeviceType_t>(vtable[9]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_checkDeviceType), CheckDeviceType_Hook);
	}
	if (!s_checkDeviceFormat) {
		s_checkDeviceFormat = reinterpret_cast<CheckDeviceFormat_t>(vtable[10]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_checkDeviceFormat), CheckDeviceFormat_Hook);
	}
	if (!s_getDeviceCaps) {
		s_getDeviceCaps = reinterpret_cast<GetDeviceCaps_t>(vtable[13]);
		Memory::SetHook(true, reinterpret_cast<void**>(&s_getDeviceCaps), GetDeviceCaps_Hook);
	}
}

static void* WINAPI Direct3DCreate8_Hook(UINT sdkVersion) {
	void* d3d8 = s_direct3DCreate8(sdkVersion);
	AppendStartupLogF("d3d8.Direct3DCreate8 sdk=%u result=0x%08lX\r\n", sdkVersion, reinterpret_cast<unsigned long>(d3d8));
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
		AppendStartupLog("d3d8.Install hooked Direct3DCreate8\r\n");
	}
	else {
		AppendStartupLog("d3d8.Install missing Direct3DCreate8\r\n");
	}
}

} // namespace D3D8DisplayModeHook
