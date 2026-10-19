# 2026-10-14：高地址字符串转换失败的根因与修复

来源：用户提供的 `logs(2).7z`、同版 BeiDou.exe/PCOM/NameSpace 反汇编、Windows x86 独立原生代码回放。关联调查：[八次闪退](player-logs2-7z-crashes-20261014.md)、[logs(6)](player-logs6-crashes-20261014.md)。本轮未启动或操作游戏、未修改资源或生产服务。

## 结论

已复现一个会造成正常资源名称失效的确定根因：**v83 字符串转换把实际只有几十字节的输出缓冲区声明成 `0x3fffffff` 个字符；当缓冲区落到高地址，32 位地址范围发生回绕，Windows 转换失败，原生代码却忽略返回值，继续使用未写入的名称。**

这个缺陷不需要外挂、坏 IMG 或人为损坏内存。独立进程直接执行原版 EXE 的转换函数，再交给真实 PCOM/ResMan/NameSpace，就能同时复现：

- 正常 UI 路径变成空文本，查询返回 `NameSpace+172DC` 目录对象，Canvas QI 失败；
- 父属性有 `Lbracket`，空查询键却得到 S_FALSE/VT_EMPTY；
- `Shape2D#Vector2D` 变成空类名，PCOM 返回 `800401F8`。

使用生产修复实现后，相同地址、相同源字符串和相同文件全部恢复正常。

玩家现场证据尤其明确的是 PID 17504：完整扫描包内资源查询行，8 条可读空键全部位于 `9608BC2C / 960D1688 / 960D1728`，正处于此问题的失败地址区间。它们不是不可读指针。对应 `Lbracket` 原始 StringPool 项和父属性节点正常。

这为已观察到的名称失效提供了可重现的上游机制。其余 Boss、快捷栏、Vector2D 的失败链也在同一实验中复现；但这些玩家 dump 未保留转换瞬间的完整输入/输出堆内存，**不能据此宣称八次每一次均已逐一证明同一个根因**。`logs(6)` 和更早脸饰故障同样需要区分“机制可解释”与“现场完整闭环”。

## 原生指令与失败条件

`StringPool::GetInstance` 在 `0079E805`，`00406292` 是取字符串并构造 `_bstr_t` 的包装；真正的窄字符串缓存读取在 `0079E993`。转换链是：

```text
StringPool → ZXString<char> → 00406301 / 00406359 → 0040638C
  004063A4 MultiByteToWideChar(..., destination=null, capacity=0) 获取真实长度
  004063B1 → 00402B4F 按真实长度分配 BSTR，写入长度前缀/结尾 NUL
  004063B8 push 3FFFFFFFh
  004063C5 MultiByteToWideChar(..., actualBuffer, 3FFFFFFFh)
  004063CB 返回 actualBuffer，未检查第二次转换是否成功
```

例如 `Lbracket` 需要 9 个 wchar（含结尾），BSTR 实际分配 22 字节（含长度前缀），但第二次 API 调用声称有约 2 GiB 的输出空间。目标地址 `9608BC2C + 3FFFFFFF*2` 超出 32 位地址范围。

实测：x86 进程中地址 `20000000` 成功；地址 `96080000` 和 `C0000000` 使用旧容量失败，返回 0、GetLastError=122（ERROR_INSUFFICIENT_BUFFER），未写入目标；使用真实容量则成功。x64 进程不能复现同样的地址回绕，所以验证必须编译 x86 并启用 `/LARGEADDRESSAWARE`。客户端 PE 的 Characteristics 为 `012F`，已包含 `0020` 大地址标记。

高地址不等于“物理内存不足”或“进程占用恰好超过 2GB”：只要实际分配地址进入该区域就能触发，碎片、资源加载和地址布局均可能影响到达时间。

失败后 BSTR 长度前缀仍可能非零，实际文本却为空或保留旧字节。PCOM 工厂入口接收的是以 NUL 结尾的类名，会重新构造自己的字符串；因此即使 EXE 返回的旧 BSTR 前缀非零，仍会进入 PCOM 的空类名分支。测试验证了实际工厂返回值，未把“首字符 NUL”与“原 BSTR 长度前缀为零”混为一谈。

`00402B2C` 的 VARIANT 格式化也使用相同错误容量。PCOM 的 `+2A1A/+857A/+89EA`、NameSpace 的 `+5031/+3A4B` 还有同类宽字符转换；反向窄字符转换也使用该哨兵长度，在更高地址同样可能回绕。仅修改一个 UI 调用点无法覆盖这些独立组件。

