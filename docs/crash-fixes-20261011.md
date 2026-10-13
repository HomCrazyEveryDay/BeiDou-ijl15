# 2026-10-11 生产崩溃首批修复与本地交付

## 2026-10-14 提交范围补充

本次统一整理已交付的 [NPC 对话焦点修复](npc-dialog-focus-20261013.md)和[内存池探针启动时序修复](pool-guard-startup-crash-20261014.md)。配套 DLL 的 SHA-256 为 `48671ACDD37D4EC9E5B51F9CE3AC4E90B9FA5275E7D4F4945B4CED48E6250743`，`Release|x86` 构建及两项离线回归通过，游戏内行为仍待实测。

下述 Canvas 空源跳过绘制和 `CharacterStatGuard` 两组候选保护继续暂缓，未包含在这次提交或配套 DLL 中。保留本记录是为了说明历史构建和当前交付的区别。

## 2026-10-13 状态补充

本文保留 10 月 11 日首次组合构建的历史记录，**下文 DLL 哈希和“三项本地修复”不代表当前交付版本，也不能作为整体提交依据**。

- PCOM 内存池生命周期修复已独立提交并推送为 `7e893ca`；对应的专项说明见 [PCOM 退出修复](pcom-pool-lifetime-20261012.md)。
- Canvas 空源跳过绘制（`ResourceReadGuards.h` 及测试）仍是未提交候选，只能避免特定空源调用，可能留下局部缺图，尚未解决资源为何为空。
- `CharacterStatGuard` 及其安装入口、项目和测试文件仍是未提交候选。后续调查已发现登录阶段错误发送 `STAT_CHANGED`、角色初始化前收到属性包的证据，不能把这些样本一概解释为退出后的过期包；应先修服务端异常响应和初始化包序。
- 会话《汇总生产崩溃与解决状态》（`01a126f8-be3f-7dd1-908e-9de433e881e5`）中，用户在 10 月 12 日明确表示“我感觉这应该不是真正的崩溃点，先不做修复”，随后授权增加定位日志。因此上述两组行为保护继续暂缓，诊断日志另行交付。
- 当时交付源码为 `244e1f5`，客户端配套提交 `b194d6cb`；DLL SHA-256 为 `AD102635B1BDF901484B07D7DF27EB9A17D71A30A828639986BEC57399DF683E`，**不包含上述两组候选保护**。它包含三处指令修正和默认关闭的内存池诊断，详见 [10 月 13 日交付记录](pool-guard-handoff-20261013.md)。这里仅核对本地交付与 Git，不据此宣称生产已发布。
- 10 月 13 日未提交改动复核中，重新运行 `ResourceReadGuardsTest.ps1`、`InventoryAndFocusFixTest.ps1` 均通过。结果仍仅覆盖独立测试进程中的真实组件/分派器、参数、清理和异常传播；没有启动游戏，没有补足界面完整性或角色属性实机验收。本轮未把候选源码构建同步为交付 DLL。

本文件作为历史调查和验证记录值得保留；提交文档不等于批准安装两组候选保护。测试输出留在 workspace `_diagnostics/uncommitted-audit-20261013/`，不提交日志。

## 10 月 11 日原始实施记录

本次接续 [生产崩溃审计](../../myself_BeiDou_Server/docs/production-crash-status-20261011.md)，处理退出 `004031FE`、空画布 `Canvas+ED28` 和属性包 `00A1FBA2`。以下是本地实现、离线验证和 DLL 同步结果，**不是已经发布或生产验收通过**。服务端脚本结果见 [配套记录](../../myself_BeiDou_Server/docs/production-script-fixes-20261011.md)。

## 状态与修复边界

