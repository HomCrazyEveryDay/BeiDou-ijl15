#include "D3D8Batch.h"
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// 槽位来自 mingw-w64 d3d8.h 的 IDirect3DDevice8 / Texture8 / Surface8 / VertexBuffer8 声明。
// 工程不依赖 d3d8.h，用到的结构按字节大小复制。
//
// 适用范围：只在创建设备的线程上合批；只延迟通过下列检查的状态调用（这些调用在 wined3d 中
// 原本返回 D3D_OK），其余先提交再直接调用并返回真实结果。视口不延迟（可能因越界失败）；
// 顶点格式首次出现时直接调用，成功后才允许延迟。被延迟的 DrawPrimitiveUP 返回 D3D_OK，
// 实际执行失败只计入统计并退回原路径，无法回传给原调用者。其他线程、状态块录制期间、
// 绑定了立方体/体积贴图时都不合批。
namespace D3D8Batch {
namespace {

enum DeviceSlot : unsigned {
	kQueryInterface = 0, kAddRef = 1, kRelease = 2, kReset = 14, kPresent = 15, kCreateTexture = 20,
	kGetDeviceCaps = 7, kCreateVertexBuffer = 23, kSetTransform = 37, kSetViewport = 40, kSetMaterial = 42, kSetClipPlane = 48,
	kSetRenderState = 50, kBeginStateBlock = 52, kEndStateBlock = 53, kApplyStateBlock = 54, kGetTexture = 60,
	kSetTexture = 61, kSetTextureStageState = 63,
	kDrawPrimitive = 70, kDrawPrimitiveUP = 72, kSetVertexShader = 76, kSetStreamSource = 83, kDeviceSlots = 97
};
enum : unsigned {
	kResourceGetType = 10, kTextureSetLOD = 11, kTextureGetSurfaceLevel = 15, kTextureLockRect = 16,
	kSurfaceLockRect = 9, kBufferLock = 11, kBufferUnlock = 12
};
enum : DWORD {
	kUsageWriteOnly = 0x8, kUsageDynamic = 0x200, kPoolDefault = 0, kPoolManaged = 1, kFormatA8R8G8B8 = 21,
	kLockDiscard = 0x2000, kResourceTexture = 3, kMaxStages = 8, kMaxRenderState = 174, kMaxStageState = 28,
	kFvfReserved0 = 0x1, kCapsMaxUserClipPlanes = 0xA4 / 4, kCapsDwords = 53
};
// 单次绘制过大时直接走原路径；暂存超过上限时提前提交。
const unsigned long long kMaxDrawBytes = 256 * 1024;
const size_t kMaxBatchBytes = 2 * 1024 * 1024;
const size_t kMaxCommands = 16384;
const size_t kMinBufferBytes = 512 * 1024;

enum class Kind : BYTE { Draw, Transform, Viewport, Material, ClipPlane, RenderState, Texture, StageState, VertexShader };
struct Command {
	Kind kind;
	DWORD a, b, c, d; // Draw：a 类型，b 图元数，c 字节偏移，d 字节数
	union { float f[17]; DWORD dw[17]; void* p; };
};

CRITICAL_SECTION s_lock;
volatile LONG s_lockState = 0; // 0 未初始化，1 初始化中，2 可用
void* s_device = nullptr;
DWORD s_ownerThread = 0;
bool s_bypass = false;
bool s_flushing = false;
bool s_stateBlock = false;       // 应用正在录制状态块
bool s_releasingBuffer = false;  // 自建缓冲区释放时 Wine 会回调设备 Release
DWORD s_non2dStages = 0;         // 绑定了立方体/体积贴图的纹理阶段
DWORD s_maxClipPlanes = 0;
std::vector<DWORD> s_knownFvf;   // 已直接调用成功过的 FVF
void* s_original[kDeviceSlots]{};
void* s_textureLockRect = nullptr;
void* s_textureSetLOD = nullptr;
void* s_surfaceLockRect = nullptr;
BYTE* s_thunks = nullptr;
void** s_hookedTable = nullptr;
bool s_installBroken = false; // 回滚不完整：不再接管任何设备
bool s_hooksBroken = false;   // 设备虚表被他方替换/改写（原生 d3d8 状态块录制会这样做）：停用到下次 Attach
void* s_expected[kDeviceSlots]{};
void(*s_logger)(const char*) = nullptr;
std::vector<Command> s_commands;
std::vector<BYTE> s_vertices;
void* s_buffer = nullptr;
size_t s_bufferBytes = 0;
LONG s_bufferDeviceRefs = 0;
unsigned long long s_draws = 0, s_flushes = 0, s_fallbacks = 0, s_frames = 0, s_invalid = 0;
bool s_statsLogged = false;

template<class F> F Original(unsigned slot) { return reinterpret_cast<F>(s_original[slot]); }
template<class F> F Method(void* object, unsigned slot) { return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[slot]); }
ULONG ReleaseObject(void* object) { return Method<ULONG(__stdcall*)(void*)>(object, 2)(object); }

void Log(const char* format, ...) {
	if (!s_logger) return;
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	s_logger(line);
}

void InitializeLock() {
	if (InterlockedCompareExchange(&s_lockState, 1, 0) == 0) {
		InitializeCriticalSection(&s_lock);
		InterlockedExchange(&s_lockState, 2);
	}
	while (s_lockState != 2) Sleep(0);
}

struct Guard {
	Guard() { EnterCriticalSection(&s_lock); }
	~Guard() { LeaveCriticalSection(&s_lock); }
};

ULONG DeviceRefCount(void* device) {
	Original<ULONG(__stdcall*)(void*)>(kAddRef)(device);
	return Original<ULONG(__stdcall*)(void*)>(kRelease)(device);
}

void ReleaseBuffer() {
	if (!s_buffer) return;
	void* buffer = s_buffer;
	s_buffer = nullptr;
	s_bufferBytes = 0;
	s_bufferDeviceRefs = 0;
	s_releasingBuffer = true;
	ReleaseObject(buffer);
	s_releasingBuffer = false;
}

bool EnsureBuffer(size_t bytes) {
	if (s_buffer && s_bufferBytes >= bytes) return true;
	ReleaseBuffer();
	size_t capacity = kMinBufferBytes;
	while (capacity < bytes) capacity *= 2;
	const ULONG before = DeviceRefCount(s_device);
	void* buffer = nullptr;
	const HRESULT hr = Original<HRESULT(__stdcall*)(void*, UINT, DWORD, DWORD, DWORD, void**)>(kCreateVertexBuffer)(
		s_device, static_cast<UINT>(capacity), kUsageDynamic | kUsageWriteOnly, 0, kPoolDefault, &buffer);
	if (FAILED(hr) || !buffer) {
		Log("event=d3d8_batch buffer_create_failed hr=0x%08lX bytes=%u", static_cast<unsigned long>(hr), static_cast<unsigned>(capacity));
		return false;
	}
	s_buffer = buffer;
	s_bufferBytes = capacity;
	s_bufferDeviceRefs = static_cast<LONG>(DeviceRefCount(s_device)) - static_cast<LONG>(before);
	return true;
}

bool Upload() {
	if (!EnsureBuffer(s_vertices.size())) return false;
	BYTE* target = nullptr;
	if (FAILED(Method<HRESULT(__stdcall*)(void*, UINT, UINT, BYTE**, DWORD)>(s_buffer, kBufferLock)(
		s_buffer, 0, static_cast<UINT>(s_vertices.size()), &target, kLockDiscard)) || !target) {
		return false;
	}
	memcpy(target, s_vertices.data(), s_vertices.size());
	return SUCCEEDED(Method<HRESULT(__stdcall*)(void*)>(s_buffer, kBufferUnlock)(s_buffer));
}

HRESULT DrawUp(const Command& command) {
	return Original<HRESULT(__stdcall*)(void*, DWORD, UINT, const void*, UINT)>(kDrawPrimitiveUP)(
		s_device, command.a, command.b, s_vertices.data() + command.c, command.dw[0]);
}

HRESULT BindStream(void* buffer, UINT stride) {
	return Original<HRESULT(__stdcall*)(void*, UINT, void*, UINT)>(kSetStreamSource)(s_device, 0, buffer, stride);
}

// 按记录顺序重放。顶点一次上传；上传、绑定或某次绘制失败后，剩余绘制退回原 DrawPrimitiveUP。
void Flush() {
	if (s_flushing || s_commands.empty()) return;
	s_flushing = true;
	bool buffered = Upload();
	if (!buffered) ++s_fallbacks;
	bool streamBound = false;
	DWORD stride = 0;
	for (Command& command : s_commands) {
		switch (command.kind) {
		case Kind::Draw:
			// 防御：绘制引用的顶点必须在本批暂存范围内，否则不执行并计数。
			if (static_cast<unsigned long long>(command.c) + command.d > s_vertices.size()) {
				++s_invalid;
				break;
			}
			if (buffered && command.dw[0] != stride) {
				if (SUCCEEDED(BindStream(s_buffer, command.dw[0]))) {
					stride = command.dw[0];
					streamBound = true;
				} else {
					buffered = false;
					++s_fallbacks;
				}
			}
			if (buffered && SUCCEEDED(Original<HRESULT(__stdcall*)(void*, DWORD, UINT, UINT)>(kDrawPrimitive)(
				s_device, command.a, command.c / command.dw[0], command.b))) {
				break;
			}
			if (buffered) {
				buffered = false;
				++s_fallbacks;
			}
			DrawUp(command);
			streamBound = false; // DrawPrimitiveUP 会清空流 0。
			break;
		case Kind::Transform:
			Original<HRESULT(__stdcall*)(void*, DWORD, const void*)>(kSetTransform)(s_device, command.a, command.f);
			break;
		case Kind::Viewport:
			Original<HRESULT(__stdcall*)(void*, const void*)>(kSetViewport)(s_device, command.dw);
			break;
		case Kind::Material:
			Original<HRESULT(__stdcall*)(void*, const void*)>(kSetMaterial)(s_device, command.f);
			break;
		case Kind::ClipPlane:
			Original<HRESULT(__stdcall*)(void*, DWORD, const float*)>(kSetClipPlane)(s_device, command.a, command.f);
			break;
		case Kind::RenderState:
			Original<HRESULT(__stdcall*)(void*, DWORD, DWORD)>(kSetRenderState)(s_device, command.a, command.b);
			break;
		case Kind::Texture:
			Original<HRESULT(__stdcall*)(void*, DWORD, void*)>(kSetTexture)(s_device, command.a, command.p);
			if (command.p) ReleaseObject(command.p);
			break;
		case Kind::StageState:
			Original<HRESULT(__stdcall*)(void*, DWORD, DWORD, DWORD)>(kSetTextureStageState)(s_device, command.a, command.b, command.c);
			break;
		case Kind::VertexShader:
			Original<HRESULT(__stdcall*)(void*, DWORD)>(kSetVertexShader)(s_device, command.a);
			break;
		}
	}
	// 与 DrawPrimitiveUP 语义一致：结束后流 0 为空；解绑失败则停用合批，避免内部缓冲区留在流 0。
	if (streamBound && FAILED(BindStream(nullptr, 0))) {
		Log("event=d3d8_batch unbind_failed disabled=1");
		s_bypass = true;
	}
	s_commands.clear();
	s_vertices.clear();
	++s_flushes;
	s_flushing = false;
}

// 只在锁内调用：本线程、未停用、无状态块、无非 2D 贴图时才合批。
bool CanBatch(void* device) {
	return device == s_device && s_device && !s_bypass && !s_hooksBroken && !s_flushing && !s_stateBlock &&
		!s_non2dStages && GetCurrentThreadId() == s_ownerThread;
}

// 暂存绘制依赖全部钩子仍在：任一状态调用绕过记录直接生效都会打乱顺序。
bool HooksIntact(void* device) {
	void** table = *reinterpret_cast<void***>(device);
	if (table != s_hookedTable) return false;
	for (unsigned slot = 0; slot < kDeviceSlots; ++slot)
		if (s_expected[slot] && table[slot] != s_expected[slot]) return false;
	return true;
}

bool Recording(void* device) { return CanBatch(device) && !s_commands.empty(); }

void FlushDevice(void* device) {
	if (s_lockState != 2) return;
	Guard guard;
	if (device == s_device && s_device) Flush();
}

// 资源钩子不知道所属设备：在锁内读取当前接管设备并提交。
void FlushCurrent() {
	if (s_lockState != 2) return;
	Guard guard;
	if (s_device) Flush();
}

void __stdcall FlushFromThunk(void* device) { FlushDevice(device); }

// 追加一条命令；分配失败时先提交已暂存内容并返回 false，由调用方直接调用原方法。
// 状态命令可在此处提交（自身不引用暂存顶点）；绘制命令已在写入顶点前处理容量。
bool Append(const Command& command) {
	if (command.kind != Kind::Draw && s_commands.size() >= kMaxCommands) Flush();
	try {
		s_commands.push_back(command);
		return true;
	} catch (const std::bad_alloc&) {
		Flush();
		return false;
	}
}

UINT VertexCount(DWORD type, UINT primitives) {
	switch (type) {
	case 1: return primitives;          // POINTLIST
	case 2: return primitives * 2;      // LINELIST
	case 3: return primitives + 1;      // LINESTRIP
	case 4: return primitives * 3;      // TRIANGLELIST
	case 5: case 6: return primitives + 2; // TRIANGLESTRIP / TRIANGLEFAN
	default: return 0;
	}
}

HRESULT __stdcall DrawPrimitiveUP_Hook(void* device, DWORD type, UINT primitives, const void* data, UINT stride) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD, UINT, const void*, UINT);
	if (s_lockState != 2) return Original<Fn>(kDrawPrimitiveUP)(device, type, primitives, data, stride);
	Guard guard;
	if (device != s_device || !s_device) return Original<Fn>(kDrawPrimitiveUP)(device, type, primitives, data, stride);
	if (!s_hooksBroken && !HooksIntact(device)) {
		Flush();
		s_hooksBroken = true;
		Log("event=d3d8_batch disabled=vtable_changed");
	}
	// 先按 64 位计算，避免极端参数溢出后绕过容量判断。
	const unsigned long long vertices = primitives > 0x3fffffffu ? 0 : VertexCount(type, primitives);
	const unsigned long long bytes = vertices * stride;
	if (!CanBatch(device) || !data || !stride || !vertices || bytes > kMaxDrawBytes) {
		Flush();
		return Original<Fn>(kDrawPrimitiveUP)(device, type, primitives, data, stride);
	}
	// 先处理命令与字节容量，再计算偏移：提交会清空暂存，必须发生在写入本次顶点之前。
	if (s_commands.size() + 1 >= kMaxCommands) Flush();
	size_t offset = (s_vertices.size() + stride - 1) / stride * stride;
	if (offset + bytes > kMaxBatchBytes) {
		Flush();
		offset = 0;
	}
	Command command{};
	command.kind = Kind::Draw;
	command.a = type;
	command.b = primitives;
	command.c = static_cast<DWORD>(offset);
	command.d = static_cast<DWORD>(bytes);
	command.dw[0] = stride;
	try {
		s_vertices.resize(offset + static_cast<size_t>(bytes));
	} catch (const std::bad_alloc&) {
		Flush();
		return Original<Fn>(kDrawPrimitiveUP)(device, type, primitives, data, stride);
	}
	memcpy(s_vertices.data() + offset, data, static_cast<size_t>(bytes));
	if (!Append(command)) return Original<Fn>(kDrawPrimitiveUP)(device, type, primitives, data, stride);
	++s_draws;
	return S_OK;
}

