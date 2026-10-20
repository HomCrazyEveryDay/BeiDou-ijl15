// D3D8Batch 回归测试：先在挂接前画出各场景作为基线，挂接后用合批重画，比较后台缓冲区 RGB 哈希
// 与场景内状态/绘制调用的返回值序列；覆盖状态/步长切换、扇形与折线、带变换与用户裁剪平面的几何、帧中查询断言、
// 帧中修改已用贴图、非法参数的同步错误、命令数与字节容量阈值、大绘制直通、状态块（含恢复立方体
// 绑定）、立方体贴图采样与修改、Reset 以及释放后新设备重新挂接，并断言合批统计，最后测帧率。
// 正式只在 Wine 下启用；原生 Windows 运行用于回归（状态块会使原生 d3d8 改写虚表，合批随之自动停用，
// 非法参数段跳过）。Windows 用 D3D8BatchTest.ps1；macOS 可用 zig 交叉编译后在 Wine 下运行：
//   zig c++ -target x86-windows-gnu -O2 -std=c++17 tests/D3D8BatchTest.cpp ezorsia/D3D8Batch.cpp -luser32 -lgdi32 -o D3D8BatchTest.exe
#include "../ezorsia/D3D8Batch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
template<class R, class... A> R Call(void* object, unsigned slot, A... args) {
	typedef R(__stdcall* Fn)(void*, A...);
	return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[slot])(object, args...);
}
// IDirect3DDevice8 及资源槽位（mingw-w64 d3d8.h）。
enum : unsigned {
	Release = 2, Reset = 14, CreateDevice = 15, Present = 15, GetBackBuffer = 16, CreateTexture = 20, CreateCubeTexture = 22,
	BeginScene = 34, EndScene = 35, Clear = 36, SetTransform = 37, SetViewport = 40, SetClipPlane = 48, SetRenderState = 50,
	GetRenderState = 51, BeginStateBlock = 52, EndStateBlock = 53, ApplyStateBlock = 54, DeleteStateBlock = 56,
	SetTexture = 61, SetTextureStageState = 63, DrawPrimitiveUP = 72, SetVertexShader = 76,
	TexLockRect = 16, TexUnlockRect = 17, CubeLockRect = 16, CubeUnlockRect = 17, SurfLockRect = 9, SurfUnlockRect = 10
};
enum : DWORD { AlphaBlend = 27, ClipPlaneEnable = 152, Lighting = 137, SrcBlend = 19, DestBlend = 20 };
struct Locked { int pitch; BYTE* bits; };
struct VertexA { float x, y, z, rhw; DWORD color; float u, v; };
struct VertexB { float x, y, z, rhw; float u, v; };
struct VertexC { float x, y, z; DWORD color; float u, v; };
struct VertexCube { float x, y, z, rhw; float u, v, w; };
// RHW|DIFFUSE|TEX1, RHW|TEX1, XYZ|DIFFUSE|TEX1, RHW|TEX1|TEXCOORDSIZE3(0)
const DWORD FvfA = 0x144, FvfB = 0x104, FvfC = 0x142, FvfCube = 0x10104;
const int Textures = 16, TexSize = 32, CubeSize = 16;
enum Scene { Mixed, Clipped, CommandCap, ByteCap, StateBlock, Cube, SceneCount };
const char* SceneNames[SceneCount] = {"mixed", "clipped", "command_cap", "byte_cap", "stateblock", "cube"};

void* d3d = nullptr;
void* device = nullptr;
void* textures[Textures]{};
void* cube = nullptr;
HWND window = nullptr;
DWORD present[13]{};
int width = 0, height = 0, failures = 0;
bool wine = false; // 原生 d3d8 不校验越界参数，非法调用只在 Wine 下测试
unsigned long long resultHash = 0; // 场景内全部调用返回值的序列哈希

LRESULT CALLBACK Proc(HWND w, UINT message, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, message, wp, lp); }
void Fail(const char* what) { failures++; printf("FAIL %s\n", what); }
HRESULT Chk(HRESULT hr) { resultHash = (resultHash ^ static_cast<DWORD>(hr)) * 1099511628211ull; return hr; }

