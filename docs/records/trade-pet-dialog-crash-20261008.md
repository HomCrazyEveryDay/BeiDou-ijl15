# 交易入口、宠物用药设置及其他弹窗崩溃排查（2026-10-08）

> 2026-10-09 已实施正式 DLL 修复；实现及验证见文末“正式修复”。10 月 8 日的调查统计和未归因范围保留，不能用本次定点修复代表全部崩溃已解决。

## 结论与验证边界

已确认一条共同的客户端缺陷：当前 BeiDou.exe 的 `CWvsApp::Run` 跳过了局部堆指针的赋值／分配路径，却仍在循环返回时执行 `free([ebp-34h])`。关闭模态弹窗时，嵌套消息循环正常返回便可能把栈上的残留值交给堆释放函数，导致 ntdll 访问异常。

10 月 8 日 16:09 提交 `7edc7bb` 新增的 `NativeExitDiagnostics::RunObserved` 全局 hook 覆盖了主循环和弹窗的嵌套循环。玩家现场均可见这个新调用帧，报告中的 DLL 构建标识与本地调查基线一致。**“原 EXE 已存在未初始化释放缺陷”已由指令和离线执行确认；“新增 hook 改变栈布局而集中暴露缺陷”是结合版本、调用栈和时间的高置信解释，尚未做同一玩家机器的旧／新 DLL 实机 A/B。** 不能把主循环捕获次数直接当作独立闪退次数。

10 月 8 日调查阶段未修改正式 DLL 源码／客户端交付文件，未写入或重启远端服务。当时的候选修复只在隔离测试进程的映射镜像中验证；文档提交 `d39a478` 不包含正式修复。10 月 9 日的后续实现另见文末。

## 日志来源及覆盖

- 按 workspace 流程先执行 `tools/log-query.ps1 refresh --source both`。返回 server pending=4、client pending=23793；因此未用索引无命中证明完整历史正常。
- 针对性读取 la_dr `/srv/beidou-client-logs/client` 中当时全部 1540 份崩溃文本，共 34,376,787 字节，无读取错误。按报告时间筛选北京时间 10 月 8 日，SHA-256 去重后为 164 份；没有获取玩家的原始 `.dmp`。
- gz 只读核对：仓库版本 `688c7bd`，Java 于当天 17:14:12 启动。完整扫描当前 `out.log` 的固定快照 6,477,066 字节，nextOffset=size；该文件覆盖当天 00:00:03 至 21:34:59。分析窗口为当天 12:00 起，未发现当日另有 `20261008/out*.log`。
- 本地证据：workspace `_diagnostics/trade-pet-crash-20261008/client-reports.json`、`server.json`、`summary.json`、`example-latest.txt`。样本包含文件路径、大小、哈希和读取终点；时间为报告时间，玩家设备可能存在时钟偏差。
- 复核脚本：workspace `.tmp/trade-pet-crash-20261008.py`；离线探针：`.tmp/trade-pet-loop-probe.cpp`、`.tmp/trade-pet-loop-probe.ps1`。

## 新版本中的共同故障

DLL PE timestamp `6AC74F20`、SizeOfImage `0012C000`，本地 `BeiDou-Client/ijl15.dll` SHA-256 为 `C9F816DB7EF7CD153D07186186555BD9716544C51B994144A6D56501BEEBED4B`。玩家报告通过 timestamp 和 image size 关联构建；报告未提供玩家 DLL 完整哈希，不能声称逐台核验 SHA-256。

| 范围 | 结果 |
| --- | --- |
| 新构建全部报告 | 132 份，55 个 clientRunId；其中 unhandled 为 34 份／34 次运行 |
| 明确保留 `009F696D` 错误释放调用帧的报告 | 71 份，35 次运行，16 个安装标识 |
| 上述 35 次运行有最终 unhandled 报告 | 25 次；其余不能只凭主循环快照断言最终闪退 |
| 按每次运行最早共同故障现场分组 | 角色右键菜单 12 次，宠物设置弹窗 11 次，其他弹窗 12 次 |

安装标识不是玩家身份；姓名来自同一 `connectionId` 的服务端记录。代表样本如下，时间按客户端报告 UTC 转北京时间，未作毫秒级校时：

| 时间 | 服务端关联角色 | 路径 |
| --- | --- | --- |
| 21:02:02 | 勇气 | 角色右键菜单返回 `009502C7` |
| 21:09:54 | 桃气 | 同上 |
| 21:13:53 | 巨頭大亀 | 同上 |
| 21:24:53 | 侠气 | 宠物药水设置返回 `007FF89E` |
| 21:26:26、21:27:54 | 糖果呀 | 宠物药水设置返回 `007FF89E` |