// 有待提交的绘制且参数合法时记录状态；否则先提交，再直接调用并返回真实结果。
#define RECORD_OR_CALL(slot, valid, call) \
	if (s_lockState != 2) return Original<Fn>(slot) call; \
	Guard guard; \
	if (!Recording(device) || !(valid)) { \
		if (device == s_device && s_device) Flush(); \
		return Original<Fn>(slot) call; \
	} \
	Command command{};

bool ValidTransform(DWORD state) { return state == 2 || state == 3 || (state >= 16 && state <= 23) || (state >= 256 && state <= 511); }

HRESULT __stdcall SetTransform_Hook(void* device, DWORD state, const float* matrix) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD, const float*);
	RECORD_OR_CALL(kSetTransform, matrix && ValidTransform(state), (device, state, matrix))
	command.kind = Kind::Transform; command.a = state; memcpy(command.f, matrix, 16 * sizeof(float));
	return Append(command) ? S_OK : Original<Fn>(kSetTransform)(device, state, matrix);
}

HRESULT __stdcall SetMaterial_Hook(void* device, const float* material) {
	typedef HRESULT(__stdcall* Fn)(void*, const float*);
	RECORD_OR_CALL(kSetMaterial, material != nullptr, (device, material))
	command.kind = Kind::Material; memcpy(command.f, material, 17 * sizeof(float));
	return Append(command) ? S_OK : Original<Fn>(kSetMaterial)(device, material);
}