void FillTexture(int index, DWORD salt) {
	Locked locked{};
	if (FAILED(Call<HRESULT>(textures[index], TexLockRect, 0u, &locked, static_cast<const RECT*>(nullptr), 0ul))) return Fail("texture lock");
	for (int y = 0; y < TexSize; y++) {
		DWORD* row = reinterpret_cast<DWORD*>(locked.bits + y * locked.pitch);
		for (int x = 0; x < TexSize; x++)
			row[x] = (((x ^ y ^ index) & 4) ? 0xff000000 : 0x90000000) | ((index * 2654435761u + salt * 40503u + x * 977u) & 0xffffff);
	}
	Call<HRESULT>(textures[index], TexUnlockRect, 0u);
}

void FillCube(DWORD salt) {
	for (DWORD face = 0; face < 6; face++) {
		Locked locked{};
		if (FAILED(Call<HRESULT>(cube, CubeLockRect, face, 0u, &locked, static_cast<const RECT*>(nullptr), 0ul))) return Fail("cube lock");
		for (int y = 0; y < CubeSize; y++) {
			DWORD* row = reinterpret_cast<DWORD*>(locked.bits + y * locked.pitch);
			for (int x = 0; x < CubeSize; x++) row[x] = 0xff000000 | ((face * 0x3f0000 + salt * 0x10307 + x * 0x0f0 + y * 0x0f) & 0xffffff);
		}
		Call<HRESULT>(cube, CubeUnlockRect, face, 0u);
	}
}

void SetupDevice() {
	for (int i = 0; i < Textures; i++)
		if (FAILED(Call<HRESULT>(device, CreateTexture, static_cast<UINT>(TexSize), static_cast<UINT>(TexSize), 1u, 0ul, 21ul, 1ul, &textures[i]))) Fail("CreateTexture");
	if (FAILED(Call<HRESULT>(device, CreateCubeTexture, static_cast<UINT>(CubeSize), 1u, 0ul, 21ul, 1ul, &cube))) Fail("CreateCubeTexture");
}

void SetupStates() {
	const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const float ortho[16] = {2.0f / width, 0, 0, 0, 0, -2.0f / height, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1};
	Call<HRESULT>(device, SetTransform, 256ul, static_cast<const void*>(identity));
	Call<HRESULT>(device, SetTransform, 2ul, static_cast<const void*>(identity));
	Call<HRESULT>(device, SetTransform, 3ul, static_cast<const void*>(ortho));
	Call<HRESULT>(device, SetRenderState, Lighting, 0ul);
	Call<HRESULT>(device, SetRenderState, SrcBlend, 5ul);
	Call<HRESULT>(device, SetRenderState, DestBlend, 6ul);
	Call<HRESULT>(device, SetRenderState, 22ul, 1ul); // CULLMODE none
}

void ReleaseDevice() {
	for (int i = 0; i < Textures; i++) if (textures[i]) Call<ULONG>(textures[i], Release), textures[i] = nullptr;
	if (cube) Call<ULONG>(cube, Release), cube = nullptr;
	const ULONG remaining = Call<ULONG>(device, Release);
	device = nullptr;
	printf("device_release_remaining=%lu\n", remaining);
	if (remaining != 0) Fail("device refcount");
}

void CreateTestDevice() {
	if (FAILED(Call<HRESULT>(d3d, CreateDevice, 0u, 1ul, window, 0x20ul, static_cast<void*>(present), &device)) || !device) {
		printf("FAIL CreateDevice\n");
		exit(2);
	}
	SetupDevice();
	SetupStates();
}

void Set(unsigned slot, DWORD a, DWORD b) { Chk(Call<HRESULT>(device, slot, a, b)); }
void SetTex(DWORD stage, void* texture) { Chk(Call<HRESULT>(device, SetTexture, stage, texture)); }
void SetFvf(DWORD fvf) { Chk(Call<HRESULT>(device, SetVertexShader, fvf)); }
void Draw(DWORD type, UINT primitives, const void* vertices, UINT stride) {
	if (FAILED(Chk(Call<HRESULT>(device, DrawPrimitiveUP, type, primitives, vertices, stride)))) Fail("DrawPrimitiveUP");
}

