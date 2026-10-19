# 2026-10-14 logs(2).7z：射手村八次闪退与空资源名称证据

调查日期：2026-10-14，时间按 Asia/Shanghai。来源为用户提供的 `logs(2).7z`、包内日志/10 份 dump、匹配 PDB 与 EXE/组件指令、服务端连接日志只读查询，以及独立 x86 组件实验。没有启动游戏、修改生产或替换 DLL。

## 本轮结论

**后续根因调查更新：** 已使用原版 EXE 转换函数与真实 WZ 组件，在日志同区间高地址复现“超大转换容量导致 32 位地址回绕 → 名称未写入 → 空键/目录对象/800401F8”的完整链路，并实现修复、通过离线回归与 Release|x86 构建。见[高地址字符串转换根因与修复](high-address-string-conversion-fix-20261014.md)。下文保留初次调查的原始证据与当时未确定项。

角色“如鱼得水蜜桃”（角色 ID 1127）的八个不同运行，在 **21:59:17 至 22:09:39** 均出现客户端致命异常。八条故障连接的服务端地图都是频道 1、`100000000`。不能把同一运行的首次异常、主循环和 unhandled 三份报告计算成三次闪退。

| 客户端时间 | PID | 连接后缀 | 已确认的失败 |
| --- | --- | --- | --- |
| 21:59:17.874 | 17504 | 29999 | E_POINTER；之前多次资源属性查询使用空键，状态栏父属性内目标节点仍存在。最近原始异常链为 `0048B335 ← 006D28F1 ← 006D3232 ← 006D9922`，完整 dump 未保留在本包 |
| 22:00:25.889 | 11708 | 30056 | Boss 血条四个 Canvas 转换均返回 E_NOINTERFACE，随后 E_POINTER 退出；本包只有文本报告 |
| 22:01:44.482 | 18468 | 30089 | Vector2D 工厂的空类名分支 `PCOM+5075/+4E67`，800401F8；本包只有文本报告 |
| 22:03:31.548 起 | 5584 | 30157 | 状态栏快捷栏背景 Copy 收到空源 Canvas，在 `Canvas+ED28` 访问地址 0；最终退出码 C0000005 |
| 22:06:00.439 | 1820 | 30204 | Boss 血条背景 Canvas 为空，在 `00533ACD` 抛 E_POINTER；有首次异常 dump |
| 22:07:13.486 | 13544 | 30247 | 同上，有首次异常 dump |
| 22:08:09.859 | 20316 | 30293 | 同上，有首次异常 dump |
| 22:09:39.906 | 21220 | 30344 | Vector2D 工厂空类名分支，800401F8；有主循环 dump |

两次空类名与[上一份 logs(6) 的 PID 13536](player-logs6-crashes-20261014.md)属于相同的直接失败分支。四次 Boss 血条、快捷栏绘制与[本地脸饰 Canvas 故障](local-face-accessory-crash-20261014.md)是不同调用点，不能用脸饰恢复已交付推断这里已修好。

今晚八次的 ijl15 模块标识均为 `6ACE6252 / SizeOfImage 179000`，匹配已交付的 `e838ec7` 基线；不是全部沿用最初旧版 DLL。10 份现存 dump 的完整模块表及包内文本均未发现完整 basename 为 `Write.dll` 的模块，不能把本案归为已识别的外挂 RichEdit 崩溃；这不等同于证明进程从未执行任何其他外部代码。

**新证据将问题收窄到：资源名/类名在运行时变空或查询返回对象类型异常；还存在一个已确认未覆盖的空 Canvas Copy 调用点。** 尚未定位导致名称变空的最初代码，也未证明八次全部共享一个上游根因。本轮只有调查文档和本地探针，没有新增诊断 hook 或把未验证的恢复补丁作为已修复结果。

## 1. 资源存在，但查询键已变为空

PID 17504 的 emergency 日志在 21:59:17.749 记录：

```text
stage=property caller=008DA4CE outerCaller=008D824C
hr=00000001 vtKnown=1 vt=0000 keyRead=1 key=""
owner=2926F2C4 ownerVtable=50C131C4
resource_property count=14 captured=14 complete=1
resource_child name="Lbracket" vt=000D object=3AD6644C vtable=50014140
```

原生 `008DA4A1` 取 StringPool `971`，内容是 `Lbracket`；`008DA4C9` 执行属性查询。实际观察却是可读的空键、S_FALSE/VT_EMPTY，而同一父属性完整采集的 14 个子节点中有 Lbracket、Rbracket、slash、percent 和数字 0..9，其画布对象也非空。

这已经不是“怀疑磁盘缺图”：现场目标节点存在，查询键异常。随后多个调用点仍以相同空键查询，最终运行以 E_POINTER 退出。但最近原始 E_POINTER 链位于 `0048B335` 等位置，不能把较早的状态栏空键记录直接写成最终唯一抛出点。