HRESULT __stdcall SetClipPlane_Hook(void* device, DWORD index, const float* plane) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD, const float*);
	RECORD_OR_CALL(kSetClipPlane, plane && index < s_maxClipPlanes, (device, index, plane))
	command.kind = Kind::ClipPlane; command.a = index; memcpy(command.f, plane, 4 * sizeof(float));
	return Append(command) ? S_OK : Original<Fn>(kSetClipPlane)(device, index, plane);
}

HRESULT __stdcall SetRenderState_Hook(void* device, DWORD state, DWORD value) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD, DWORD);
	RECORD_OR_CALL(kSetRenderState, state && state <= kMaxRenderState, (device, state, value))
	command.kind = Kind::RenderState; command.a = state; command.b = value;
	return Append(command) ? S_OK : Original<Fn>(kSetRenderState)(device, state, value);
}

HRESULT __stdcall SetTextureStageState_Hook(void* device, DWORD stage, DWORD type, DWORD value) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD, DWORD, DWORD);
	RECORD_OR_CALL(kSetTextureStageState, stage < kMaxStages && type && type <= kMaxStageState, (device, stage, type, value))
	command.kind = Kind::StageState; command.a = stage; command.b = type; command.c = value;
	return Append(command) ? S_OK : Original<Fn>(kSetTextureStageState)(device, stage, type, value);
}

