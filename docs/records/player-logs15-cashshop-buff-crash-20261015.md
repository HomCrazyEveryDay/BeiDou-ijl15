# 2026-10-15 logs(15)：进入商城后晚到的神圣之火增益包导致闪退

## 结论

本包确认 **1 次真实崩溃**：角色“米格”在 2026-10-15 22:27:24.590（玩家电脑时间，UTC+8）进入商城后，收到 `GIVE_BUFF(0x20)` 神圣之火增益包。商城已经释放地图中的 `CUserLocal`，原生增益处理函数仍不判空地读取其虚表，在 `BeiDou.exe!00A20586` 读地址 0，最终以 `C0000005` 退出。

这次有故障发生时的寄存器/栈、退出后的较大 dump、实际包体、前序商城消息和进程退出码，直接故障链已闭环。服务端源码存在商城切换与增益发送未统一隔离的竞态窗口；具体是哪名施法者、哪个任务线程产生了这条包，现有现场未记录，不能据此指定某个玩家或任务。

玩家已加载高地址字符串修复，`string.conversion.fix ready=1`。此次空指针来自地图角色生命周期，与此前资源名称转换失败的指纹不同。两份 dump 是同一 PID、同一异常前后两阶段捕获，不是两次崩溃。

初次分析仅保存证据；用户随后授权修复。现已实现服务端商城/增益处理隔离与客户端 `TemporaryStatGuard`，通过 23 项服务端测试、原生故障指令与分派隔离测试，完成 `Release|x86` 构建及本地 DLL 同步。未运行游戏，未写生产，未发布，未提交推送；尚未完成实机验收。实现与测试边界见 [配套修复记录](../../../BeiDou-Server/docs/records/cashshop-buff-transition-fix-20261015.md)。

## 样本、版本与范围

- 原附件：`logs(15).zip`，SHA-256 `1d77ea24bd7c7ff1f56726bfe89d6e31273399455ff1032385fb4350bf9be3b0`。
- 解压 172 个文件：87 个 `.log`、81 个上传元数据 `.json`、2 个 `.txt`、2 个 `.dmp`；另有 4 个目录条目。全部 87 份日志已解码扫描，两个崩溃报告与两个 dump 已交叉读取。
- 诊断目录：workspace `_diagnostics/player-logs15-20261015/`，样本和探针产物不提交。
- PID `4120`，线程 `18768`，run `3bad730d41b5dc718acaec60a6b9a89a`，连接尾号 `34916`。服务端关联角色“米格”，角色 ID `1145`，崩溃前地图 `100000000`（射手村）、频道 1。
- EXE timestamp `4B7C15C9`；DLL timestamp `6ACF9ED2`、ImageSize `17C000`。dump 中 DLL 的 CodeView GUID/age 与已交付 `exports/string-conversion-fix-20261014/ijl15.dll`、PDB 一致。该交付 DLL SHA-256 为 `5ADF70733881FDA59842A75C24BC6C53C160D9DBB469A109F63139C234DA75DE`。
- 日志中的 `compiled=Oct 14 2026 00:54:23` 是编译单元的文本，不可单独用来判定整份 DLL 版本；本次以 PE/CodeView 和修复安装记录确认。
- dump 模块列表共 113 项，未见此前的 `Write.dll`。本案已有独立故障证据，不需要假设该插件参与；这不代表模块列表能证明所有形式的外部干预均不存在。
- 退出监控里其他已结束会话记录为退出码 0；最新 PID `4576` 只有 monitor_ready。退出码 0 不是对玩家所有历史体验“无异常”的保证，本包只有 PID 4120 有明确异常退出证据。

服务端用 `tools/log-query/query.py` 增量刷新后 `pendingFiles=0`，按上述 run 查询 26 组索引事件，`nextOffset=null`，再读取故障附近原文。没有把全局缓存中其他客户端源未完成的扫描当作已完成。五组连接绑定时差为服务端减客户端约 -0.20 至 -0.25 秒；不要沿用 logs(14) 的 3 分 56 秒偏差。

## 可复核的现场顺序

