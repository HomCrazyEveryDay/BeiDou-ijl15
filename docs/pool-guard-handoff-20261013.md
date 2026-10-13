# 2026-10-13 Claude 闪退调查接续与本地交付

来源：本机 Claude 会话 `3449877f-dc70-45cd-a40b-22ed99dc60b0`、当前源码、正式 EXE、[结论独立复核](crash-claims-verification-20261013.md)。会话中用户已同意内存池诊断、Boss 血条限流和三处补丁修正；Claude 在留下部分 PoolGuard 源码后因服务错误中断。本次补完上述本地范围，未发布或访问生产，未启动或操作游戏。（更正：本节初稿写作"未提交、推送"，实际随后已提交并推送为 DLL `244e1f5`、客户端 `b194d6cb`、服务端 `5afaf11a6`。晚间修订见文末。）

**三处独立的坏指令补丁已修正，诊断捕获能力已在隔离测试中验证；多人 Boss 异常的最初写坏者仍未找到。** 本文的 dump 是人工故障测试产物，不是新的玩家事故证据。

## 已交付内容

1. `Client.cpp`：`004D59B2` 的高度立即数写入改为 `+2`；删除落在 `0064061D` 除法上的重复写入；删除将 `00A5FC2B` 的 `mov eax,SEH-handler` 改成 `add eax,SEH-handler` 的字节写入。后两者不是资源字符串根因的证明。
2. `PoolGuard.cpp/.h`、`PoolGuardCore.h`：检查匿名、char、wide 三个原生小块池，以及 DLL 的 `ZAllocEx` 复刻池。启动时校验 EXE 标识和六个入口签名，再以同一事务安装。DLL 项目文件、启动入口和 CrashReporter 接口均已接好。
3. 服务端 `Client.announceBossHpBar`：对同一连接、同一 Boss 合并 100 ms 内的更新；保留窗口内最新一次血量并安排尾次发送。首次显示、选中另一个 Boss、死亡通知立即发送。继续使用原来的五秒目标选择规则，限流计时独立于攻击更新的 aggro 时间。详见 [服务端记录](../../myself_BeiDou_Server/docs/boss-hp-throttle-20261013.md)。

## 诊断边界及开关

内存池探针**默认开启**（晚间修订，原为默认关闭）。只有游戏启动前继承到环境变量 `BEIDOU_POOL_GUARD=0` 时才不安装；关闭时不改空闲块内容、不安装原生分配/释放探针。既有首次异常和崩溃捕获保留原开关。

启用后在空闲块内保留 canary、释放调用者、序号，校验块头和空闲链表头部两个节点；重复释放采用独立的定长地址表佐证，避免把用户数据恰好等于 canary 当作重复释放。表碰撞会漏掉部分证据，不作完整覆盖承诺。新分桶/启动前已有链表做有界初始化，日常不全堆扫描。

检测异常后跳过坏释放，或丢弃相应空闲链表、让分配器申请新块。这会泄漏被隔离的块，也会改变复现时序，是取证期间的保护措施，不能当作长期根因修复。

每个进程最多记录 24 组故障元数据、尝试 2 份 mini dump。日志写入放在池锁之外，有重入和日志故障保护，不新增逐帧/逐包记录，不输出任意块的原始文本或 payload。dump 沿用现有诊断目录与保留策略，并受启动器的 `CrashDump` 设置控制。

记录中的 `caller` 是检测时调用者；canary 完整时的 `freedBy` 是先前释放者。两者都不必然是最初覆写者。发生覆写到再次分配/释放之间可能有延迟；只改活跃块、未触及检查字段、地址表碰撞、异常大块头等情况可能不被本探针发现。错误不存在于日志中不代表内存没有损坏。

## 验证

