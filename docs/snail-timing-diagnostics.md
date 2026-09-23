# 蜗牛壳本机卡顿诊断

覆盖技能 1000、10001000、20001000、20011000。不修改伤害、动作或资源。

2026-09-22 本机 PID 2744 的空放日志已捕获：20011000 的 magic_return/send_begin 耗时接近 0ms，随后 update_gap 为 484～703ms。以 18:05:54 为例，first-chance 报告从 54.756 持续到 55.209，同一主线程逐行 FlushFileBuffers，下一次更新为 55.227；重复 fingerprint 已跳过 dump，但此前的参数和 provenance 报告仍重复刷盘。hitAfter 路径只是关联线索，不足以证明资源错误。

修正：整个 first-chance 详细报告在任何写盘前按异常码和栈去重，每进程最多 16 种；首次报告使用线程独立的 EmergencyBatch，共用文件句柄，结束时统一刷盘。重复报告不再写盘，异常继续原样传播。原有真实崩溃、退出和 handled dump 路径保留，conditional dump 仍最多 4 种。首次生成 dump 仍可能产生一次短暂停顿；实机改善需要用户复测，不能以构建通过代替。

`SnailTimingDiagnostics.h` 复用已有发包和 CUserLocal Update hook。若经过魔法施法入口，同时记录 magic_entry/magic_return；其他攻击路径从 send_observed 开始，不能由没有 magic_entry 推断没有施法。send_begin 的 detail 为攻击 opcode；update_gap 是两次本地玩家更新入口之间的墙钟间隔，update_slow 是原生 update 调用耗时，send_slow 是原生发包调用耗时。超过 100ms 才记录慢调用；这些数据不能单独指出具体资源或线程等待根因。

每个游戏线程最多 32 次施法、256 条事件，施法后观察 3 秒，不逐帧写盘。使用现有 Trace 日志轮转。BEIDOU_SNAIL_TIMING_LOG=0 关闭此诊断；帧间隔及魔法入口依赖原有 EvanTimingDiagnostics hooks 启用（BEIDOU_EVAN_TIMING_LOG 不得设为 0）。记录无账号凭据或完整封包。

用户重新启动本地客户端后，蜗牛壳施放 3～5 次，每次间隔至少 4 秒，记录角色、技能等级和卡顿时间。只在用户反馈后读取日志，不自动启动或操作客户端，不轮询复测结果。Release|x86 构建通过仅说明日志实现可编译，捕获效果仍待实测。