void Quad(int i, DWORD seed, bool formatB, float size = 0) {
	const DWORD h = (i + 1) * 2654435761u + seed * 97u;
	const float x = static_cast<float>(h % (width - 48)), y = static_cast<float>((h >> 11) % (height - 48));
	const float s = size ? size : 16.0f + (h >> 24) % 32;
	if (formatB) {
		VertexB q[4] = {{x, y, 0, 1, 0, 0}, {x + s, y, 0, 1, 1, 0}, {x, y + s, 0, 1, 0, 1}, {x + s, y + s, 0, 1, 1, 1}};
		Draw(5, 2, q, sizeof(VertexB));
	} else {
		const DWORD color = 0xff000000 | (h & 0xffffff);
		VertexA q[4] = {{x, y, 0, 1, color, 0, 0}, {x + s, y, 0, 1, color, 1, 0}, {x, y + s, 0, 1, color, 0, 1}, {x + s, y + s, 0, 1, color, 1, 1}};
		Draw(5, 2, q, sizeof(VertexA));
	}
}

// 采样立方体 +X 面的屏幕四边形。
void CubeQuad(int i, DWORD seed) {
	const float x = static_cast<float>((i * 83 + seed * 7) % (width - 64)), y = static_cast<float>((i * 47) % (height - 64));
	VertexCube q[4] = {{x, y, 0, 1, 1, -1, -1}, {x + 60, y, 0, 1, 1, -1, 1}, {x, y + 60, 0, 1, 1, 1, -1}, {x + 60, y + 60, 0, 1, 1, 1, 1}};
	Draw(5, 2, q, sizeof(VertexCube));
}

// 模拟衣柜预览：世界坐标几何、平移变换与四个用户裁剪平面。
void ClippedQuad(int i, DWORD seed) {
	const float left = static_cast<float>(40 + (i * 37 + seed * 11) % (width / 2)), top = static_cast<float>(30 + (i * 53) % (height / 2));
	const float clip[4][4] = {{1, 0, 0, -left}, {-1, 0, 0, left + 90}, {0, 1, 0, -top}, {0, -1, 0, top + 70}};
	for (DWORD p = 0; p < 4; p++) Chk(Call<HRESULT>(device, SetClipPlane, p, static_cast<const float*>(clip[p])));
	const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, left - 20, top - 15, 0, 1};
	Chk(Call<HRESULT>(device, SetTransform, 256ul, static_cast<const void*>(world)));
	Set(SetRenderState, ClipPlaneEnable, 15);
	SetFvf(FvfC);
	const DWORD color = 0xff000000 | ((i * 40503u + seed) & 0xffffff);
	VertexC q[4] = {{0, 0, 0.5f, color, 0, 0}, {140, 0, 0.5f, color, 1, 0}, {0, 110, 0.5f, color, 0, 1}, {140, 110, 0.5f, color, 1, 1}};
	Draw(5, 2, q, sizeof(VertexC));
	const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	Chk(Call<HRESULT>(device, SetTransform, 256ul, static_cast<const void*>(identity)));
	Set(SetRenderState, ClipPlaneEnable, 0);
}

void FanAndStrip(int i, DWORD seed) {
	const float cx = static_cast<float>(60 + (i * 71 + seed) % (width - 120)), cy = static_cast<float>(60 + (i * 29) % (height - 120));
	VertexA fan[6] = {{cx, cy, 0, 1, 0xffff8040, 0.5f, 0.5f}, {cx - 30, cy - 30, 0, 1, 0xff40ff80, 0, 0}, {cx + 30, cy - 30, 0, 1, 0xff4080ff, 1, 0},
		{cx + 40, cy + 10, 0, 1, 0xffffff40, 1, 1}, {cx, cy + 40, 0, 1, 0xffff40ff, 0, 1}, {cx - 40, cy + 10, 0, 1, 0xff40ffff, 0, 0.5f}};
	Draw(6, 4, fan, sizeof(VertexA));
	VertexA line[5];
	for (int p = 0; p < 5; p++) line[p] = {cx - 50 + p * 25.0f, cy + ((p & 1) ? 45.0f : -45.0f), 0, 1, 0xffffffff, 0, 0};
	Draw(3, 4, line, sizeof(VertexA));
}