| 指纹 | 本次处理 | 验证范围与限制 |
| --- | --- | --- |
| `004031FE` 退出释放 | 原生匿名内存池保持到进程结束，供 PCOM 和其他 WZ DLL 的晚到释放继续使用；Windows 最终回收进程内存 | 真实 PCOM、List.wz 和原生分配器对照通过，包含“List 已清空仍崩溃”的旧补丁反例；完整游戏退出待实测 |
| `Canvas+ED28` | 在 `00424BC8` 和 `004B7DBB` 两个已观察到的 Copy 调用位置拦截空源，保留后续清理；非空源继续原始 Copy | 真实 Canvas 空源复现同一故障，新调用位置均能安全返回；这是防崩保护，不能说明资源变空的原因已解决 |
| `00A1FBA2` | 在原生 `0x1F / STAT_CHANGED` 分派调用前检查角色数据，无角色时丢弃该属性包 | 真实分派器、跳转表、调用栈和返回清理通过；非空角色继续原函数，其他异常继续传播；最初角色被清空的原因仍需结合实测连接过程判断 |

未对动作控制流、空 COM、RichEdit、其他窗口/图形异常作无证据的通用异常屏蔽。此前宠物 UOL 修复保留在本次构建中；宠物原场景实机验收仍待完成。

## PCOM：旧修复为什么不够

审计中已有成功的 `cleanup.begin entries=371 → cleanup.end entries=0 → pool.end poolFlags=1F`，之后仍出现 `004031FE`。旧实验先执行 `PcTermModule` 再清 List，遗漏了模块终止更晚的退出顺序。

本次新增对照：执行真实 `PcInitModule`、载入 371 条 List、清空 List、销毁原生池，最后执行 `PcTermModule`。即使 List 已经为空，模块路径字符串的释放仍在同一条原生指令 `004031FE` 发生访问异常。该实验验证了额外的晚到内存持有者，但不把所有生产同地址样本都认定为这个特定字符串。

`00796D51` 是 `00796D45` 注册的 EXE CRT 析构回调，发生在 `ExitProcess` 和依赖 DLL 退出清理之前。新的 `PcomExitFix.cpp` 不再在这里提前清 List、释放整池，也不设置“池已销毁”标志。池的寿命覆盖所有后续 DLL 清理；普通 Alloc/Realloc/Free 和 PCOM 原有析构均保留。进程结束后由 Windows 回收内存，不在正常游戏过程中积累新增保留对象。

代价是进程退出的最后阶段保留原有池块，不做提前整批 HeapFree。没有 `Free` 空操作、坏指针判活后跳过、SEH 吞异常或直接强退。补丁仍校验 EXE/PCOM 版本、代码字节和分配器回调；不匹配则拒绝安装。

`tests/PcomExitFixTest.ps1` 八组均符合预期：原版错误顺序、旧清表后晚终止两组精确复现 AV；修复的 guard/Windows heap 两组及晚终止两组正常；未初始化 List 不额外初始化；人为制造的清理异常继续传播。修复组最后在测试进程中手动回收池，核对所有 25/26 个 backing block 仍可正常回收。生产代码不执行这个测试回收步骤。

## Canvas：覆盖两个实际调用入口

生产首次异常中 `ESI=ECX=0`，原生 `Canvas+ED28` 读取源画布宽度时解引用空对象。已观察到普通 Copy 包装器和另一个直接 COM Copy 入口；原补丁只在 `008DA50B` 尝试重载左括号，无法保护其他空源。

新增保护保留完整 `stdcall` COM 参数和按值传递的 VARIANT。空源返回 `S_FALSE`，只省略当前不可用的绘制，继续调用者原有 VARIANT/对象清理；下次非空源立即正常绘制。非空源的 HRESULT 原样传递，原生错误处理不变。已知左括号仍优先走原有资源重载，而不是直接省略。

`resource.guard canvas missing_source` 每次运行最多记录八条，包含调用位置、目标画布、坐标和短调用栈。没有逐帧输出、无限重载或新建占位资源。若实测仍有局部 UI 缺图，应按这个位置继续检查资源取得和对象释放；“没有崩溃”不能替代画面完整性验收。

