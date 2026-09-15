# 默认退出监视与条件 mini dump

2026-09-15：用户授权默认开启。仅日志改动，保持极光恢复必现故障用于验收。

## 配置与行为

`config.ini` 的 `[debug]` 下，`enableLifecycleDiagnostics`、`enableExitMonitor`、`enableConditionalMiniDump` 缺省值均为 true，可逐项关闭。`enableCrashDump=false` 同时阻止条件 dump。无需为了默认开启修改本地配置。

- `BeiDouExitMonitor.exe` 与客户端 EXE/DLL 放在同目录。DLL 通过显式继承句柄列表只传递目标进程的等待/查询句柄；PID 被复用也不会误关联。辅助进程隐藏启动，WaitForSingleObject 等待进程实际结束，不轮询，不需要管理员权限。
- `ijl15-exit-<session>.log` 记录监视就绪、实际退出时间、退出码。游戏仍在运行或仅回到登录界面时不会伪造 process_exited；退出码只是证据，不直接等于错误分类。
- 条件 dump：同线程观察到 22161003 后 5 秒内，C++ first-chance 异常堆栈含原生龙动作加载返回地址（EXE+0xFECA9）。每会话最多尝试一次；不抓普通 C++ 异常，不修改异常处理结果。
- 条件 dump 强制使用紧凑 MiniDumpNormal + ThreadInfo + UnloadedModules，不使用全内存模式。文件标记 `_conditional`，报告类型 `conditional_first_chance`，即使原生处理器最终处理异常也保留现场。DbgHelp 写入互斥采用非阻塞方式，避免异常回调等待自身。
- 辅助进程在启动和目标退出后整理同目录 `ijl15-crash-*.dmp`，保留最新最多 10 份且合计不超过 256 MiB。只处理这一命名范围，不递归、不跟随目录链接，不删除被占用文件；占用造成暂时超限时留待下次整理。现有普通日志和 text 报告不属于此上限。

## 验证

Release|x86 构建同时生成 DLL 和辅助 EXE（DLL 工程的 AfterTargets=Build 构建依赖）。交付必须同步两者。

`tests/LifecycleLoggingHarness.ps1` 使用实际日志实现，验证：工作线程异常、正常退出、自终止、未处理崩溃、dump 写入失败、禁用、最终异常处理器被替换后的单次条件 dump，以及辅助进程捕获正常/自终止/崩溃/外部强杀退出码。条件筛选测试覆盖错误异常码、过期时间、没有技能时间和无目标堆栈。历史整理测试覆盖超过 10 份、257 MiB 文件及无关文件保留。全部通过。

实际空闲辅助进程私有内存 737280 字节，1 秒采样 CPU 增量 0 ms；这是测试机采样，不承诺任意设备绝对零开销。dump 触发仍会产生短暂停顿，真实游戏停顿尚未测量。

新增筛选曾改变回调函数栈布局，使原有堆栈捕获回归测试失败；现已把筛选捕获分离为 noinline 函数。DisconnectDiagnosticsTest 再次通过，100 次异常压缩记录约 46 KB，保留原始错误码和异常传播。

## 交付状态

产物和独立测试已完成，客户端 PID 11972 仍在运行。遵守用户“先不关闭”和工作区规则，没有替换运行中的 DLL，也没有启动/操作游戏或结束该进程。待客户端关闭后同步 `out/Release/ijl15.dll` 和 `out/Release/BeiDouExitMonitor.exe`，逐项校验哈希，再由用户以极光恢复实测 dump/退出记录。

2026-09-15 11:17：用户确认关闭后，再次检查无 BeiDou/MapleStory 进程，已安装两项产物并核对构建与客户端哈希一致：

- `ijl15.dll`：`0DAE9C78E203F127350169CC415218D064BA7D37642A495909FFC9535176185A`
- `BeiDouExitMonitor.exe`：`CD995175FF1BDB92E8B8EB211202FCE8759F1D0D4132E7190E943D4F0EEDBA12`

替换前文件备份于 `exports/evan/logging-validation/install-backup-20260915-111746`。本机生命周期开关为 true，退出监视和条件 dump 未配置覆盖值，使用默认开启。未启动或操作客户端，等待用户复现。

## 11:20 实机验收通过

会话 `20260915-032002654Z-pid12560`，目标 PID 12560，监视 PID 15392：

- 11:20:22.930 记录 22161003、1 级；22.936 记录 C++ 异常 E06D7363，堆栈包含 0x4FECA9 / 0x96CB8D / 0x969224。
- 22.969 触发条件 dump；23.179 写入成功，错误码 0。文件 102084 字节。独立解析 MINIDUMP 头、目录及异常流通过：flags=0x1020，线程 8908、异常 E06D7363，包含线程、模块、内存、异常等流。并非仅有空文件或成功日志。
- 28.629 程序请求 ExitProcess(0)；28.743 在清理阶段发生 C0000005；30.705 请求 TerminateProcess(C0000005)。独立监视器于 30.765 确认进程实际结束，最终退出码 C0000005。这区分了初始技能异常、退出请求与最终终止结果，不能把清理阶段异常当作最初故障。
- 实际条件抓取至写入成功间隔约 210 ms，包含同步处理与日志开销，不是精确的游戏帧停顿测量。只生成一份条件 dump。
- 文本报告原先仍使用全局 dumpType 标签 mini-triage，二进制已正确使用条件 mini flags。已修正报告标签为 mini-conditional，Release|x86 构建通过，确认无客户端进程后同步 DLL 并校验一致。

本轮技能、资源未改。退出码监视和条件 dump 的真实客户端验收完成，后续技能修复仍按用户明确指示执行。