角色菜单在 `009502C7` 从模态调用返回后才按所选命令分支。因此可确认菜单关闭路径损坏；仅凭堆栈不能逐次还原玩家选择的是交易还是该菜单中的其他命令。

宠物路径可进一步静态核验：`007FF5FE` 调用 `0058E0A8`，后者发送 `CHANGE_KEYMAP=0x87`、类型 2，并存储 `CFuncKeyMappedMan+3C4`；相邻 HP 路径调用 `0058E03C`、使用 `+3C0`。这与服务端 `KeymapChangeHandler` 的宠物自动 HP／MP 药水配置对应。**这些样本是拖入／设置宠物药水时的弹窗路径；不能直接宣称已确认所有自动吃药 `PET_AUTO_POT=0xAB` 时的掉线都相同。**

糖果呀 21:27 的同次连接为 `16817cdb2091487e838d01ad2f67574c-8753`。客户端先记录堆访问异常，服务端随后记录 `transport_closed_without_recorded_server_request`；该连接没有记录业务主动关闭请求。其他样本中的 `Connection reset` 是连接异常证据，不可单凭该文本认定网络是根因。

## 指令、符号与缺陷机制

以糖果呀上述报告为例：

```text
exceptionCode=C0000005
exceptionInformation1=FFFFFEFF
EAX=FFFFFF00, EDX=FFFFFF00
ntdll.dll
  <- 00A61ECC       CRT free -> HeapFree
  <- 009F696D       CWvsApp::Run 返回清理
  <- ijl15+016C41   NativeExitDiagnostics::RunObserved +81
  <- 004EDBCB       模态弹窗的嵌套消息循环
  <- 007FF89E       宠物 MP 药水设置弹窗
  <- 004F4D32
  <- 004EFC66
  <- ijl15+043007   InventoryItemDropped +147
```

`ijl15+016C41`、`+043007` 通过当前 DLL／PDB 的 DbgHelp 符号查询解析。指令来自当前客户端 EXE 的离线映射：

```asm
009F5CA3  jmp 009F5FDB       ; 已有 EXE 跳转，跳过分配／赋值区
... 被跳过的区间 ...
009F5E78  mov [ebp-34h],eax ; 原指针赋值
...
009F6965  push [ebp-34h]    ; 正常退出循环仍执行
009F6968  call 00A61DF2     ; free
009F696D  pop ecx
```

函数入口的其他局部变量初始化没有覆盖 `[ebp-34h]`。仓库 `Client.cpp` 旧注释也记载此处应跳过 free，但当前 EXE 实际保留该调用；不能把注释当作已安装补丁。

最新诊断 hook 从函数入口统一拦截 `CWvsApp::Run`，会同时包裹最外层循环和 `004EDBC6` 等调用的模态循环，而不仅是退出游戏。这解释了为何用户体验为点击交易、调整宠物药水以及其他弹窗均可能出问题。异常落在 ntdll 不表示 Windows 自身就是故障源。

## 离线复核结果

独立 x86 探针映射当前 EXE，执行实际函数前导、局部初始化、现有跳转和清理尾部；跳过游戏循环正文，把 free 调用替换成只记录参数的观察函数。保留真实现有诊断 hook 安装，并分别测试无 hook、有 hook和候选修复。

| 预置栈残留 | 原生清理参数 | 当前 hook 下清理参数 | 映射镜像中初始化指针后 |
| --- | --- | --- | --- |
| `00000000` | `00000000` | `00000000` | `00000000` |
| `FFFFFF00` | `FFFFFF00` | `FFFFFF00` | `00000000` |
| `12345678` | `12345678` | `12345678` | `00000000` |

测试证明未初始化指针会沿真实清理路径流入 free；没有真的把非法地址交给系统堆，也没有运行游戏、登录或模拟交易，因此这是缺陷机制验证，不是玩家完整闪退复现。既有 `NativeExitDiagnosticsTest.cpp` 用 `LoopFixture` 替代主循环正文，验证了异常捕获，却没有覆盖本次原生返回清理缺陷。

候选修复是在已确认分配路径被跳过的镜像上，进入循环前将 `[ebp-34h]` 初始化为 nullptr，保留原来的 `free(nullptr)` 和消息循环行为。正式实现须校验 EXE 及目标指令，拒绝其他布局，保留长期崩溃捕获，并增加真实清理路径回归；不宜全局吞掉 ntdll 异常。