以下均为玩家日志换算 UTC+8，时间误差不影响同一进程内的先后顺序。

| 时间 | 证据 |
| --- | --- |
| 21:10:34.592 | `category=string.conversion.fix ready=1`，新转换修复已安装。 |
| 22:27:22.785 | 收到 `0x100F` 商城商品扩展数据。 |
| 22:27:23.877 | `recentIncomingPackets packet135` 收到 `SET_CASH_SHOP(0x007F)`，size=32017。 |
| 22:27:24.571 | `monthly.shop apply channel=4 token=13105 month=0`；这里 channel=4 是商品数据的现金商城类别，不是游戏频道 4。 |
| 22:27:24.589 | `showMobDamage.field dispose generation=82`，原地图释放；收到商城库存/余额及仍到达的地图对象消息。 |
| 22:27:24.590 | packet255 收到 `GIVE_BUFF(0x0020)`，size=51；同一线程进入原生增益处理，访问空 `CUserLocal`。 |
| 22:27:24.737 | `native_main_loop_escape` 首次现场 dump 写入成功。 |
| 22:27:25.885–26.065 | 未处理异常捕获和较大 mini-triage dump 完成，异常仍为 `C0000005 / 00A20586`。 |
| 22:27:29.291 | 独立退出监控确认 `process_exited exitCode=C0000005`。 |
| 22:27:37.705 | 新 PID `4576` 启动。 |

服务端在自身时间 22:27:26.450 收到此连接关闭，trigger=`transport_closed_without_recorded_server_request`。它发生于客户端首次异常之后，是退出相关连接清理，不能倒因为服务器先踢人。

## 故障指令、栈和对象

使用 main-loop dump 的原始异常上下文；最终 unhandled 报告的栈已展开到系统异常处理，不宜用它取代首次栈。

```text
CClientSocket::ProcessPacket 004965F1
  -> CWvsContext packet dispatcher 00A07A08, opcode=20
  -> native temporary-stat/buff handler 00A202BE
     00A20581 call 00538C9E
       00538C9E mov eax, [00BEBF98]  ; CUserLocal
       00538CA3 ret
     00A20586 mov esi, [eax]         ; EAX=0，读地址 0
     00A205A4 call [esi+20]          ; 本地角色虚方法，尚未执行
```

首次栈：`00A07A50 -> 00496651 -> ijl15+2715E -> ijl15+27299 -> 004965C9 -> 00496218 -> 009FEB3D`，更外层为 USER32/D3D8 消息分派和游戏主循环。出现窗口消息函数本身不说明输入法或显卡是根因；本案该消息进入的是网络包分派。

匹配 PDB 将 DLL 两层解析为 `ProcessPacketBody`（调用原生函数后的返回位置，HpMpAlert.cpp:826）和 `ProcessPacket_Hook`（:840）；没有把“栈里有 DLL”直接当作 DLL 指令崩溃。

较大 dump 验证 `*(DWORD*)00BEBF98=0`，`*(DWORD*)00BE7918=00BF1C20`（CWvsContext 仍存在）。当前阶段对象 `*(DWORD*)00BEDED4=11CC70FC`，虚表为 `00AF18A4`，与前序商城切换、地图 dispose 相符。现场 `00A20581` 起的指令字节与原 EXE 一致。

## 增益包是完整的神圣之火

首次栈保留 `CInPacket*=001AE74C`，data=`1AC86A98`、DataLen=51、Offset=46。较大 dump 保留实际包缓冲，包体按当前服务端 `PacketCreator.giveBuff` 的写入顺序反解并重新序列化，47 字节载荷（不含 4 字节传输前缀）完全一致：

| 字段 | 值 |
| --- | --- |
| opcode | `0020` / GIVE_BUFF |
| 状态掩码 | `0000600000000000`，HYPERBODYHP + HYPERBODYMP |
| 两项增益值 | 60、60 |
| 两项来源技能 | `1301007` / 神圣之火（HYPER_BODY） |
| 两项持续时间 | 300000 ms |