bool KnownFvf(DWORD handle) {
	for (DWORD known : s_knownFvf) if (known == handle) return true;
	return false;
}

HRESULT __stdcall SetVertexShader_Hook(void* device, DWORD handle) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD);
	if (s_lockState != 2) return Original<Fn>(kSetVertexShader)(device, handle);
	Guard guard;
	// 只延迟已成功设置过的 FVF 码；首次出现或可编程着色器句柄（最低位为 1）直接调用。
	const bool fvf = handle && !(handle & kFvfReserved0);
	if (!Recording(device) || !fvf || !KnownFvf(handle)) {
		const bool mine = device == s_device && s_device;
		if (mine) Flush();
		const HRESULT hr = Original<Fn>(kSetVertexShader)(device, handle);
		if (mine && SUCCEEDED(hr) && fvf && !KnownFvf(handle)) {
			try { s_knownFvf.push_back(handle); } catch (const std::bad_alloc&) {}
		}
		return hr;
	}
	Command command{};
	command.kind = Kind::VertexShader; command.a = handle;
	return Append(command) ? S_OK : Original<Fn>(kSetVertexShader)(device, handle);
}
#undef RECORD_OR_CALL

bool IsPlainTexture(void* texture) {
	return !texture || Method<DWORD(__stdcall*)(void*)>(texture, kResourceGetType)(texture) == kResourceTexture;
}