// 有暂存绘制时的非法参数：返回值须与未挂接时一致。
void InvalidCalls() {
	if (!wine) return;
	const DWORD outside[6] = {static_cast<DWORD>(width), 0, 1, 1, 0, 0x3f800000};
	Chk(Call<HRESULT>(device, SetViewport, static_cast<const void*>(outside)));
	const float plane[4] = {1, 0, 0, 0};
	Chk(Call<HRESULT>(device, SetClipPlane, 31ul, static_cast<const float*>(plane)));
	Chk(Call<HRESULT>(device, SetTextureStageState, 9ul, 1ul, 4ul));
	Chk(Call<HRESULT>(device, SetTexture, 9ul, textures[0]));
	Chk(Call<HRESULT>(device, SetRenderState, 9999ul, 1ul));
}

void BeginFrame() {
	for (int i = 0; i < Textures; i++) FillTexture(i, 0);
	FillCube(0);
	Chk(Call<HRESULT>(device, BeginScene));
	Chk(Call<HRESULT>(device, Clear, 0ul, static_cast<const void*>(nullptr), 1ul, 0xff203040ul, 1.0f, 0ul));
	SetFvf(FvfA);
	Set(SetRenderState, AlphaBlend, 1);
	SetTex(1, nullptr);
	Set(SetTextureStageState, 0, 0); // 占位，保持序列稳定（stage 0 / type 0 为非法，两边一致）
}

void SceneMixed(DWORD seed, int sprites) {
	bool formatB = false;
	for (int i = 0; i < sprites; i++) {
		SetTex(0, textures[(i * 7 + seed) % Textures]);
		if (i % 17 == 0) {
			const DWORD value = (i / 17) & 1;
			Set(SetRenderState, AlphaBlend, value);
			if (i % 51 == 0) { // 帧中查询必须看到刚设置的值。
				DWORD read = 99;
				if (FAILED(Chk(Call<HRESULT>(device, GetRenderState, AlphaBlend, &read))) || read != value) Fail("GetRenderState mid-batch");
			}
		}
		if (i % 23 == 0) Chk(Call<HRESULT>(device, SetTextureStageState, 0ul, 1ul, (i / 23) & 1 ? 2ul : 4ul)); // COLOROP
		if (i % 31 == 0) formatB = !formatB;
		SetFvf(formatB ? FvfB : FvfA);
		if (i % 97 == 40) {
			const DWORD viewport[6] = {16, 16, static_cast<DWORD>(width - 32), static_cast<DWORD>(height - 32), 0, 0x3f800000};
			Chk(Call<HRESULT>(device, SetViewport, static_cast<const void*>(viewport)));
		}
		if (i % 97 == 70) {
			const DWORD viewport[6] = {0, 0, static_cast<DWORD>(width), static_cast<DWORD>(height), 0, 0x3f800000};
			Chk(Call<HRESULT>(device, SetViewport, static_cast<const void*>(viewport)));
		}
		if (i == sprites / 2) FillTexture((seed * 3) % Textures, seed + 1); // 修改已画过、后面还要用的贴图
		if (i == sprites / 3) InvalidCalls();
		if (i % 53 == 20) { ClippedQuad(i, seed); SetFvf(formatB ? FvfB : FvfA); }
		if (i % 41 == 10) { SetFvf(FvfA); FanAndStrip(i, seed); SetFvf(formatB ? FvfB : FvfA); }
		Quad(i, seed, formatB);
	}
	const DWORD viewport[6] = {0, 0, static_cast<DWORD>(width), static_cast<DWORD>(height), 0, 0x3f800000};
	Chk(Call<HRESULT>(device, SetViewport, static_cast<const void*>(viewport)));
}

// 每个精灵前三条状态命令：先撞上命令数上限（字节数远低于 2MB）。
void SceneCommandCap(DWORD seed) {
	for (int i = 0; i < 9000; i++) {
		SetTex(0, textures[(i + seed) % Textures]);
		Set(SetRenderState, AlphaBlend, i & 1);
		Chk(Call<HRESULT>(device, SetTextureStageState, 0ul, 1ul, (i & 2) ? 2ul : 4ul));
		Quad(i, seed, false, 8);
	}
}