没有包长截断或乱码证据。触发的是有效增益包在角色对象已不存在的阶段被处理；技能 ID 只标识本次触发包，不表示“神圣之火资源损坏”或“禁用技能即可修好”。

## 修复前服务端为什么允许这个顺序

只读核对生产仓库 HEAD `688c7bd634f1b31c3dc158bf9496085614a915e5`；下列三份源码无工作区改动。EnterCashShopHandler、StatEffect 的生产 Git blob 与本地一致；Client.java 与本地 blob 不同，已读取生产版本，相关发送路径结论一致。这里核对的是生产仓库源码，不等于反编译了运行中的 JAR。

1. 当时 `EnterCashShopHandler.java` 先保存 buff/异常状态，再设置 away-from-world、取消 buff 等；`:97` 发送 `openCashShop`，接着发送商城库存等；直到 `:103–105` 才从频道和地图移除角色、设置 cashShop.open(true)。保存前没有等待并发效果完成。
2. `StatEffect.applyBuff`（本地 :1212）先从地图取候选列表，再逐个 `applyTo`。候选已经取得后，即使商城线程开始切换，调用链没有重新核对 away/cashshop 阶段。`applyBuffEffect`（:1472）通过普通 `applyto.sendPacket(buff)`（:1601）发送本地增益。
3. 生产 `Client.java:1899–1926` 使用 announcerLock 串行化单次写入；它未将商城阶段切换与后续所有地图增益一并封锁。地图过滤只在 source!=null 时执行；普通增益发送没有携带这个 source。现有地图过滤也没有“当前处于商城”的明确隔离状态。

因此修复前实现允许 `SET_CASH_SHOP -> 晚到 GIVE_BUFF` 的时序，包内确实捕获到了该顺序。提前设置 cashShop.open(true) 或在 applyTo 开头增加一次检查，单独使用都不能消除“检查后发生切换”或“候选已被另一个线程持有”的并发窗口。晚到包具体来自队伍施法、重发还是其他增益任务，仍需区分；本包不含其发送线程栈。

## 与既有修复的关系及修复边界

- [高地址字符串修复](high-address-string-conversion-fix-20261014.md) 处理转换 API 容量导致的资源名失败，本次安装成功，故障现场没有走该链。
- `CharacterStatGuard.cpp` 只保护 `STAT_CHANGED(0x1F)`，检查 context+20B8 的角色数据。商城保留角色数据，但没有地图上的 CUserLocal；它不会保护 `GIVE_BUFF(0x20)`。
- `ClientCrashFixes.cpp` 的 quest.context 只保护离开地图后的任务对话续接；服务端已有的 NPC 对话禁止进入商城也仅覆盖对话状态。它们不覆盖本次增益包。

初次分析提出以下两个修复边界，现已按配套修复记录实施：

1. 服务端将离开地图、商城进入消息及本地角色增益的发送资格纳入一致的阶段/同步控制，覆盖已经取得目标的并发施法；明确返回地图时何处恢复合法状态同步。避免仅移动几行代码而留下竞态。
2. 客户端在处理增益时尊重 CUserLocal 生命周期，对没有地图角色的分支保持安全，并核对 CWvsContext 的 buff 状态和返图重同步。不能直接吞异常、伪造角色或未经核对就丢弃全部正常增益同步。

实机验收仍需覆盖战斗中接受队友神圣之火时进商城、进入前临界到期、商城暂停计时、返回地图后的 HP/MP 上限和图标、反复进出以及断开重登。已完成服务端并发/状态恢复测试、原生故障指令与真实分派入口的隔离测试；尚未进行完整游戏渲染或原生 SecondaryStat 解码的端到端测试，不能宣称已经完成实机验收。

## 本地复核产物

诊断目录保存 `archive-manifest.json`、`text-scan.json`、`dump-frames.json`、`dump-modules.json`、`matched-symbols.json`、`buff-handler-full.txt`、`buff-packet-evidence.json`、`clock-offsets.json`、`server-run-4120.json`、`server-crash-window.json`、`production-Client.java` 和 `evidence-checks.json`。它们是只读分析输出，不是客户端交付文件。