- `tests/ResolutionPatchTest.py` 读取真实 EXE 字节与当前补丁源码，在隔离缓冲区检查 600、720、768、800、900、1024、1050、1080、1440、2160 十种高度的 `cmp`、`idiv`、后继指令及 SEH 装载边界；通过。未修改 EXE。
- `tests/PoolGuardTest.ps1` 在独立进程中映射原生分配器代码，只执行分配器，不执行游戏入口。覆盖关闭状态、安装前分配、入口校验、12 个桶、原生和复刻池互转、大块/null、4 个并发线程、合法补码块头、活跃数据碰巧含 canary，以及人工重复释放、空闲块改写、坏链和坏头；通过。
- 元数据记录数和 dump 尝试上限、日志/dump 写入失败隔离已通过。另一个使用真实 CrashReporter 的测试进程在三次人工故障下生成两份实际 dump，合计 173,390 字节，报告为 `pool_guard_detection`，随后进程继续运行。
- 本机一次一百万次分配/释放对的探针结果：未启用约 31.3 ns/对，启用约 120.4 ns/对，增加约 89.1 ns/对。仅为该机器的独立探针结果，不代表游戏帧率或所有负载下的成本。
- DLL `Release|x86` 构建成功。最终增量构建 0 错误，3 条既有 Detours PDB 缺失警告。
- JDK 21 Maven 编译和 `ClientBossHpBarTest`（13 项）、`ClientMapRescueTest`（12 项）通过，共 25 项。模拟每秒 40 次同 Boss 更新得到该秒 10 次发送及末尾 1 次最新值刷新；覆盖死亡、目标切换、地图实例切换、换线/断线以及旧定时器不得回放旧血条。
- 本地服务端经现有 `launch.ps1 -Background -Restart -SkipMySql -SkipAdminUi` 重启，17:38:28 启动完成；8686、8484、7575、7576、7577 由新 Java PID 18772 监听。使用本地 dev 配置和 localhost 数据库。生产尚未加载此限流。

## DLL 交付与留档

为保留上轮已交付的首次异常/hitAfter 取证能力，同时排除工作区中其他未完成改动，本次复用上轮交付的隔离源码快照，叠加本次 14 个源码/测试/项目文件；没有把无关 CharacterStatGuard 等脏改动混进交付 DLL。

- 基线提交：`342010d145119ac96ed4243d63957040d9643d37`，加上上轮交付清单中的本地诊断改动。
- 构建源码：workspace `.tmp/pool-guard-handoff-20261013/dll/`。
- 完整来源及 391 个文件 SHA-256：`.tmp/pool-guard-handoff-20261013/source-manifest.json`。
- 交付：确认 `BeiDou.exe` 已关闭后，17:37:43 同步至 `BeiDou-Client/ijl15.dll`；构建产物与客户端 SHA-256 相同。
- 新 DLL：810,496 字节，`AD102635B1BDF901484B07D7DF27EB9A17D71A30A828639986BEC57399DF683E`。
- 替换前 DLL：`D57DE0EA85F525C3EF955CCB92AFF0554277C2FBBB5D8DEFE136CDFD435FBF8C`。
- 原 DLL、新 DLL、匹配 PDB 和交付清单：`_diagnostics/horntail-two-attachments-20261013/pool-guard-delivery/`；其中 `ijl15-before.dll` 可在游戏关闭后恢复。临时样本不提交。
- 构建/测试记录：`.tmp/pool-guard-handoff-20261013/{build-final.log,tests.log,server-tests.log,server-restart.log}`。

## 用户实机验证

晚间修订后探针默认开启，正常从启动器启动即可，无需设置环境变量；启动器的崩溃转储设置应保持开启。测试所用服务器由用户在现有启动器中选择，本次服务端限流只在本地生效。若需排除探针影响做对照，可在 PowerShell 中先 `$env:BEIDOU_POOL_GUARD = '0'` 再启动启动器，启动后 `Remove-Item Env:\BEIDOU_POOL_GUARD`；这只影响该次启动。本轮未自动启动游戏或启动器。