`tests/ResourceReadGuardsTest.ps1` 使用真实 Canvas DLL 和当前 IMG 复现 `ED28`，验证两个修补后的调用位置；同时覆盖原生包装器、COM 参数、错误返回、诊断限量、左括号重载引用计数，以及已有头像/物品守卫。额外反读龙神教学 `UI/tutorial.img/evan/0/0`、`8/0`，两者均为有尺寸的画布。没有启动游戏进程。

## 属性包：更正原审计的初步分类

`00A07A08` 的分派表将 opcode `0x1F` 交给 `00A1FB52`，与服务端 `STAT_CHANGED(0x1F)` 一致。`00425D0B` 从 context+`20B8` 取得角色数据；`00A1FBA2` 在未检查空值时读取角色+`35`。这不是 `OnInventoryOperation` 的空槽删除问题。

`CharacterStatGuard.cpp` 仅修补这个分派调用：角色数据存在则进入完整原生函数；不存在则记录并丢弃当前独立解包后的属性消息。不会给其他角色重放过期属性，也不会跳过同一背包包中的后续操作。`tests/InventoryAndFocusFixTest.ps1` 执行真实 0x1F 分派器，覆盖空角色、下一次有效角色更新、栈参数和异常传播；既有背包与焦点回归也通过。

## 本地交付

2026-10-11 08:14:48（北京时间）确认无 BeiDou/MapleStory 客户端进程后，已备份并替换 `BeiDou-Client/ijl15.dll`。源码基线为 DLL 仓库 `BeiDou` 分支 `ecd533a` 加本轮未提交修改；没有提交、推送或远端发布。

- 构建：`ezorsia.sln /p:Configuration=Release /p:Platform=x86`，成功；构建附带的 LauncherParentPolicyTest 通过。保留既有 Detours PDB 缺失链接警告，无编译错误。
- 新 DLL：772608 bytes，PE timestamp `6ACAD3ED`，SizeOfImage `0012F000`。
- SHA-256：`A90EE15DC563830A5E9DC10D0B8938A35CCF8DF1F13001A44ECFA034E4B98B4A`；构建输出和客户端文件一致。
- 旧 DLL SHA-256：`B5C44AC3D93AF19B95C4EDA48A62461A3F79BA50CD2021ACD904FB0D1764550B`。
- 备份、新 DLL、匹配的 PDB 和 manifest：workspace `exports/crash-fixes-20261011/delivery-20261011-081448/`；旧文件在其 `backup/ijl15.dll`。
- 构建日志：workspace `_diagnostics/crash-audit-20261011/client-fix-build.log`。PCOM 测试产物：`.tmp/pcom-exit-fix-test-a0e7c412966a4c86adeb0a83476459a1/`；属性/背包/焦点测试产物：`.tmp/inventory-focus-test-0486bd75243444f9b5b10075222031f9/`。
- `BeiDouExitMonitor.exe` 构建与客户端哈希相同，无需替换。服务端 Java/WZ 和客户端 IMG 本次未修改。

本地回退：关闭客户端后，将上述备份 DLL 复制回 `BeiDou-Client/ijl15.dll`，核对旧哈希。远端发布和回滚仍需遵守 Git 提交、推送、目标机器拉取及构建流程；本记录不授权生产写操作。

## 实机验收清单

由用户启动本地测试客户端；本次没有自动登录、操作或轮询用户测试状态。开始前核对实际加载的 DLL timestamp/hash，防止启动器更新后测到旧构建。

1. 普通退出、返回登录后退出、连接失败后退出；检查实际退出码和 `pcom.exit.fix pool.retained`，不能只看是否弹窗。
2. 登录、换线、返回角色列表、再次进入；检查角色属性、操作恢复及 `character.stat` 是否触发，确认无 `00A1FBA2`。
3. 状态栏 HP/MP/经验、数字括号、界面特效；确认画面完整，检查 `canvas missing_source` 的位置和频率，确认无 `Canvas+ED28`。
4. 保留宠物、背包刷新、人物焦点等已修复功能的场景验收；本轮专项回归不等同于完整游戏回归。

正式版本上线后，应再按新 DLL 身份和同一故障指纹观察复发；当前统计仍然属于旧发布版本。
