#pragma once
#include <windows.h>

// Wine/macOS 下的 DrawPrimitiveUP 合批。
//
// Apple Silicon 只能用新 WoW64 运行 32 位客户端：wined3d 每次 DrawPrimitiveUP
// 都映射一次流式 GL 缓冲区，而 WoW64 会为每次映射复制整块缓冲区，精灵越多越慢
// （1080p 约 8 fps）。Gr2D_DX8 只用 DrawPrimitiveUP 画精灵，因此这里把一段连续
// 绘制的顶点暂存到内存，并按原顺序记录其间的状态调用；遇到任何其他设备调用或
// 贴图/表面加锁时，一次写入动态顶点缓冲区后原样重放。仅由调用方在 Wine 下启用。
namespace D3D8Batch {
// 在 CreateDevice 成功后调用；只接管第一个设备。安装失败时逆序恢复已改槽位；
// 恢复不完整时保留转发桩（只转发到原方法）并不再接管任何设备。
bool Attach(void* device);
// 可选日志回调（单行文本，无换行）。
void SetLogger(void(*logger)(const char* line));
// 供测试使用：临时停止合批并提交已暂存内容（true 为停止）。
void SetBypass(bool bypass);
// 供测试与诊断：draws 已合批绘制数，fallbacks 退回原路径次数，invalid 顶点范围越界而跳过的绘制数；
// hooksBroken 表示设备虚表被他方改写（如原生 d3d8 的状态块录制），合批已停用到下次 Attach。
struct Stats { unsigned long long draws, flushes, fallbacks, invalid; bool hooksBroken; };
Stats GetStats();
}