该样本仅保留文本，最初损坏的 StringPool/ZXString/BSTR 内容及写入者不在证据内。日志里的 keyRead=1 表明成功读到首个 NUL，不能进一步断定原始编码字符串何时丢失。

## 2. 快捷栏查询拿到目录对象，随后遗漏保护的 Copy 崩溃

PID 5584 的三份 dump 属于同一个故障：首次异常 22:03:31.548、主循环 22:03:31.693、unhandled 22:03:35.094。较大的 unhandled dump 为 23,457,486 字节，仍是选择性内存快照，不能视为完整进程内存。

首次抛错链：

```text
Canvas+ED28 ← Canvas+D603 ← Canvas+D5CF
← 008DDF6C ← 008D8260 ← 009E45F5 ← 009F6368
```

`Canvas+ED28` 是 `mov eax,[esi]`，ESI 为 0。原生帧 `0019EAE0` 保存：

| 栈槽 | 值 | 含义 |
| --- | --- | --- |
| `EBP-18h` | `4BB0552C` | 目标画布，非空 |
| `EBP-50h` | `0` | 源画布，空 |
| `EBP-74h` | `03F0B928` | 查询返回的原始 IUnknown |
| `EBP-4` | `1E` | 与 Copy 阶段一致 |

`008DDE74` 取 StringPool `9C8 = UI/StatusBar.img/base/quickSlot`，其查询返回的对象保存在 `EBP-74h`，Canvas QI 输出保存在 `EBP-50h`。dump 能读到原始对象 `03F0B928` 的虚表 `508172DC = NAMESPACE.DLL+172DC`：**返回的是目录对象，不是所需画布**。

更早同一帧刷新中的 `UI/StatusBar.img/gauge/graduation` 查询也记录了同一个原始对象 `03F0B928`，Canvas QI 返回 E_NOINTERFACE。此处日志中的 owner 是该观察器通用布局推导值，不用它解释父对象；依据是 source 指针、真实虚表与后续快照的局部变量。

`008DDF66` 是直接 `call [ecx+80h]` 的 IWzCanvas::Copy，向它传入了空的 `EBP-50h`。目前 `ResourceReadGuards` 只覆盖 `00424BC8` 的 Copy 包装、`004B7DBB` 的另一个直接调用及 `008DA50B` 的 Lbracket 专用调用，**不包含 008DDF66**。因此“已有空源 Copy 修复”不等于所有调用点均受保护。这是可复核的覆盖漏项；直接补防护也仍需处理前面的错误资源对象，不能宣称等于修复名称失效的根因。

## 3. 三份 Boss 首次异常明确空的是背景画布

PID 1820、13544、20316 的首次异常帧均保留 `CField::ShowMobHpTag(005336CA)` 内的 `EBP=0019E834`：`EBP-10h=0`，SEH 状态 `21`。`00533ABF` 检查此槽，`00533ACD` 抛 80004003，返回位置 `00533AD2`。graphics/ResMan 全局指针均非空，因此不是后面检查 graphics 的 `00533ADF` 分支。

背景来源为 `StringPool CD8 = UI/UIWindow.img/MobGage/backgrnd`。四个背景资源的 Canvas QI 返回位置 `005337A1 / 00533884 / 00533964 / 00533A47` 均记录 E_NOINTERFACE。原始 IUnknown 槽部分非空，不能把诊断日志中 `vtKnown=0/source=0` 当成实际查询返回空值：该 Boss 观察点刻意不猜局部 VARIANT 布局，0 是未采集字段。

| PID | mobId | color / bg | HP / MaxHP |
| --- | --- | --- | --- |
| 1820 | 8800000 | 1 / 3 | 61,394,741 / 66,000,000 |
| 13544 | 8800001 | 3 / 4 | 60,853,962 / 88,000,000 |
| 20316 | 8800001 | 3 / 4 | 10,792,374 / 88,000,000 |

参数来自首次帧，不是按地图猜测。HP 为正且不超最大值。匹配 PDB 显示 `ijl15+10AEA` 是 `BossHP::HookShowMobHPTag` 调用原生函数后的返回地址，自定义百分比绘制尚未执行，不能把堆栈上的 BossHP 符号直接当成百分比计算错误。

PID 11708 也有相同四个 QI 失败、`boss_resource_first_throw` 标记和随后的主循环 E_POINTER，但其对应 dump 虽在日志中记录写入成功，已经不在本包，不能声称已读取到同样局部变量。

## 4. 两次空类名与 logs(6) 一致

PID 18468、21220 的致命异常同 tick 最近栈均为：

```text
PCOM+5075 ← PCOM+4E67 ← 00447F80 ← 0043FF62
← 0044514B ← 00A03425
```