// 只有绘制、没有状态命令：每次 600 顶点（16800 字节），约 125 次即先撞上 2MB 字节上限
// （命令数远低于上限），同时经过 512KB 起的扩容；再加一次超过 256KB 的直通绘制。
void SceneByteCap(DWORD seed) {
	SetTex(0, textures[seed % Textures]);
	static VertexA chunk[600];
	for (int i = 0; i < 300; i++) {
		for (int v = 0; v < 600; v++) {
			const DWORD h = (i * 600 + v + 1) * 2654435761u + seed;
			chunk[v] = {static_cast<float>(h % width), static_cast<float>((h >> 12) % height), 0, 1, 0x30000000u | (h & 0xffffff), 0, 0};
		}
		Draw(4, 200, chunk, sizeof(VertexA));
	}
	static VertexA big[9600];
	for (int v = 0; v < 9600; v++) {
		const float x = static_cast<float>((v * 37 + seed) % width), y = static_cast<float>((v * 91) % height);
		big[v] = {x, y, 0, 1, 0x40ffffff, 0, 0};
	}
	Draw(4, 3200, big, sizeof(VertexA));
	for (int i = 0; i < 50; i++) Quad(i + 30000, seed, false);
}

// 状态块录制期间绘制；录制块设置立方体绑定，之后在 2D 绑定下应用以恢复立方体并修改其内容。
void SceneStateBlock(DWORD seed) {
	for (int i = 0; i < 40; i++) { SetTex(0, textures[i % Textures]); Quad(i, seed, false); }
	if (FAILED(Chk(Call<HRESULT>(device, BeginStateBlock)))) return Fail("BeginStateBlock");
	Set(SetRenderState, AlphaBlend, 0);
	SetTex(0, cube);
	for (int i = 40; i < 60; i++) Quad(i, seed, false);
	DWORD token = 0;
	if (FAILED(Chk(Call<HRESULT>(device, EndStateBlock, &token)))) return Fail("EndStateBlock");
	for (int i = 60; i < 100; i++) { SetTex(0, textures[i % Textures]); Quad(i, seed, false); }
	Chk(Call<HRESULT>(device, ApplyStateBlock, token)); // 恢复立方体绑定
	SetFvf(FvfCube);
	for (int i = 0; i < 20; i++) CubeQuad(i, seed);
	FillCube(seed + 5);
	for (int i = 20; i < 40; i++) CubeQuad(i, seed);
	SetTex(0, textures[1]);
	SetFvf(FvfA);
	for (int i = 100; i < 160; i++) Quad(i, seed, false);
	Chk(Call<HRESULT>(device, DeleteStateBlock, token));
}

// 立方体贴图真正参与采样：修改面之前画的四边形必须使用旧内容。
void SceneCube(DWORD seed) {
	for (int i = 0; i < 60; i++) { SetTex(0, textures[i % Textures]); Quad(i, seed, false); }
	SetTex(0, cube);
	SetFvf(FvfCube);
	for (int i = 0; i < 30; i++) CubeQuad(i, seed);
	FillCube(seed + 1);
	for (int i = 30; i < 60; i++) CubeQuad(i, seed);
	SetTex(0, textures[2]);
	SetFvf(FvfA);
	for (int i = 120; i < 200; i++) { SetTex(0, textures[i % Textures]); Quad(i, seed, false); }
}

bool backBufferReadable = false;

unsigned long long HashBackBuffer() {
	if (!backBufferReadable) return 1; // 不支持的格式：Compare 标记为 SKIP，不计为 PASS
	void* surface = nullptr;
	if (FAILED(Call<HRESULT>(device, GetBackBuffer, 0u, 0ul, &surface)) || !surface) return 0;
	Locked locked{};
	unsigned long long hash = 1469598103934665603ull;
	if (SUCCEEDED(Call<HRESULT>(surface, SurfLockRect, &locked, static_cast<const RECT*>(nullptr), 0x10ul))) {
		for (int y = 0; y < height; y++) {
			const DWORD* row = reinterpret_cast<const DWORD*>(locked.bits + y * locked.pitch);
			for (int x = 0; x < width; x++) hash = (hash ^ (row[x] & 0xffffff)) * 1099511628211ull; // 32 位格式：忽略 X/A
		}
		Call<HRESULT>(surface, SurfUnlockRect);
	} else {
		hash = 0;
	}
	Call<ULONG>(surface, Release);
	return hash;
}

