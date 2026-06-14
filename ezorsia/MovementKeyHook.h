#pragma once

#include <cstring>

extern BYTE enabled;

namespace MovementKeyHook
{
	constexpr BYTE KEY_DOWN = 0x80;
	constexpr DWORD FUNC_KEY_MAPPED_MAN = 0x00BED5A0;
	constexpr BYTE FUNC_KEY_TYPE_ANY = 0;
	constexpr int KEYMAP_SIZE = 90;

	constexpr BYTE DIK_UP = 0xC8;
	constexpr BYTE DIK_LEFT = 0xCB;
	constexpr BYTE DIK_RIGHT = 0xCD;
	constexpr BYTE DIK_DOWN = 0xD0;

	constexpr BYTE SCAN_UP = 0x48;
	constexpr BYTE SCAN_LEFT = 0x4B;
	constexpr BYTE SCAN_RIGHT = 0x4D;
	constexpr BYTE SCAN_DOWN = 0x50;

	constexpr BYTE FUNC_KEY_TYPE = 4;
	constexpr DWORD ACTION_MOVE_UP = 28;
	constexpr DWORD ACTION_MOVE_DOWN = 29;
	constexpr DWORD ACTION_MOVE_LEFT = 30;
	constexpr DWORD ACTION_MOVE_RIGHT = 31;
	constexpr DWORD ACTION_MOVE_UP_ICON = 107;
	constexpr DWORD ACTION_MOVE_DOWN_ICON = 108;
	constexpr DWORD ACTION_MOVE_LEFT_ICON = 109;
	constexpr DWORD ACTION_MOVE_RIGHT_ICON = 110;

	const GUID GUID_SYS_KEYBOARD = {
		0x6F1D2B61, 0xD5A0, 0x11CF,
		{0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}
	};

	typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
	typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(void*, REFGUID, void**, LPUNKNOWN);
	typedef HRESULT(STDMETHODCALLTYPE* GetDeviceState_t)(void*, DWORD, LPVOID);
	typedef BOOL(WINAPI* PeekMessageA_t)(LPMSG, HWND, UINT, UINT, UINT);
	typedef BOOL(WINAPI* PeekMessageW_t)(LPMSG, HWND, UINT, UINT, UINT);
	typedef BOOL(WINAPI* GetMessageA_t)(LPMSG, HWND, UINT, UINT);
	typedef BOOL(WINAPI* GetMessageW_t)(LPMSG, HWND, UINT, UINT);

	static DirectInput8Create_t RealDirectInput8Create = nullptr;
	static CreateDevice_t RealCreateDevice = nullptr;
	static GetDeviceState_t RealGetDeviceState = nullptr;
	static PeekMessageA_t RealPeekMessageA = nullptr;
	static PeekMessageW_t RealPeekMessageW = nullptr;
	static GetMessageA_t RealGetMessageA = nullptr;
	static GetMessageW_t RealGetMessageW = nullptr;
	static HHOOK KeyboardHook = nullptr;
	static HHOOK MouseHook = nullptr;
	static BYTE LastEnabledState = 0;
	static DWORD LastInputFocusTick = 0;
	static DWORD LastMouseButtonDownTick = 0;

	static void DebugLog(const char* /*message*/)
	{
	}


	static void DebugLogState(const char* label, DWORD key, BYTE type, DWORD action, BYTE pressed)
	{
		char buffer[160];
		wsprintfA(buffer, "%s key=%lu type=%u action=%lu pressed=%u\r\n", label, key, type, action, pressed);
		DebugLog(buffer);
	}

	static bool GuidEquals(REFGUID lhs, const GUID& rhs)
	{
		return std::memcmp(&lhs, &rhs, sizeof(GUID)) == 0;
	}

