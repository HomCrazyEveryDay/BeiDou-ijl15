# 干草堆客户端卡顿计时

2026-09-23 14:50 本地复现中，服务端六次命中仅耗时 1.9–8.3ms。
客户端 PID 18676 的同次运行中，反复记录 E06D7363 异常，关联资源请求
`Sound/Reactor.img/1002008/0/Hit`。重复详细报告仍发生在主线程，单次约 0.4 秒。
资源关联不等于已证实缺失；旧客户端 DLL 没有工作区已有的整份异常报告去重及批量刷盘修正。
本次构建保留该修正，补充以下观测，不修改原生攻击或资源行为。

`event=reactor_timing` 复用现有发送、接收、玩家 Update 和异常观察入口，
每游戏线程最多观察 32 次反应堆攻击、输出 256 行，发包后观察 3 秒。
`BEIDOU_REACTOR_TIMING_LOG=0` 关闭；Update 观测依赖既有 EvanTimingDiagnostics 安装成功。

- `send_begin`：CD 攻击反应堆包；oid 与服务端一致，detail 是技能 ID。
- `send_return`：原生发送函数耗时。
- `receive_begin` / `receive_return`：同线程、同 oid 的 0115 受击包及其处理耗时。
- `update_gap` / `update_slow`：超过 100ms 的玩家更新间隔或原生更新调用。
- `exception_observer`：当前观测窗口内异常诊断本身耗时，detail 为异常码。
- `window_end`：观察窗口结束。

`sinceSendMs` 从最近一次受击发包观测计算，不是网络 RTT；`durationMs` 含义由 phase 决定。
只记录标量，不记录包内容或凭据；使用现有 Trace 日志和轮转。
发包前未进入观测窗口的停顿、其他线程的收包、键盘入口和具体动画资源调用未单独计时，不能从缺失记录断定不存在卡顿。
用户重启客户端后打两堆干草，再反馈复现情况；不自动启动客户端或轮询测试日志。