struct Result { unsigned long long pixels, results; };

Result RenderScene(int scene, DWORD seed) {
	resultHash = 1469598103934665603ull;
	BeginFrame();
	switch (scene) {
	case Mixed: SceneMixed(seed, 700); break;
	case Clipped: for (int i = 0; i < 120; i++) { ClippedQuad(i, seed); SetFvf(FvfA); Quad(i, seed, false); } break;
	case CommandCap: SceneCommandCap(seed); break;
	case ByteCap: SceneByteCap(seed); break;
	case StateBlock: SceneStateBlock(seed); break;
	case Cube: SceneCube(seed); break;
	}
	Chk(Call<HRESULT>(device, EndScene));
	Result result{HashBackBuffer(), resultHash};
	Call<HRESULT>(device, Present, static_cast<const RECT*>(nullptr), static_cast<const RECT*>(nullptr), static_cast<HWND>(nullptr), static_cast<const void*>(nullptr));
	return result;
}

bool Compare(const char* name, DWORD seed, const Result& baseline, const Result& batched) {
	const bool same = batched.pixels == baseline.pixels && batched.pixels != 0 && batched.results == baseline.results;
	if (!same) failures++;
	const char* verdict = !same ? "FAIL" : backBufferReadable ? "PASS" : "SKIP-PIXELS";
	printf("%s %s seed=%lu pixels %016llx/%016llx results %016llx/%016llx\n", verdict, name, seed,
		baseline.pixels, batched.pixels, baseline.results, batched.results);
	return same;
}

double MeasureFps(int sprites, double seconds) {
	LARGE_INTEGER frequency, start, now;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);
	now = start;
	int frames = 0;
	while ((now.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart) < seconds) {
		MSG message;
		while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
		BeginFrame();
		for (int i = 0; i < sprites; i++) { SetTex(0, textures[(i * 7 + frames) % Textures]); Quad(i, frames, false); }
		Call<HRESULT>(device, EndScene);
		Call<HRESULT>(device, Present, static_cast<const RECT*>(nullptr), static_cast<const RECT*>(nullptr), static_cast<HWND>(nullptr), static_cast<const void*>(nullptr));
		frames++;
		QueryPerformanceCounter(&now);
	}
	return frames / ((now.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart));
}

