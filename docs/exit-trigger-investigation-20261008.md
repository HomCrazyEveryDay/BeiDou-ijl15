# 退出前触发原因续查（2026-10-08）

> 同日继续只读深查 gz 和四个原附件，新增 exit(0) 外层调用、0xDF 清理语义、登录条款退出及心跳先后关系证据。见 [玩家崩溃逐案复核与 gz 日志深查](gz-crash-rootcause-20261008.md)。不能把下文所有“退出”自动解释成已经发生了另一场异常掉线。

目标：继续查明 `004031FE` 之前为何掉线／退出。已有 PCOM 清理顺序修复只覆盖退出时的访问异常，本轮不把它当作最初退出的根因修复。

## 本轮实际结论

**月神冰、Fibonacci、梨涡允允、纸鸢逐风这五次样本的最初退出根因仍未证实。** 已补齐历史文本、按连接握手校准时钟、核对后台原文，并实现主循环离开前的证据捕获；没有足够证据据此修改玩法、背包、网络协议或服务端踢人逻辑。

| 玩家及报告时间（北京时间） | 更深入核对的结果 | 仍缺什么 |
| --- | --- | --- |
| 梨涡允允，10-05 01:43 | 断开前约 105 秒的武器卸装／重装均成功；连接断开没有服务器主动关闭请求，最后包为 HEAL_OVER_TIME；客户端后来处置场景 | 主动关游戏、网络关闭、主循环异常之间不能区分；不因武器操作接近就认定武器导致 |
| 纸鸢逐风，10-05 15:07 | 服务端先收到 `SocketException: Connection reset`，随后 exceptionCaught 才调用关闭；不是已证实的业务踢人。四个附件中存在这一份最终 dump | dump 已到 PCOM/CRT 退出阶段；没有保存最初退出触发者 |
| 月神冰，10-06 23:19 | 本连接没有心跳超时／业务主动关闭。最后死亡弹窗日志为 22:03 创建成功、22:04 关闭，距最终退出约 75 分钟。附近两条心跳超时属于其他角色和连接 | 不能把其他玩家超时或此前死亡直接算为本次根因；仍缺主循环退出原因 |
| Fibonacci，10-06 23:47 | 客户端时钟快约 14.485 秒；校正后后台断开到报告约 5.060 秒，场景处置约与断开同步 | 不能把原 19.545 秒差判为卡死或心跳超时 |
| Fibonacci，10-07 21:46 | 客户端时钟快约 16.406 秒；校正后断开到报告约 2.926 秒。同图石破天约早 1.331 秒断开，记录接入地址相同 | 两个连接接近断开值得保留，但不能单凭地址／时间认定同一电脑、同一人、网络故障或客户端共同根因 |

报告时间含 dump 写入延迟。时钟偏移估算来自同一个 connectionId 的服务端 `context-bound` 和客户端 `diagnostic_binding`，包含通信延迟，不是毫秒精度授时。Fibonacci 第一次运行 12 次握手偏移稳定在 -14.470～-14.485 秒，第二次 3 次为 -16.406～-16.410 秒，足以纠正十几秒的比较误差。

## 读取范围及缺口

- 先按 `tools/log-query` 增量 refresh，再定向搜索。refresh 后 server pending=6、client pending=21488，因此未用索引无命中证明全部历史正常。
- 补取原分类的 **33 次 `004031FE` 运行全部现存上传文本：230 个文件、26,390,751 字节**，包括 trace、lifecycle、emergency、buff、equipment 等。每份记录原始 SHA-256、size、startOffset=0、nextOffset=size，本地保存脱敏文本。
- 定向读取上述五次报告前 150 秒到后 10 秒的全部后台原文；扫描 10 份相关日期滚动文件及当前文件的固定大小快照，保存各文件 size/nextOffset。窗口共 450 行，没有只筛含角色名的 ERROR。
- 33 次 emergency 均有 `conditional_dump_config enabled=0`，没有 `diagnostic_ready`、`disconnect_event` 或首次异常记录。两份代表性旧 DLL 确实包含这些观察代码／标记，但这些运行没有启用该观察流程；当前共享设置默认也把 verbose lifecycle 和 conditional dump 设为 0。
- 四份附件的 27 个退出 dump 中，CWvsApp 单例 `00BE7B38` 都已清零，主线程栈顶部未保留可校验的 WinMain 原始调用帧／待处理异常值。不能用 PCOM 析构栈倒推出此前 C++ 异常，也不能用不知来源的旧栈数值充当证据。
- 五个点名样本中，附件只有纸鸢逐风 pid1408 的同次最终 dump；其余四次没有对应 dump。日志站也只提供上传白名单文本，不能假装已经检查了这些不存在的原始现场。

本地产物位于 workspace `_diagnostics/exit-trigger-20261008/`：`manifest.json`、`server-windows.json`、`aligned-timelines.json` 和 `main-disassembly.txt`。复核脚本位于 `.tmp/exit-trigger-investigation-20261008/`。远端动作全部只读，没有修改、重启或部署服务器。

## 为什么新增捕获点