	static bool PatchVTable(void* instance, int index, void* hook, void** original)
	{
		if (instance == nullptr) {
			return false;
		}

		void*** instanceVTable = reinterpret_cast<void***>(instance);
		void** vTable = *instanceVTable;
		if (vTable == nullptr || vTable[index] == hook) {
			return false;
		}

		if (original != nullptr && *original == nullptr) {
			*original = vTable[index];
		}

		DWORD oldProtect = 0;
		if (!VirtualProtect(&vTable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			return false;
		}

		vTable[index] = hook;

		DWORD unused = 0;
		VirtualProtect(&vTable[index], sizeof(void*), oldProtect, &unused);
		return true;
	}

	static bool SafeReadByte(DWORD address, BYTE* value)
	{
		__try {
			*value = *reinterpret_cast<BYTE*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	static bool SafeReadDword(DWORD address, DWORD* value)
	{
		__try {
			*value = *reinterpret_cast<DWORD*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	static bool ReadKeyBindingLayout(DWORD base, int key, int arrayOffset, int stride, int actionOffset, BYTE* type, DWORD* action)
	{
		const DWORD entry = base + arrayOffset + key * stride;
		return SafeReadByte(entry, type) && SafeReadDword(entry + actionOffset, action);
	}

	static bool IsMovementBinding(BYTE type, DWORD action)
	{
		return type != 0 &&
			(action == ACTION_MOVE_UP ||
				action == ACTION_MOVE_DOWN ||
				action == ACTION_MOVE_LEFT ||
				action == ACTION_MOVE_RIGHT ||
				action == ACTION_MOVE_UP_ICON ||
				action == ACTION_MOVE_DOWN_ICON ||
				action == ACTION_MOVE_LEFT_ICON ||
				action == ACTION_MOVE_RIGHT_ICON);
	}

	static bool ReadKeyBindingFromBase(DWORD base, int key, BYTE* type, DWORD* action)
	{
		if (base == 0) {
			return false;
		}

		return ReadKeyBindingLayout(base, key, 4, 5, 1, type, action) && IsMovementBinding(*type, *action);
	}

	static bool ReadKeyBinding(int key, BYTE* type, DWORD* action)
	{
		if (key < 0 || key >= KEYMAP_SIZE) {
			return false;
		}

		DWORD keyMapBase = 0;
		return SafeReadDword(FUNC_KEY_MAPPED_MAN, &keyMapBase) &&
			ReadKeyBindingFromBase(keyMapBase, key, type, action);
	}

	static BYTE FindBoundKey(DWORD action)
	{
		for (int key = 0; key < KEYMAP_SIZE; ++key) {
			BYTE type = 0;
			DWORD currentAction = 0;
			if (ReadKeyBinding(key, &type, &currentAction) && type != 0 && currentAction == action) {
				return static_cast<BYTE>(key);
			}
		}
		return 0;
	}

	static void ApplyMovementKeys(BYTE* keys, DWORD cbData)
	{
		if (keys == nullptr || cbData <= DIK_DOWN) {
			return;
		}

		BYTE up = 0;
		BYTE down = 0;
		BYTE left = 0;
		BYTE right = 0;
		up = FindBoundKey(ACTION_MOVE_UP);
		if (up == 0) {
			up = FindBoundKey(ACTION_MOVE_UP_ICON);
		}
		down = FindBoundKey(ACTION_MOVE_DOWN);
		if (down == 0) {
			down = FindBoundKey(ACTION_MOVE_DOWN_ICON);
		}
		left = FindBoundKey(ACTION_MOVE_LEFT);
		if (left == 0) {
			left = FindBoundKey(ACTION_MOVE_LEFT_ICON);
		}
		right = FindBoundKey(ACTION_MOVE_RIGHT);
		if (right == 0) {
			right = FindBoundKey(ACTION_MOVE_RIGHT_ICON);
		}

		auto mapKey = [&](BYTE source, BYTE target) {
			if (source != 0 && source < cbData) {
				BYTE type = 0;
				DWORD action = 0;
				ReadKeyBinding(source, &type, &action);
				DebugLogState("dinput", source, type, action, (keys[source] & KEY_DOWN) != 0);
			}
			if (source != 0 && source < cbData && (keys[source] & KEY_DOWN) != 0) {
				keys[target] |= KEY_DOWN;
				if (source != target) {
					keys[source] &= ~KEY_DOWN;
				}
			}
		};

		mapKey(up, DIK_UP);
		mapKey(down, DIK_DOWN);
		mapKey(left, DIK_LEFT);
		mapKey(right, DIK_RIGHT);
	}

	static bool TryGetMovementTarget(BYTE source, BYTE* targetScan, WPARAM* targetVk)
	{
		if (source == 0) {
			return false;
		}

		if (source == FindBoundKey(ACTION_MOVE_UP) || source == FindBoundKey(ACTION_MOVE_UP_ICON)) {
			*targetScan = SCAN_UP;
			*targetVk = VK_UP;
			return true;
		}
		if (source == FindBoundKey(ACTION_MOVE_DOWN) || source == FindBoundKey(ACTION_MOVE_DOWN_ICON)) {
			*targetScan = SCAN_DOWN;
			*targetVk = VK_DOWN;
			return true;
		}
		if (source == FindBoundKey(ACTION_MOVE_LEFT) || source == FindBoundKey(ACTION_MOVE_LEFT_ICON)) {
			*targetScan = SCAN_LEFT;
			*targetVk = VK_LEFT;
			return true;
		}
		if (source == FindBoundKey(ACTION_MOVE_RIGHT) || source == FindBoundKey(ACTION_MOVE_RIGHT_ICON)) {
			*targetScan = SCAN_RIGHT;
			*targetVk = VK_RIGHT;
			return true;
		}

		return false;
	}

	static bool IsCurrentProcessForeground()
	{
		const HWND foreground = GetForegroundWindow();
		if (foreground == nullptr) {
			return false;
		}

		DWORD processId = 0;
		GetWindowThreadProcessId(foreground, &processId);
		return processId == GetCurrentProcessId();
	}

	static bool IsCurrentProcessWindow(HWND hwnd)
	{
		if (hwnd == nullptr) {
			return false;
		}

		DWORD processId = 0;
		GetWindowThreadProcessId(hwnd, &processId);
		return processId == GetCurrentProcessId();
	}

	static bool IsTextCaretActive()
	{
		const HWND foreground = GetForegroundWindow();
		if (foreground == nullptr) {
			return false;
		}

		DWORD processId = 0;
		const DWORD threadId = GetWindowThreadProcessId(foreground, &processId);
		if (processId != GetCurrentProcessId() || threadId == 0) {
			return false;
		}

		GUITHREADINFO info = {};
		info.cbSize = sizeof(info);
		return GetGUIThreadInfo(threadId, &info) &&
			info.hwndCaret != nullptr &&
			!IsRectEmpty(&info.rcCaret);
	}

	static void TrackInputFocusState()
	{
		if (::enabled == LastEnabledState) {
			return;
		}

		LastEnabledState = ::enabled;
		if (::enabled != 0) {
			LastInputFocusTick = GetTickCount();
		}
	}

	static bool HasModifierKeyDown()
	{
		return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
			(GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
			(GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
			(GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
	}

	static bool ShouldBypassMovementRemap()
	{
		if (HasModifierKeyDown()) {
			return true;
		}

		TrackInputFocusState();
		if (::enabled == 0) {
			return false;
		}

		if (LastMouseButtonDownTick > LastInputFocusTick) {
			return false;
		}

		return true;
	}

	static bool IsMouseButtonMessage(WPARAM wParam)
	{
		return wParam == WM_LBUTTONDOWN ||
			wParam == WM_RBUTTONDOWN ||
			wParam == WM_MBUTTONDOWN ||
			wParam == WM_XBUTTONDOWN;
	}

	static void ObserveQueuedMessage(const MSG* message)
	{
		if (message == nullptr || !IsMouseButtonMessage(message->message)) {
			return;
		}

		if (IsCurrentProcessForeground() || IsCurrentProcessWindow(message->hwnd)) {
			LastMouseButtonDownTick = GetTickCount();
		}
	}

	static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HC_ACTION && lParam != 0 && IsMouseButtonMessage(wParam)) {
			const MSLLHOOKSTRUCT* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
			if (IsCurrentProcessForeground() || IsCurrentProcessWindow(WindowFromPoint(mouse->pt))) {
				LastMouseButtonDownTick = GetTickCount();
			}
		}

		return CallNextHookEx(MouseHook, code, wParam, lParam);
	}

	static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HC_ACTION && lParam != 0 && IsCurrentProcessForeground()) {
			const KBDLLHOOKSTRUCT* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);

			if (ShouldBypassMovementRemap()) {
				return CallNextHookEx(KeyboardHook, code, wParam, lParam);
			}

			if ((keyboard->flags & LLKHF_INJECTED) == 0) {
				BYTE sourceScan = static_cast<BYTE>(keyboard->scanCode & 0xFF);
				if (sourceScan == 0x36) {
					sourceScan = 0x2A;
				}

				BYTE targetScan = 0;
				WPARAM targetVk = 0;
				if (TryGetMovementTarget(sourceScan, &targetScan, &targetVk)) {
					const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
					keybd_event(static_cast<BYTE>(targetVk), targetScan, keyUp ? KEYEVENTF_KEYUP : 0, 0);
					return 1;
				}
			}
		}

		return CallNextHookEx(KeyboardHook, code, wParam, lParam);
	}

	static bool IsKeyboardMessage(UINT message)
	{
		return message == WM_KEYDOWN ||
			message == WM_KEYUP ||
			message == WM_SYSKEYDOWN ||
			message == WM_SYSKEYUP;
	}

	static void ApplyMovementMessage(MSG* message)
	{
		if (message == nullptr || !IsKeyboardMessage(message->message)) {
			return;
		}

		BYTE sourceScan = static_cast<BYTE>((message->lParam >> 16) & 0xFF);
		if (sourceScan == 0x36) {
			sourceScan = 0x2A;
		}

		BYTE targetScan = 0;
		WPARAM targetVk = 0;
		if (!TryGetMovementTarget(sourceScan, &targetScan, &targetVk)) {
			return;
		}

		message->wParam = targetVk;
		message->lParam &= ~static_cast<LPARAM>((0xFF << 16) | (1 << 24));
		message->lParam |= static_cast<LPARAM>(targetScan) << 16;
		message->lParam |= static_cast<LPARAM>(1) << 24;
	}

	static HRESULT STDMETHODCALLTYPE GetDeviceState_Hook(void* device, DWORD cbData, LPVOID lpvData)
	{
		const HRESULT hr = RealGetDeviceState(device, cbData, lpvData);
		if (SUCCEEDED(hr) && cbData >= 256) {
			ApplyMovementKeys(reinterpret_cast<BYTE*>(lpvData), cbData);
		}
		return hr;
	}

	static HRESULT STDMETHODCALLTYPE CreateDevice_Hook(void* directInput, REFGUID rguid, void** device, LPUNKNOWN outer)
	{
		const HRESULT hr = RealCreateDevice(directInput, rguid, device, outer);
		if (SUCCEEDED(hr) && device != nullptr && *device != nullptr && GuidEquals(rguid, GUID_SYS_KEYBOARD)) {
			PatchVTable(*device, 9, reinterpret_cast<void*>(GetDeviceState_Hook), reinterpret_cast<void**>(&RealGetDeviceState));
		}
		return hr;
	}

	static HRESULT WINAPI DirectInput8Create_Hook(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer)
	{
		const HRESULT hr = RealDirectInput8Create(instance, version, iid, out, outer);
		if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
			PatchVTable(*out, 3, reinterpret_cast<void*>(CreateDevice_Hook), reinterpret_cast<void**>(&RealCreateDevice));
		}
		return hr;
	}

	static BOOL WINAPI PeekMessageA_Hook(LPMSG message, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax, UINT removeMsg)
	{
		const BOOL result = RealPeekMessageA(message, hwnd, messageFilterMin, messageFilterMax, removeMsg);
		if (result) {
			ObserveQueuedMessage(message);
		}
		return result;
	}

	static BOOL WINAPI PeekMessageW_Hook(LPMSG message, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax, UINT removeMsg)
	{
		const BOOL result = RealPeekMessageW(message, hwnd, messageFilterMin, messageFilterMax, removeMsg);
		if (result) {
			ObserveQueuedMessage(message);
		}
		return result;
	}

	static BOOL WINAPI GetMessageA_Hook(LPMSG message, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax)
	{
		const BOOL result = RealGetMessageA(message, hwnd, messageFilterMin, messageFilterMax);
		if (result > 0) {
			ObserveQueuedMessage(message);
		}
		return result;
	}

	static BOOL WINAPI GetMessageW_Hook(LPMSG message, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax)
	{
		const BOOL result = RealGetMessageW(message, hwnd, messageFilterMin, messageFilterMax);
		if (result > 0) {
			ObserveQueuedMessage(message);
		}
		return result;
	}

	static bool Hook(bool enable)
	{
		if (RealDirectInput8Create == nullptr) {
			RealDirectInput8Create = reinterpret_cast<DirectInput8Create_t>(GetFuncAddress("dinput8", "DirectInput8Create"));
		}
		if (RealPeekMessageA == nullptr) {
			RealPeekMessageA = reinterpret_cast<PeekMessageA_t>(GetFuncAddress("user32", "PeekMessageA"));
		}
		if (RealPeekMessageW == nullptr) {
			RealPeekMessageW = reinterpret_cast<PeekMessageW_t>(GetFuncAddress("user32", "PeekMessageW"));
		}
		if (RealGetMessageA == nullptr) {
			RealGetMessageA = reinterpret_cast<GetMessageA_t>(GetFuncAddress("user32", "GetMessageA"));
		}
		if (RealGetMessageW == nullptr) {
			RealGetMessageW = reinterpret_cast<GetMessageW_t>(GetFuncAddress("user32", "GetMessageW"));
		}
		// Movement remapping is handled by the low-level keyboard hook below.
		// Queued messages are only observed for mouse focus changes, never rewritten.

		bool hooked = false;
		if (RealDirectInput8Create != nullptr) {
			const bool result = Memory::SetHook(enable, reinterpret_cast<void**>(&RealDirectInput8Create), DirectInput8Create_Hook);
			hooked |= result;
			DebugLog(result ? "hook DirectInput8Create ok\r\n" : "hook DirectInput8Create failed\r\n");
		}
		if (RealPeekMessageA != nullptr) {
			hooked |= Memory::SetHook(enable, reinterpret_cast<void**>(&RealPeekMessageA), PeekMessageA_Hook);
		}
		if (RealPeekMessageW != nullptr) {
			hooked |= Memory::SetHook(enable, reinterpret_cast<void**>(&RealPeekMessageW), PeekMessageW_Hook);
		}
		if (RealGetMessageA != nullptr) {
			hooked |= Memory::SetHook(enable, reinterpret_cast<void**>(&RealGetMessageA), GetMessageA_Hook);
		}
		if (RealGetMessageW != nullptr) {
			hooked |= Memory::SetHook(enable, reinterpret_cast<void**>(&RealGetMessageW), GetMessageW_Hook);
		}
		if (enable && KeyboardHook == nullptr) {
			HMODULE module = GetModuleHandleA("ijl15.dll");
			KeyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, LowLevelKeyboardProc, module, 0);
			DebugLog(KeyboardHook != nullptr ? "hook keyboard ok\r\n" : "hook keyboard failed\r\n");
			hooked |= KeyboardHook != nullptr;
		}
		if (enable && MouseHook == nullptr) {
			HMODULE module = GetModuleHandleA("ijl15.dll");
			MouseHook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, module, 0);
			hooked |= MouseHook != nullptr;
		}
		if (!enable && KeyboardHook != nullptr) {
			UnhookWindowsHookEx(KeyboardHook);
			KeyboardHook = nullptr;
		}
		if (!enable && MouseHook != nullptr) {
			UnhookWindowsHookEx(MouseHook);
			MouseHook = nullptr;
		}
		return hooked;
	}
}