// 用真实绑定重算非 2D 贴图阶段（状态块结束/应用、Reset 后调用）。
void ResyncTextureStages() {
	s_non2dStages = 0;
	for (DWORD stage = 0; stage < kMaxStages; ++stage) {
		void* texture = nullptr;
		if (FAILED(Original<HRESULT(__stdcall*)(void*, DWORD, void**)>(kGetTexture)(s_device, stage, &texture)) || !texture) continue;
		if (!IsPlainTexture(texture)) s_non2dStages |= 1u << stage;
		ReleaseObject(texture);
	}
}

// 立方体/体积贴图的加锁接口未挂接：绑定期间停用合批，保证其内容修改前已画完。
HRESULT __stdcall SetTexture_Hook(void* device, DWORD stage, void* texture) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD, void*);
	if (s_lockState != 2) return Original<Fn>(kSetTexture)(device, stage, texture);
	Guard guard;
	if (device != s_device || !s_device) return Original<Fn>(kSetTexture)(device, stage, texture);
	const bool plain = stage < kMaxStages && IsPlainTexture(texture);
	if (!Recording(device) || !plain) {
		Flush();
		const HRESULT hr = Original<Fn>(kSetTexture)(device, stage, texture);
		// 状态块录制期间的设置不改变当前绑定；结束或应用后统一重算。
		if (SUCCEEDED(hr) && stage < kMaxStages && !s_stateBlock) {
			if (plain) s_non2dStages &= ~(1u << stage);
			else s_non2dStages |= 1u << stage;
		}
		return hr;
	}
	Command command{};
	command.kind = Kind::Texture; command.a = stage; command.p = texture;
	if (texture) Method<ULONG(__stdcall*)(void*)>(texture, 1)(texture);
	if (Append(command)) {
		s_non2dStages &= ~(1u << stage);
		return S_OK;
	}
	if (texture) ReleaseObject(texture);
	return Original<Fn>(kSetTexture)(device, stage, texture);
}

// 状态块录制期间不合批，避免内部流绑定进入应用的状态块。
HRESULT __stdcall BeginStateBlock_Hook(void* device) {
	typedef HRESULT(__stdcall* Fn)(void*);
	if (s_lockState != 2) return Original<Fn>(kBeginStateBlock)(device);
	Guard guard;
	const bool mine = device == s_device && s_device;
	if (mine) Flush();
	const HRESULT hr = Original<Fn>(kBeginStateBlock)(device);
	if (mine && SUCCEEDED(hr)) s_stateBlock = true;
	return hr;
}