void PrintLog(const char* line) { printf("log: %s\n", line); }
}

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != nullptr;
	printf("start wine=%d\n", wine ? 1 : 0);
	width = argc > 1 ? atoi(argv[1]) : 1280;
	height = argc > 2 ? atoi(argv[2]) : 720;
	const double seconds = argc > 3 ? atof(argv[3]) : 3.0;
	WNDCLASSA wc{};
	wc.lpfnWndProc = Proc;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "d3d8batch";
	RegisterClassA(&wc);
	RECT rect{0, 0, width, height};
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	window = CreateWindowA("d3d8batch", "d3d8batch", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);
	printf("window created\n");
	typedef void*(WINAPI * Create_t)(UINT);
	Create_t create = reinterpret_cast<Create_t>(GetProcAddress(LoadLibraryA("d3d8.dll"), "Direct3DCreate8"));
	d3d = create ? create(220) : nullptr;
	DWORD mode[4]{};
	if (!d3d || FAILED(Call<HRESULT>(d3d, 8, 0u, static_cast<void*>(mode)))) { printf("FAIL d3d8 unavailable\n"); return 2; } // GetAdapterDisplayMode
	backBufferReadable = mode[3] == 21 || mode[3] == 22; // A8R8G8B8 / X8R8G8B8
	if (!backBufferReadable) printf("note: back buffer format %lu is not 32-bit; pixel comparison skipped\n", mode[3]);
	present[0] = width; present[1] = height; present[2] = mode[3]; present[3] = 1;
	present[5] = 1; /* DISCARD */ present[6] = reinterpret_cast<DWORD>(window); present[7] = TRUE; present[10] = 1; /* LOCKABLE_BACKBUFFER */
	CreateTestDevice();
	printf("device created\n");

	// 1. 挂接前的基线。
	Result baseline[SceneCount][3]{};
	for (int scene = 0; scene < SceneCount; scene++)
		for (DWORD seed = 1; seed <= 3; seed++) baseline[scene][seed - 1] = RenderScene(scene, seed);
	printf("baseline done\n");

	// 2. 挂接后合批重画，与基线比较，并断言确实合批、没有回退或越界。
	D3D8Batch::SetLogger(PrintLog);
	if (!D3D8Batch::Attach(device)) { printf("FAIL Attach\n"); return 2; }
	for (int scene = 0; scene < SceneCount; scene++)
		for (DWORD seed = 1; seed <= 3; seed++) {
			const unsigned long long before = D3D8Batch::GetStats().flushes;
			Compare(SceneNames[scene], seed, baseline[scene][seed - 1], RenderScene(scene, seed));
			// 字节场景只有 2MB 上限会在帧内提交（另有直通绘制前与帧末各一次）。
			const unsigned long long flushes = D3D8Batch::GetStats().flushes - before;
			if (scene == ByteCap && flushes < 4) Fail("byte cap did not trigger");
			if (scene == ByteCap) printf("byte_cap flushes=%llu\n", flushes);
		}
	const D3D8Batch::Stats stats = D3D8Batch::GetStats();
	printf("stats draws=%llu flushes=%llu fallbacks=%llu invalid=%llu hooks_broken=%d\n", stats.draws, stats.flushes, stats.fallbacks,
		stats.invalid, stats.hooksBroken ? 1 : 0);
	// 原生 d3d8 的状态块录制会改写设备虚表，合批应自动停用（画面仍须与基线一致）；Wine 不会。
	if (wine && stats.hooksBroken) Fail("hooks unexpectedly broken under Wine");
	if (stats.draws == 0) Fail("no draws were batched");
	if (stats.fallbacks || stats.invalid) Fail("fallback or invalid draw range");

	// 3. Reset 会先释放合批自建的 DEFAULT 池缓冲区；之后须继续合批且结果一致。
	if (FAILED(Call<HRESULT>(device, Reset, static_cast<void*>(present)))) Fail("Reset");
	SetupStates();
	Compare("after_reset", 2, baseline[Mixed][1], RenderScene(Mixed, 2));

	// 4. 帧率。
	const int sprites = static_cast<int>(width * height / 1024.0 * 0.8);
	D3D8Batch::SetBypass(true);
	const double directFps = MeasureFps(sprites, seconds);
	D3D8Batch::SetBypass(false);
	const double batchedFps = MeasureFps(sprites, seconds);
	printf("PERF %dx%d sprites=%d direct_fps=%.1f batched_fps=%.1f\n", width, height, sprites, directFps, batchedFps);

	// 5. 释放后新建设备（同一虚表）须能重新挂接，结果仍与基线一致。
	ReleaseDevice();
	CreateTestDevice();
	if (!D3D8Batch::Attach(device)) {
		if (wine) Fail("re-Attach");
		else printf("note: re-Attach declined on native d3d8 (device vtable differs); batching stays off\n");
	}
	Compare("reattach", 3, baseline[Mixed][2], RenderScene(Mixed, 3));
	// 6. 模拟他方替换设备虚表（原生 d3d8 状态块录制即如此）：合批须自动停用且画面仍一致。
	{
		void** original = *reinterpret_cast<void***>(device);
		static void* copy[128];
		memcpy(copy, original, 97 * sizeof(void*));
		*reinterpret_cast<void***>(device) = copy;
		Compare("vtable_swapped", 1, baseline[Mixed][0], RenderScene(Mixed, 1));
		*reinterpret_cast<void***>(device) = original;
		if (!D3D8Batch::GetStats().hooksBroken) Fail("vtable swap not detected");
	}
	ReleaseDevice();
	Call<ULONG>(d3d, Release);
	printf(failures ? "RESULT FAIL\n" : "RESULT PASS\n");
	return failures ? 1 : 0;
}