前次已通过同版 PCOM 指令及真实空类名输入证明：`+5075` 是类名长度为零时的抛错分支，早于模块查找；静态期望类名是 `Shape2D#Vector2D`。本案不能套用历史 `+52CF` 的模块加载失败结论，也不能解释为缺失 PCOM.dll。PID 21220 的 dump 里 `_com_error` 的值为 800401F8，实际栈与文本吻合；PID 18468 只有文本。

## 5. 独立组件实验：正常路径与空路径的区别

探针 `.tmp/player-logs2-resources.cpp` / `.ps1` 只加载本地正式 PCOM/ResMan/Canvas/NameSpace，挂载本地 Data；不加载 ijl15、不启动 BeiDou.exe、不连接服务器、不修改 IMG。

| 实际调用 | 返回结果 |
| --- | --- |
| ResMan GetObject 空字符串或 `/` | S_OK、VT_UNKNOWN、无 Canvas 接口，虚表 `NAMESPACE+172DC` |
| `UI/StatusBar.img/gauge/graduation` | S_OK、Canvas，340×31 |
| `UI/StatusBar.img/base/quickSlot` | S_OK、Canvas，466×80 |
| `MobGage/backgrnd`、`backgrnd2`、`backgrnd3`、`backgrnd4` | 均为 Canvas，分别 35×37、4×19、1×20、5×20 |
| number 属性查询 `Lbracket` | S_OK、有 Canvas |
| 同一 number 属性查询空字符串 | S_FALSE、VT_EMPTY、无 Canvas |

空路径实验得到与 PID 5584 现场相同的目录类虚表；结合 PID 17504 已捕获的空键、两次 PCOM 空类名，资源名称传递/生命周期成为更强的共同调查方向。**实验不是 PID 5584 实际路径文本的恢复**，其他目录路径也可能返回此类型，不能把“符合空路径行为”写成“所有背景实际路径都已证实为空”。

健康本地文件正常可读，也不保证玩家磁盘文件完全一致；玩家 IMG 原文件不在包内。本轮没有因为正常资源实验通过而排除整个客户端的运行时问题。

## 服务端关联与时钟

按八个精确 connectionId 查询，每条匹配 8 组，nextOffset=null；增量刷新服务端后 pendingFiles=0。八条均为 Connection reset 进入 exceptionCaught/closeMapleSession。没有发现对应玩法踢线理由；不能把这一清理过程里的 `reason=server` 当成主动踢人。

该玩家时钟与服务器有偏差：多数服务端 reset 的显示时间比客户端异常早约 1.1～1.2 秒，不能按两个未校准时钟的毫秒值倒推出“服务器先踢了才崩”。依据同一 run/connection 的致命异常、dump 和网络异常清理链分类为客户端闪退。

七次 C++ 异常后退出码为 0，原生循环转入退出流程并不抹去已记录的崩溃。PID 5584 的退出码则是 C0000005。22:09:58 启动的 PID 19216 在 22:17:50 退出码 0，本包未记录该运行的致命异常，**不把它硬算成第九次闪退**。

## 样本覆盖、版本与证据边界

- 压缩包 SHA-256：`d8576aca6a405c00323edeb0101721dc20a6bc90873a11a93a698184a5255c53`。7-Zip 完整解压校验成功，409 文件、31,873,272 字节。
- 21 份 crash 文本对应 14 个异常运行，含 10 月 11～14 日历史；今晚部分为上述 8 个运行。10 份实际 dump 只覆盖今晚 5 个运行，全部解析完成。
- 397 个文本文件共 4,691,814 字节完成关键词扫描，无 `pool_guard fault=`、`appearance.mix ... resource_failed`。无命中只代表没有记录该检测结果。
- 历史 6 个异常运行使用模块标识 `6ACA6C26 / 12F000`，与今晚新基线区分；不能用历史旧版崩溃证明新修复无效。
- 模块标识/PDB 匹配不是玩家 DLL 全文件哈希校验。压缩包没有玩家的 ijl15.dll 原文件。
- 大 dump 的 StringPool 全局指针可读，但池对象/数组不在捕获范围；工具的 null 结果是未捕获，不是证明 StringPool 本身为零。CDB 不自动理解 BDS1 自定义内存，本案首次帧 globals 由本地解析器补入后读取。

本地证据目录为 workspace `_diagnostics/player-logs2-7z-20261014/`：`archive-manifest.json`、`crash-report-summary.json`、`dump-inventory.json`、`boss-frame-details.json`、`canvas-frame-details.json`、`evening-emergency-excerpts.json`、`server-correlations.json`、`text-scan.json`、`offline-resources.txt` 和 CDB 输出。日志/dump/临时产物不提交。

后续具体工作应优先定位 StringPool→ZXString/BSTR→PCOM/ResMan 的名称异常来源，并评估 `008DDF66` 的真实资源恢复与空源保护。不能用吞全部异常、伪造空画布或关闭 Boss 血条来声称根因修复；本调查没有承诺已完成这些实现。