HRESULT __stdcall EndStateBlock_Hook(void* device, DWORD* token) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD*);
	if (s_lockState != 2) return Original<Fn>(kEndStateBlock)(device, token);
	Guard guard;
	const bool mine = device == s_device && s_device;
	if (mine) Flush();
	const HRESULT hr = Original<Fn>(kEndStateBlock)(device, token);
	if (mine && SUCCEEDED(hr)) {
		s_stateBlock = false;
		ResyncTextureStages();
	}
	return hr;
}

HRESULT __stdcall ApplyStateBlock_Hook(void* device, DWORD token) {
	typedef HRESULT(__stdcall* Fn)(void*, DWORD);
	if (s_lockState != 2) return Original<Fn>(kApplyStateBlock)(device, token);
	Guard guard;
	const bool mine = device == s_device && s_device;
	if (mine) Flush();
	const HRESULT hr = Original<Fn>(kApplyStateBlock)(device, token);
	if (mine && !s_stateBlock) ResyncTextureStages();
	return hr;
}

void Detach() {
	s_device = nullptr;
	ReleaseBuffer();
	s_commands.clear();
	s_vertices.clear();
	s_stateBlock = false;
	s_non2dStages = 0;
}

// 设备最终释放前先释放自建顶点缓冲区（它持有设备引用），并停止接管该设备。
ULONG __stdcall Release_Hook(void* device) {
	typedef ULONG(__stdcall* Fn)(void*);
	if (s_lockState == 2) {
		Guard guard;
		if (device == s_device && s_device && !s_releasingBuffer && !s_flushing) {
			Flush();
			const ULONG own = s_buffer && s_bufferDeviceRefs > 0 ? static_cast<ULONG>(s_bufferDeviceRefs) : 0;
			if (DeviceRefCount(device) <= 1 + own) {
				Log("event=d3d8_batch detach draws=%llu flushes=%llu fallbacks=%llu", s_draws, s_flushes, s_fallbacks);
				Detach();
			}
		}
	}
	return Original<Fn>(kRelease)(device);
}

// D3DPOOL_DEFAULT 资源必须在 Reset 前释放；之后按需重建。
HRESULT __stdcall Reset_Hook(void* device, void* parameters) {
	typedef HRESULT(__stdcall* Fn)(void*, void*);
	if (s_lockState != 2) return Original<Fn>(kReset)(device, parameters);
	Guard guard;
	const bool mine = device == s_device && s_device;
	if (mine) {
		Flush();
		ReleaseBuffer();
	}
	const HRESULT hr = Original<Fn>(kReset)(device, parameters);
	if (mine) {
		s_stateBlock = false;
		ResyncTextureStages();
	}
	return hr;
}

HRESULT __stdcall Present_Hook(void* device, const RECT* source, const RECT* dest, HWND window, const void* dirty) {
	typedef HRESULT(__stdcall* Fn)(void*, const RECT*, const RECT*, HWND, const void*);
	if (s_lockState == 2) {
		Guard guard;
		if (device == s_device && s_device) {
			Flush();
			if (++s_frames == 1200 && !s_statsLogged) {
				s_statsLogged = true;
				Log("event=d3d8_batch stats frames=%llu draws=%llu flushes=%llu draws_per_flush=%.1f fallbacks=%llu",
					s_frames, s_draws, s_flushes, s_flushes ? static_cast<double>(s_draws) / s_flushes : 0.0, s_fallbacks);
			}
		}
	}
	return Original<Fn>(kPresent)(device, source, dest, window, dirty);
}

// 贴图、表面内容或 LOD 即将改变：先画完引用它们的暂存绘制。
HRESULT __stdcall TextureLockRect_Hook(void* texture, UINT level, void* locked, const RECT* rect, DWORD flags) {
	FlushCurrent();
	return reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, void*, const RECT*, DWORD)>(s_textureLockRect)(texture, level, locked, rect, flags);
}

DWORD __stdcall TextureSetLOD_Hook(void* texture, DWORD lod) {
	FlushCurrent();
	return reinterpret_cast<DWORD(__stdcall*)(void*, DWORD)>(s_textureSetLOD)(texture, lod);
}

HRESULT __stdcall SurfaceLockRect_Hook(void* surface, void* locked, const RECT* rect, DWORD flags) {
	FlushCurrent();
	return reinterpret_cast<HRESULT(__stdcall*)(void*, void*, const RECT*, DWORD)>(s_surfaceLockRect)(surface, locked, rect, flags);
}

