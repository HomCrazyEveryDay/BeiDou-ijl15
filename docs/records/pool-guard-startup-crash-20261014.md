# 授权成功后游戏窗口未出现：内存池探针启动时序（2026-10-14）

## 现场与根因

用户本机在北京时间 00:09:09、00:09:11、00:10:31 连续启动，启动器显示“游戏已启动并确认授权”，但窗口没有出现。对应三个游戏进程 PID 为 17540、12936、18580，均在 DLL 初始化期间触发 `C0000005`，执行地址为 `00000000`。

诊断样本为本机 `BeiDou-Client/logs/ijl15-crash-20261013-160909033Z-pid17540-tid16640-codeC0000005-addr00000000.dmp` 及同目录后两次 dump。日志文件名使用 UTC。三个 dump 中 `00BDC9D0=00796EF9`，`00BF03E0=0`，`00BF02F4=0`。

用现场 DLL 对应 PDB 解析 PID 18580 的堆栈：

1. `ProcessEntryHook` 调用 `InitializeClientAtProcessEntry`，授权完成后安装 `PoolGuard`。
2. `PoolGuard::Install` 内的 `StampExistingLists` 调用原生 `ZFatalSection::Lock(00403166)`。
3. 原生锁从 `00BDC9D0` 取得回调；此时它是非空的懒初始化入口 `00796EF9`。
4. 该入口在 `00796F06` 调用 `00BF03E0` 中尚未初始化的系统函数地址，跳转到 0；栈顶返回地址为 `00796F0C`。

因此，旧 `LockReady()` 的“回调非空”判断不足。默认启用探针后，在游戏原始初始化开始前主动调用原生锁，造成启动崩溃。问题发生在 NPC 对话焦点修复安装前。

现场 DLL SHA-256：`164FA8E3400FF8E17F4D8544C6441AB1EF5DF9BA524C64CA9919A386AAFC1B69`。对应符号文件来自 `.tmp/npc-dialog-focus-20261013/dll/out/Release/ijl15.pdb`；符号解析结果保存在 workspace `_diagnostics/startup-null-20261014/symbols.json`。

启动器的授权确认事件在 DLL 初始化的开头触发，其成功提示不代表窗口已经出现。现场启动的是本地 `ZhuMengLauncher-Dev.exe` 2.0.23。本次修复针对游戏 DLL 的实际崩溃；没有改变启动器的授权协议或成功提示。

## 修复与回归

`LockReady()` 现在同时检查原生锁的 `Sleep` 依赖；回调仍为懒初始化入口时，还检查其 `GetSystemInfo` 依赖。依赖尚未就绪时，安装钩子不触碰未构造的原生池，不调用初始化入口。原生初始化完成后继续执行原来的内存池校验，不修改游戏的回调槽位，不关闭诊断，也不吞掉原生异常。

测试入口：`tests/PoolGuardTest.ps1 -ClientExe ../BeiDou-Client/BeiDou.exe`。

- 新回归映射本机真实 EXE 的锁初始化代码，保留非空初始化回调并清空其系统函数依赖。旧实现退出码为 `-1073741819`（`C0000005`），复现相同的空地址崩溃。失败产物位于 workspace `.tmp/pool-guard-test-7b77304a525a473f9842fe10b2d13622`。
- 修复后，缺少 `GetSystemInfo`、缺少 `Sleep`、回调为空三组启动边界均通过；安装时原生池保持未修改。
- 恢复依赖后执行真实初始化入口，再验证全部 12 种桶的原生/DLL 分配释放、无误报和故障检测仍有效。
- 原有四线程混合分配、重复释放、释放后写入、链表损坏、块头异常、日志异常隔离及诊断数量限制测试通过。
- 使用真实 `CrashReporter` 的进程入口模拟通过：三次故障继续执行，只生成两组报告和 dump。成功产物位于 `.tmp/pool-guard-test-239d1b6c65a8470ba280557490ab9603`。

测试只在独立控制台进程执行映射的原生组件，没有启动游戏入口、打开游戏窗口或联网登录。

## 本地交付与边界

- 基于 `.tmp/npc-dialog-focus-20261013/dll` 的已交付源码创建新构建目录，只替换本次 `PoolGuard.cpp` 和回归测试；保留 NPC 对话焦点修复。工作区其他未提交的 CharacterStatGuard、ResourceReadGuards 等改动未编入。
- 新构建目录：workspace `.tmp/pool-startup-fix-20261014/dll`；日志 `.tmp/pool-startup-fix-20261014/build.log`。`Release|x86` 构建通过，0 错误，47 条已有警告；构建内启动器父进程策略检查通过。
- 确认 `BeiDou.exe` 已关闭后，替换了本地 `BeiDou-Client/ijl15.dll`。构建产物与客户端文件 SHA-256 一致：`48671ACDD37D4EC9E5B51F9CE3AC4E90B9FA5275E7D4F4945B4CED48E6250743`。
- 原 DLL 及匹配 PDB 保存在 `exports/pool-startup-fix-20261014/ijl15-before-164FA8E3400F.{dll,pdb}`。旧 DLL 包含本次已证实的启动崩溃，不应当作可正常运行版本发布。
- 尚未发布到线上；实机窗口和登录行为等待用户重新启动确认。隔离回归与构建通过不能替代完整游戏实测。