1. 启动日志应出现 `pool_guard_config enabled=1 installed=1`。`installed=0` 表示探针未装好，不能把这次运行当作内存池取证已启用。
2. 检查常用分辨率（特别是高度 768、1050）的界面；在可控测试环境复现原多人/Boss 场景。验证最后一击后的血量能刷新，死亡清空、换地图/换 Boss 后没有旧血条回来。
3. 若异常出现，通过现有启动器诊断导出保留整次运行的日志及 dump。新增检索键为 `pool_guard fault=`、`pool_guard_frame`、`pool_guard_detection`；快照文件带 `_pool-guard-1` 或 `_pool-guard-2`。同时保留既有资源首次异常记录和 DLL 哈希，便于关联先后顺序。

交付后按 workspace 规则等待用户反馈，不自动登录、操作游戏或反复轮询本次测试日志。

## 晚间修订（2026-10-13 23:00 +08:00）

用户审阅后要求：探针默认开启、修正换行噪音、补充块标记元数据、服务端不再强引用上次血条对象，并更正本文"未提交"的描述。

- **默认开启**：`dllmain.cpp` 改为未继承 `BEIDOU_POOL_GUARD=0` 即安装。日志仍为 `pool_guard_config enabled=… installed=…`；`installed=0` 表示签名或 Detours 安装失败，此次运行不能当作已取证。
- **报告元数据**：`pool_guard` 行增加 `headerDelta`；`pool_guard_block` 行增加 `canaryDelta`（±0x100 内输出有符号差值，超出记 `replaced`）与 `changed`（`canary`/`freedBy`/`freeSeq` 中哪些标记字已不再像释放时写入的值，新切出的块的零值不计）。`freedBy` 改为只要该字仍指向已加载模块即输出，因此仅 canary 被原地加减时仍保留先前释放者。小的 `canaryDelta`（如 -1/+1）与对已释放对象做引用计数增减的形态一致，但这是推断线索，不是写入者证明。仍不输出块内容文本或原始 payload。
- **换行噪音**：`244e1f5` 将 `Client.cpp`、`ezorsia.vcxproj`、`ezorsia.vcxproj.filters`、`dllmain.cpp` 的混合 CRLF 整体改为 LF（`vcxproj` 另加了 BOM），diff 显示约 2,900 行而实际只改约 20 行。已按 `342010d` 逐行恢复原换行和 BOM，字节比对后只剩真实内容差异（`Client.cpp` 3 行替换，其余为新增行）。提交时须使用 `git -c core.autocrlf=false add`，否则 autocrlf 会再次把这些文件规范为 LF。
- **测试**：`PoolGuardTest` 的空闲块改写用例改为原地减 1，并断言日志含 `canaryDelta=-1 changed=canary freedBy=`；该测试与 `ResolutionPatchTest.py` 通过。本次一百万次分配/释放对：未启用约 28.7 ns/对，启用约 115.4 ns/对（另一次运行增加约 166 ns/对，计时波动较大）。
- **服务端**：`Client` 用弱引用记录上一次已发送的 Boss 目标（角色、地图、怪物），不再因限流状态延长已离开地图实例或已死亡怪物的生命期；待发送的一条仍为强引用，最多保留 100 ms。`ClientBossHpBarTest`（13 项）、`ClientMapRescueTest`（12 项）通过；本地服务端经 `launch.ps1 -Background -Restart -SkipMySql -SkipAdminUi` 重启，22:57:27 启动完成，8686/8484/7575–7577 由 PID 19804 监听。生产未加载。
- **DLL 交付**：以 `244e1f5` 为基线的独立 worktree（workspace `.tmp/claude-dll-build`），只叠加本次 `PoolGuard.cpp`、`dllmain.cpp`、`tests/PoolGuardTest.cpp`，未混入工作区中 CharacterStatGuard 等未完成改动。`Release|x86` 构建 0 错误，无新增警告。确认 `BeiDou.exe` 未运行后同步至 `BeiDou-Client/ijl15.dll`：811,008 字节，SHA-256 `AE4DB16D7FF426918B6B744008761393C80466C9362C5F45A0975AAAD6F1B825`，与构建产物一致。替换前 `AD102635…683E` 备份为 `_diagnostics/horntail-two-attachments-20261013/pool-guard-delivery/ijl15-before-default-on.dll`。