// 其余设备方法统一走转发桩：push this; call FlushFromThunk; jmp [原方法]。
BYTE* BuildThunks() {
#if defined(_M_IX86) || defined(__i386__)
	const size_t size = kDeviceSlots * 16;
	BYTE* code = static_cast<BYTE*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!code) return nullptr;
	memset(code, 0xCC, size);
	const UINT_PTR flush = reinterpret_cast<UINT_PTR>(&FlushFromThunk);
	for (unsigned slot = 0; slot < kDeviceSlots; ++slot) {
		BYTE* p = code + slot * 16;
		p[0] = 0xFF; p[1] = 0x74; p[2] = 0x24; p[3] = 0x04;          // push dword ptr [esp+4]
		p[4] = 0xE8;                                                   // call FlushFromThunk
		const DWORD relative = static_cast<DWORD>(flush - (reinterpret_cast<UINT_PTR>(p) + 9));
		memcpy(p + 5, &relative, 4);
		p[9] = 0xFF; p[10] = 0x25;                                     // jmp dword ptr [s_original + slot]
		const DWORD target = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(&s_original[slot]));
		memcpy(p + 11, &target, 4);
	}
	DWORD old = 0;
	if (!VirtualProtect(code, size, PAGE_EXECUTE_READ, &old)) {
		VirtualFree(code, 0, MEM_RELEASE);
		return nullptr;
	}
	FlushInstructionCache(GetCurrentProcess(), code, size);
	return code;
#else
	return nullptr;
#endif
}

void* HookFor(unsigned slot, BYTE* thunks) {
	switch (slot) {
	case kQueryInterface: case kAddRef: return nullptr; // 不需要提交
	case kRelease: return reinterpret_cast<void*>(&Release_Hook);
	case kReset: return reinterpret_cast<void*>(&Reset_Hook);
	case kPresent: return reinterpret_cast<void*>(&Present_Hook);
	case kBeginStateBlock: return reinterpret_cast<void*>(&BeginStateBlock_Hook);
	case kEndStateBlock: return reinterpret_cast<void*>(&EndStateBlock_Hook);
	case kApplyStateBlock: return reinterpret_cast<void*>(&ApplyStateBlock_Hook);
	case kDrawPrimitiveUP: return reinterpret_cast<void*>(&DrawPrimitiveUP_Hook);
	case kSetTransform: return reinterpret_cast<void*>(&SetTransform_Hook);
	case kSetMaterial: return reinterpret_cast<void*>(&SetMaterial_Hook);
	case kSetClipPlane: return reinterpret_cast<void*>(&SetClipPlane_Hook);
	case kSetRenderState: return reinterpret_cast<void*>(&SetRenderState_Hook);
	case kSetTexture: return reinterpret_cast<void*>(&SetTexture_Hook);
	case kSetTextureStageState: return reinterpret_cast<void*>(&SetTextureStageState_Hook);
	case kSetVertexShader: return reinterpret_cast<void*>(&SetVertexShader_Hook);
	default: return thunks + slot * 16;
	}
}

// 安装记录：失败时逆序恢复已写入的槽位。
struct SlotWrite { void** slot; void* value; };