## 尚未归因及 10 月 8 日调查交付状态

新 DLL 的另外 9 次最终 unhandled 尚未以本次共同调用帧关联：5 次仍为 `004031FE` 退出清理，4 次 ntdll 报告缺少本次关键调用链。它们需要独立续查，不能将全部归为上述原因。10 月 8 日更早旧 DLL 的其他崩溃也不在本次缺陷修复已验证范围内。

10 月 8 日仅完成只读远端调查、离线探针和本文档，当时未应用候选修复或发布 DLL。此后的正式修复记录如下。

## 正式修复（2026-10-09）

`NativeLoopCleanupFix.cpp` 在 DLL 启动期间、安装 `NativeExitDiagnostics` 之前，将已核实的跳转和 NOP 区改为：

```asm
009F5CA3  mov dword ptr [ebp-34h],0
009F5CAA  jmp 009F5FDB
```

仅在当前 083 EXE 的架构、PE timestamp、映像大小、函数入口、跳过分配的原跳转／填充以及清理调用全部匹配时安装。MOV 和 JMP 不改变寄存器或标志位；保留原 `free(nullptr)`、停止标志、WM_QUIT、封包及弹窗返回值。补丁安装后恢复页面权限；写权限失败不写入，刷新指令缓存或恢复权限失败时回滚补丁字节。

修复独立于 CrashDump 开关启用。若不满足修复条件，启动日志记录 `native_loop_cleanup_fix installed=0`，且不再向该循环安装新的诊断调用帧。匹配时记录 `installed=1`，原主循环异常捕获、dump 及其他长期诊断继续保留。没有逐帧日志、全局堆释放拦截或吞异常处理。

### 验证

- `tests/NativeLoopCleanupFixTest.ps1` 通过：执行本地 EXE 的真实函数前导、局部初始化、跳转和清理尾部，以三种栈残留值证明旧逻辑会把残留值传入 free；调用正式修复安装函数后，真实清理参数均为 nullptr，并调用测试进程的 CRT `free(nullptr)`。
- 同一测试覆盖诊断关闭／开启、三层嵌套模态循环、原生 WM_QUIT 分支及退出码、调用方寄存器／停止标志、错误版本和错误指令拒绝、重复安装、页面权限恢复；正常清理没有生成异常报告。测试替换游戏循环正文，不启动游戏入口或登录。
- `tests/NativeExitDiagnosticsTest.ps1` 通过：原 C++／SEH 异常仍向外传播，栈展开前捕获、报告写入失败隔离、四份报告限制及正常退出记录继续有效。

服务端逻辑和 WZ/XML/IMG 没有改动：根因发生在客户端共用消息循环的局部指针生命周期，与交易数据、药水数值及资源内容无关。用户实机仍需验证右键交易、设置宠物 HP／MP 药水、关闭其他原故障弹窗及正常退出；不自动启动或操作客户端。

### 构建与客户端交付

`ezorsia.sln /t:Rebuild /p:Configuration=Release /p:Platform=x86` 成功，构建自带的启动器父进程策略测试通过；仅有既有 codecaves 整数收缩／未使用标签、EvanKillingWing 内联 SEH 和 Detours 缺 PDB 警告。构建日志位于 workspace `.tmp/native-loop-cleanup-release-20261009.log`。

确认 `BeiDou.exe` 关闭后已备份旧 DLL，并将产物同步到 `BeiDou-Client/ijl15.dll`。两处 SHA-256 一致：`F76A1D9CDA6CE7F81FF2742D99AD64167FECFE9E39C22401A397B55D2C99EC85`。`BeiDouExitMonitor.exe` 与客户端现有文件哈希同为 `33D5CD04B02F6BE561B5B252822D9440229F99F441844A47B4B6B89F85358ACE`，无需配套修改监控程序。

本地备份及交付清单位于 workspace `_diagnostics/native-loop-cleanup-fix-20261009/`，备份目录名使用 UTC。回滚本地 DLL 时先关闭客户端，再从清单记录的备份路径恢复。本次源码／测试／文档提交目标为 `BeiDou-ijl15/BeiDou`，产物提交目标为 `BeiDou-Client/main`。Git 推送不等于启动器更新包发布；本轮不部署远端服务，不发布更新包，玩家实机验证仍待用户执行。