原生 `WinMain` 在 `009F1CB8` 调用 `CWvsApp::Run(009F5C50)`，随后处理异常或执行 `CWvsApp` 清理，最后才走 CRT／PCOM 析构。等到 `ExitProcess` 或 `004031FE` 再抓现场，初始原因已经可能丢失。

原生主循环还会消费窗口消息处理阶段存下的 pending error：`CWvsApp+38` 转为 COM 错误，`+34` 按错误区间转为 `CDisconnectException`、`CTerminateException`、`CPatchException` 或 `ZException`。因此仅抓最终 `_com_error` 的生成点可能再次遗漏较早的资源调用。

新模块 `NativeExitDiagnostics`：

1. 精确校验当前 083 EXE 的 timestamp、大小、主循环入口、WinMain 调用点和 WM_QUIT 返回分支，再安装 hook。
2. 在主循环外包一层只观察的 SEH filter。只有异常确实越过主循环边界、需要外层处理时，才在**栈展开前**保存原异常记录、寄存器、调用帧、局部栈和应用状态；始终返回 `EXCEPTION_CONTINUE_SEARCH`，不改变游戏原处理方式。
3. 当前主循环线程最近 8 次 C++ 异常只保存在内存中；离开循环时输出其中最近 10 秒的类型、错误码和模块偏移。普通技能内部捕获的异常不写盘、不生成 dump。较早记录明确标为未证明因果。
4. 用实际 RTTI 识别类型后才解码错误字段。`CDisconnectException`／`CTerminateException` 的错误位于对象第一个 DWORD，`_com_error` 的 HRESULT 才在 +4；未知类型不猜 HRESULT，不输出任意对象内容。
5. 记录执行主循环线程的 `PostQuitMessage` 来源，以及主循环正常返回时的停止标志，帮助区分退出消息和异常逃逸。退出消息本身不等于“用户亲手点击关闭”。
6. 跟随现有 CrashDump 开关，默认开启；独立于 verbose lifecycle／conditional dump，避免再次因它们默认为 0 而漏证据。每进程最多 4 份主循环异常报告、16 条停止事件；栈溢出不在耗尽的栈上调用 dump writer。

文本仍使用既有 `ijl15-emergency-*.log` 和 `ijl15-crash-*_main-loop-N.txt`，符合启动器当前上传白名单；dump 为本地 `.dmp`，原始内存不上传。捕获文本标为 `native_main_loop_escape` / `fatalStatus=unknown_first_chance`，因为外层仍可能恢复，不能直接算额外闪退次数。

这是为继续定位根因补齐证据，不是已经修复了上述玩家最初退出的声明。

## 验证与交付

- `NativeExitDiagnosticsTest.ps1` 在独立测试进程、`/EHsc` 下通过：原生 EXE 入口真实 Detours 安装及错误版本拒绝；真实 C++ 异常原类型／错误码到达外层 catch；硬件访问异常继续到达外层 SEH；回调发生在局部对象析构前；100 次内部已处理异常只使用内存环；正常 PostQuitMessage 的退出码和消息不变；4 份报告上限；写报告失败不覆盖原异常；BDS1 原始上下文和文本反读通过。
- 应用主体由测试替身触发异常，没有执行游戏／安全／输入主循环，没有登录客户端。尚未验证玩家实机事故复现。
- 既有 `TargetedCrashSnapshotTest.ps1` 和 `DisconnectDiagnosticsTest.ps1` 通过，原五类捕获、去重、异常传播及网络错误保留未回归。
- `Release|x86` 构建成功，自带启动器父进程测试通过；只有既有 Detours PDB 缺失警告。

提交前于 2026-10-08 16:07（UTC+8）重新执行 `Release|x86` 全量 Rebuild 成功，构建自带启动器父进程检查通过；仍有既有 codecaves 整数收缩／未使用标签、EvanKillingWing 内联 SEH 和 Detours PDB 警告。确认客户端及退出监控进程关闭后，备份并同步本地客户端，产物与构建目录 SHA-256 一致：

| 文件 | SHA-256 |
| --- | --- |
| `ijl15.dll` | `C9F816DB7EF7CD153D07186186555BD9716544C51B994144A6D56501BEEBED4B` |
| `BeiDouExitMonitor.exe` | `33D5CD04B02F6BE561B5B252822D9440229F99F441844A47B4B6B89F85358ACE` |

DLL 保留 BD27 及上一轮四类修复。当前构建／同步清单见 `_diagnostics/crash-commit-20261008/delivery.json`，构建日志见 `.tmp/crash-commit-release-20261008.log`；先前 `3659720E...` 产物及其交付清单保留为历史记录。源码、专项测试和调查文档提交到 `BeiDou-ijl15/BeiDou`，配套产物提交到 `BeiDou-Client/main`；本轮仅本地提交，未推送或发布。离线专项回归已通过，玩家实机复现仍待验证。

下一步需要这几位玩家在包含此改动的版本上正常运行、再次发生原问题时的同次 emergency 和 main-loop 报告。先按错误类型／代码／原始调用帧定位具体退出来源，再修对应代码；不能把上传的最终清理地址重复当成根因。交给用户实测后不自行启动、操作客户端或轮询实测日志。
