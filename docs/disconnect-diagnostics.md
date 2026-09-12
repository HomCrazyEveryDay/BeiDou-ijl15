# 返回登录页诊断

本功能用于追查进程仍在运行、游戏连接却关闭的问题，不代表已经修复掉线。

本轮排查期间对使用新版 DLL 的所有客户端直接启用，不读取 `config.ini` 的 `enableDisconnectDiagnostics` 配置；即使配置为 false 也会启用。替换 DLL 并重启客户端后生效。无需打开全量封包内容日志。启动时检查 lifecycle 日志中 `disconnect_diagnostics` 的三个安装结果均为 1。排查结束后需通过后续 DLL 更新恢复可配置开关。

## 客户端证据

- `disconnect_event` 记录原生 EXE 调用 recv 时的 EOF/错误，以及调用 closesocket 时的调用栈（模块名称、加载地址、偏移）。`local_socket_close` 表示本地执行关闭，不自动等于玩家主动退出。
- `disconnect_packet` 是内存中最近 32 条收发封包的方向、操作码、长度、线程和单调时钟，不记录载荷。只在诊断事件时写出。
- `first_chance_exception_not_necessarily_fatal` 观察安装线程上的指定异常，包括 C++ 异常；异常仍交给原处理器，不能据此认定崩溃。`packet_processing_exception` 在封包处理栈展开之前记录原始异常地址和寄存器。
- 每次符合观察条件的事件和异常都记录，不设每连接次数上限。新连接开始时清空封包历史，不再额外输出 connection_begin 详细事件。仅防止日志代码自身递归触发记录。
- `diagnostic_previous_connection` 在新连接创建前输出旧 connectionId，配合现有新连接记录追踪返回登录流程。
- 崩溃文本使用单独保存的原始异常上下文，与 DbgHelp 写 dump 使用的副本隔离。

数据进入现有 `logs/ijl15-lifecycle-*.log` 和崩溃文本上传链路，无需新增启动器版本。dump 上传策略没有改变。断线诊断使用的 lifecycle 追加日志不再受 8 MiB 停写上限约束（文件头 maxBytes=-1）；其他类别日志维持原规则。文件持续追加，磁盘空间不足或写入失败仍可能造成缺失。

## 服务端配合

配套 Java 改动写出 `[DisconnectCause]`，同一会话只保留第一个触发点：网络异常、无效包头、心跳超时、强制断开、业务断开或 closeSession/disconnectSession 调用位置；同时关联 clientRunId、connectionId、sessionId 和角色。

`transport_closed_without_recorded_server_request` 明确表示关闭前没有记录到服务端请求，原因仍未知；`transition_transport_closed` 表示迁移期间关闭。必须与已有 `[Disconnect]`、客户端事件按 connectionId 对齐。重新连接仅证明进程继续运行，不证明此前掉线正常。

## 验证与边界

自动测试覆盖真实 Winsock hook、错误码不变、正常收包、对端 EOF、异常继续传播、超过原 16/8 次额度及 8 MiB 后仍继续记录，以及 32 条封包历史窗口；服务端测试覆盖连接重置后保留首次原因。客户端实机验证由用户进行：正常返回登录、切频道、复现异常退登录分别保留时间、角色及上传日志。

这些观察点不能覆盖所有 Windows 异步网络通知，也不能仅凭 EOF 判定远端服务、代理或网络中的哪一层先关闭。调用栈仍需按对应版本 EXE/DLL 分析。生产 Java 更新和用户客户端更新 DLL 之前，历史日志不会补出新证据。

## 正常换线过滤

收到完整的 CHANGE_CHANNEL 成功响应（0x10、成功标志 1、IPv4 和端口）后，同线程 5 秒内的下一次本地 closesocket 不输出详细事件，随后立即消耗标记。新连接、网络错误或被记录的异常会清除标记；换线出错仍记录。仅凭请求换线、EOF、重连或返回登录页不会过滤。菜单正常退出按下述已验证调用链过滤；未识别的调用链继续保留。该过滤根据协议时序识别预期关闭，实机仍需验证；不保证证明整个换线最终成功。

## 正常退出分类（2026-09-12）

先校验当前 EXE 的 PE timestamp=4B7C15C9、SizeOfImage=A94000 和五处 call 指令目标。只有全部匹配才启用退出过滤，启动记录 verifiedExitSites=1；其他版本保留详细日志。

正常返回登录必须同时包含确认返回调用点 A068BB（确认对话框结果为 6 后执行）、登录重建 A2467F，以及快捷键 A07529 或菜单 8D42B0 之一。单独出现 UI 函数、登录重建或确认函数不足以过滤。出现 FD_CLOSE 路径则优先保留。

程序清理要求同时包含 9F5219 和主循环清理点 9F1CF1；它表示 process_cleanup，不宣称证明是玩家点击关闭，程序异常后的清理也可能经过此路径。正常关闭成功时不输出详细断线日志，关闭失败仍记录。分类在原始 closesocket 完成后进行，不在 shutdown/closesocket 之间做日志或栈分析。

异常永不因上述分类吞掉；日志增加 phase=active/user_logout/process_cleanup。新登录或频道身份发送时恢复 active。返回登录过程中的 C++ 异常和退出阶段 C0000005 仍保留，但不应统计为游戏中掉线。后台未接收客户端主动退出标记，仍可能显示 transport_closed_without_recorded_server_request，需结合 DLL phase 判断。

为避免 FPO 导致回溯漏帧，异常另记录原始 ESP 起 64 个栈槽中的可执行映像地址候选（exception_stack_candidate，notUnwound=1），不输出其他原始内存。这些候选不能当作已还原的调用栈。

自动验证：两种已知 UI 样本、缺失关键帧、FD_CLOSE 反例；真实读取地址 4 后 EIP 和直接调用返回地址被记录；真实异常生成的文本及 dump 保留故障 EIP、ESP、EBP 和访问地址。分类仍需用户实机复测。此次未修复历史 004181D8 或退出图形异常的根因。
## 重复异常开销优化（2026-09-13）

每次异常仍记录，不设次数上限。连续异常的调用栈、代码/地址、访问类型/目标、阶段、事件种类及封包历史版本全部一致时，用单行 exception_repeat 保留时间、线程、event、detailRef、阶段和本次寄存器。detailRef 指向同一文件同一线程的完整 disconnect_event event 编号。其他诊断事件或上述条件变化会重新输出详情；锁不可用或回溯失败时不复用。

每线程仅缓存最近一份详情，不使用不断增长的堆缓存。重复行复用先前调用栈和封包历史，原始栈候选仅在完整详情时扫描，不代表每次候选完全相同。网络错误和连接关闭继续完整记录。

阶段在每次异常发生时按已验证调用链判断，不再等待 closesocket。本次历史样本中，6 条原标为 active 的清理异常可据此识别为 process_cleanup；不会因为被处理的异常永久改变阶段。

测试中 100 次真实读地址 4 异常全部保留，日志 45,218 字节；中途新封包会刷新详情。错误码和异常传播测试通过。实际游戏开销及分类仍需用户复测；图形访问异常根因尚未修复。
后续图形异常根因修复与实机验证见 [flash-renderer-fix.md](flash-renderer-fix.md)。上文“尚未修复”为当时诊断阶段记录，当前已修复空动画渲染与重复释放两条路径；历史 004181D8 字符串崩溃仍未证明解决。