## 修复范围

`LegacyStringConversionFix.cpp` 在客户端启动阶段安装 MultiByteToWideChar / WideCharToMultiByte 的 Detours。仅当 **输出缓冲区非空，且容量恰为旧代码的 `0x3fffffff`** 时重新查询相同源字符串、代码页、标志所需长度，再按真实长度执行转换。普通容量、只查询长度、正常的不足缓冲区错误与无效编码错误保留原行为。

这样也覆盖随后加载的原生 WZ 组件，不替换资源、不创建假画布、不绕过 PCOM 工厂、不吞异常。内存仍由原有 BSTR/ZXString 分配与释放路径管理，不改变前次 ResourceVariant 分配器配对修复。

安装不依赖 CrashDump 开关；只校验支持的 x86 客户端映像后安装。已有 Wine 编码 hook 先安装时，新 hook 继续调用既有链，保留代码页映射。仅记录一次安装结果 `string.conversion.fix ready=1`，没有增加逐帧或逐包日志。

## 可重复验证

运行：`tests/LegacyStringConversionFixTest.ps1`，可传 `-ClientDirectory`。

测试读取真实 EXE 到隔离地址 `30400000`，仅执行 `0040638C/00402B4F`，保留原始转换/分配长度算法；分配回调用于确定性选择缓冲区地址。没有执行游戏入口、没有改坏字符串、没有强制 WinAPI 返回失败。独立加载真实 PCOM/ResMan/Canvas/NameSpace，挂载只读 Data。

| 验证项 | 旧实现 | 修复后 |
| --- | --- | --- |
| 原生转换，低地址 `20000004` | 正常 | 正常 |
| 原生转换，高地址 `96080004` | 错误 122，未写入名称 | 名称、长度前缀和原分配大小正确 |
| 状态栏 graduation、quickSlot | 返回目录，Canvas=0 | Canvas=1 |
| Boss backgrnd / 2 / 3 / 4 | 全部返回目录，Canvas=0 | 全部 Canvas=1 |
| number 的 Lbracket 属性 | S_FALSE、VT_EMPTY | S_OK、有效 Canvas |
| Vector2D 工厂 | 800401F8 | S_OK、有效对象 |
| 空输入 / nullptr 原生输入 | 原生语义 | 原生语义不变 |

API 回归覆盖低地址、2GB 边界、高地址 `96080000`、`C1000000`，GBK/UTF-8 双向转换、显式长度、无效 UTF-8、普通不足缓冲区、长度查询与默认替代字符输出。两组进程分别为无其他编码 hook、已有 ACP→936 hook；共 64,000 次矩阵转换通过，原生/组件对照也全部通过。ACP 链测试不等同于 Wine 操作系统实机验收。

首次成功测试产物：workspace `.tmp/string-conversion-test-b4f2a449622249e197a961ed3460e5e6/`。可复用测试源码随仓库保留，日志/EXE/dump 不提交。

## 构建与交付状态

`Release|x86` 构建通过；新 DLL SHA-256：`5ADF70733881FDA59842A75C24BC6C53C160D9DBB469A109F63139C234DA75DE`。构建仅有已有 Detours PDB 缺失链接警告。

构建包含先前已交付的脸饰恢复及配套故障捕获。23:29 首次准备交付时 `BeiDou.exe` PID 21620 仍运行，因此没有覆盖客户端 DLL。用户随后要求提交并推送；23:43 再次确认游戏已退出，备份旧版 `81D492ECE7327CE991A4DEB4FFFC6E6B12C5714DA84F5164A353E579F9660A71`，将上述构建产物同步到 `BeiDou-Client/ijl15.dll` 并核对 SHA-256 完全一致。源码与客户端 DLL 分别在 `BeiDou-ijl15/BeiDou`、`BeiDou-Client/main` 管理；没有发布到远端游戏环境。

用户提出的“按剩余地址空间截断容量”方案本轮仅分析，没有实施；交付仍使用已通过回归的重新求长实现。备份及安装记录位于 workspace `_diagnostics/player-logs2-7z-20261014/`，不提交日志和二进制诊断样本。

实机怪物攻城/长时间运行尚未验证，不能以离线复现和构建通过承诺所有闪退已经消失。既有 `008DDF66` 空源 Copy 保护漏项没有在本轮改成跳过绘制；本次修复处理的是让正常资源名失效的上游转换错误，其他独立原因仍应按首次异常分别判断。