// 返回 0 成功；1 失败且已完整恢复；2 失败且恢复不完整（仍有槽位指向本模块，桩不能释放）。
int WriteSlots(std::vector<SlotWrite>& writes) {
	size_t done = 0;
	for (; done < writes.size(); ++done) {
		DWORD old = 0;
		if (!VirtualProtect(writes[done].slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) break;
		writes[done].value = InterlockedExchangePointer(writes[done].slot, writes[done].value);
		DWORD ignored = 0;
		VirtualProtect(writes[done].slot, sizeof(void*), old, &ignored);
	}
	if (done == writes.size()) return 0;
	bool restored = true;
	while (done-- > 0) {
		DWORD old = 0;
		if (VirtualProtect(writes[done].slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
			InterlockedExchangePointer(writes[done].slot, writes[done].value);
			DWORD ignored = 0;
			VirtualProtect(writes[done].slot, sizeof(void*), old, &ignored);
		} else {
			restored = false;
		}
	}
	return restored ? 1 : 2;
}

// 全部成功才发布；用临时贴图取得贴图与表面的共享虚表。
bool HookTables(void* device) {
	void* texture = nullptr;
	void* surface = nullptr;
	if (FAILED(Method<HRESULT(__stdcall*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**)>(device, kCreateTexture)(
		device, 1, 1, 1, 0, kFormatA8R8G8B8, kPoolManaged, &texture)) || !texture) {
		return false;
	}
	Method<HRESULT(__stdcall*)(void*, UINT, void**)>(texture, kTextureGetSurfaceLevel)(texture, 0, &surface);
	void** textureTable = *reinterpret_cast<void***>(texture);
	void** surfaceTable = surface ? *reinterpret_cast<void***>(surface) : nullptr;
	if (surface) ReleaseObject(surface);
	ReleaseObject(texture);
	if (!surfaceTable) return false;

	void** table = *reinterpret_cast<void***>(device);
	void* original[kDeviceSlots];
	for (unsigned slot = 0; slot < kDeviceSlots; ++slot) original[slot] = table[slot];
	BYTE* thunks = BuildThunks();
	if (!thunks) return false;
	// 桩通过 s_original 跳转，写槽位前必须先就位。
	memcpy(s_original, original, sizeof(original));
	s_textureLockRect = textureTable[kTextureLockRect];
	s_textureSetLOD = textureTable[kTextureSetLOD];
	s_surfaceLockRect = surfaceTable[kSurfaceLockRect];

	std::vector<SlotWrite> writes;
	try {
		writes.push_back({&textureTable[kTextureLockRect], reinterpret_cast<void*>(&TextureLockRect_Hook)});
		writes.push_back({&textureTable[kTextureSetLOD], reinterpret_cast<void*>(&TextureSetLOD_Hook)});
		writes.push_back({&surfaceTable[kSurfaceLockRect], reinterpret_cast<void*>(&SurfaceLockRect_Hook)});
		for (unsigned slot = 0; slot < kDeviceSlots; ++slot)
			if (void* hook = HookFor(slot, thunks)) {
				writes.push_back({&table[slot], hook});
				s_expected[slot] = hook;
			}
	} catch (const std::bad_alloc&) {
		VirtualFree(thunks, 0, MEM_RELEASE);
		return false;
	}
	const int written = WriteSlots(writes);
	if (written == 2) {
		// 残留槽位仍会经桩/钩子转发到原方法（s_device 为空时不合批），桩必须保留。
		s_thunks = thunks;
		s_hookedTable = table;
		s_installBroken = true;
		Log("event=d3d8_batch install=partial_rollback");
		return false;
	}
	if (written == 1) {
		VirtualFree(thunks, 0, MEM_RELEASE);
		return false;
	}
	s_thunks = thunks;
	s_hookedTable = table;
	return true;
}

} // namespace

void SetLogger(void(*logger)(const char* line)) { s_logger = logger; }

Stats GetStats() {
	Stats stats{};
	if (s_lockState != 2) return stats;
	Guard guard;
	stats.draws = s_draws;
	stats.flushes = s_flushes;
	stats.fallbacks = s_fallbacks;
	stats.invalid = s_invalid;
	stats.hooksBroken = s_hooksBroken;
	return stats;
}

void SetBypass(bool bypass) {
	InitializeLock();
	Guard guard;
	if (bypass) Flush();
	s_bypass = bypass;
}

bool Attach(void* device) {
	if (!device) return false;
	InitializeLock();
	Guard guard;
	if (s_device || s_installBroken) return false;
	void** table = *reinterpret_cast<void***>(device);
	if (!s_hookedTable) {
		if (!HookTables(device)) {
			Log("event=d3d8_batch install=failed");
			return false;
		}
	} else if (table != s_hookedTable) {
		Log("event=d3d8_batch skip=different_vtable");
		return false;
	}
	if (s_installBroken) return false;
	DWORD caps[kCapsDwords]{};
	s_maxClipPlanes = SUCCEEDED(Method<HRESULT(__stdcall*)(void*, void*)>(device, kGetDeviceCaps)(device, caps)) ? caps[kCapsMaxUserClipPlanes] : 0;
	s_device = device;
	s_ownerThread = GetCurrentThreadId();
	s_stateBlock = false;
	s_bypass = false;
	s_hooksBroken = false;
	s_knownFvf.clear();
	ResyncTextureStages();
	s_draws = s_flushes = s_fallbacks = s_frames = s_invalid = 0;
	s_statsLogged = false;
	Log("event=d3d8_batch install=ok device=0x%08lX", static_cast<unsigned long>(reinterpret_cast<UINT_PTR>(device)));
	return true;
}

} // namespace D3D8Batch
