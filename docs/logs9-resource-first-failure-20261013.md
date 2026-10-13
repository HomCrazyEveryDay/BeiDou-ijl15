# 2026-10-13 logs(9)：继续排查非 RichEdit 的首次崩溃

调查与本地交付时点：2026-10-13 10:24 +08:00。来源为用户提供的 `logs(9).zip`、正式 release 34 对应 EXE/PCOM/资源，以及工作区已有调查证据。本文接续 workspace `_diagnostics/disconnect-20261012-logs9/二轮深查-外部组件与资源链.md`。用户要求继续调查不能归因于外挂的闪退。该轮修改诊断源码并构建、同步本地 DLL；当时未启动游戏、提交、推送或部署生产。以下结论和验收状态保留该时点，不覆盖后续独立修复记录。

## 当前结论与边界

**仍有三条首次故障未完成根因闭环。** PID 1676（暴击 Canvas）、12560（Property 工厂）、9592（龙王血条）的故障 dump 模块表均未列出 Write.dll。不能把它们归为已确认的 Write/ExUI/RichEdit 退出问题，也不能从模块表缺失反推“进程从未执行任何外部代码”。[外挂相关识别及归因边界](third-party-write-richedit-crash-20261013.md) 另见专文。

| 首次故障 | 已有证据 | 还缺什么 |
| --- | --- | --- |
| 暴击特效 | `00438225` 向 Canvas Copy 传入空源对象；期望读取 `Effect/BasicEff.img/NoCri1/effect`，正式匹配资源离线可读 | 原查询返回 S_FALSE/空值，还是非 Canvas 对象导致 QI 失败；真实键名及父对象状态 |
| Property 工厂 | `800401F8 / PCOM+52CF/+4E67`；静态调用期望类名 Property | 运行时类名、规范化模块键、内置/LoadLibrary 分支、自身模块句柄 |
| 龙王血条 | 三次均恢复到 `8810018 / color=3 / bg=5`，正数 HP 且未超过最大 HP；错误在原生血条函数内 | 第一个空对象/失败资源，以及原生函数内真正抛错的位置 |

已做过的 8,008 次资源读取是隔离进程的串行热缓存验证，不是战斗重放。此次没有重复它来冒充新的根因证据。[另一批射手村事故记录](henesys-boss-disconnect-investigation-20261013.md)还包含基础 Vector2D/Property 工厂失败及原本应为 BA 的字体样式非法分支。这些记录支持继续检查字符串传递、缓存和对象生命周期，但不能证明两批样本同一个上游原因。

本轮核对了 StringPool 汉化、ResMan 初始化、角色动作加载及内存管理相关源码，没有找到直接把本案 Property、effect、BA 替换成错误值的确定性路径；该结果不等于排除 hook 间接破坏。也没有证据将发生于游戏运行中的工厂异常解释为“旧退出池修复失效”。不为这些未经证实的猜想修改业务行为。

## 新定位：为何旧样本停在最终报错

原有 `TargetedCrashSnapshot` 只分类五类访问违例；`800401F8` 和 `80004003` 是 `_com_error`，不会触发这些分类。`NativeExitDiagnostics` 保存最近异常的类型、错误码和回溯，但完整 dump 通常到主循环再次抛错时才生成。先前窗口过程捕获/转译已经清掉原始局部变量和字符串。

已有 `ResourceLookupDiagnostics` 观察状态栏、数字助手和商城，未选中 `0043819D` 暴击查询返回点及 `005336CA..00534A1E` 血条内部资源查询。仅增加最终空指针保护不能恢复这些证据。

原版 PCOM 的反汇编还确认了两个容易误读的局部槽：

- `PcCreateObject+4D19` 已将原始入参槽清零；实际类名副本仍在该帧 `EBP-14` 的 `_bstr_t`。不能看到入参槽为零就认定调用者传了空类名。
- 工厂 `+50B2` 将参数槽改为规范化后的 `_bstr_t` holder；在 `+52CF` 抛错时应按这个布局读取，不能再按函数刚进入时的双层引用解读。
- 工厂帧 `EBP-10` 是分支标记：`1` 为执行过 `LoadLibraryA`，`0` 为内置 `PROPERTY/LIST/UOL` 使用自身模块。该值与模块句柄、真实键名一起才具有判别力。

隔离测试另发现，`/O2` 的 VEH 中 `CaptureStackBackTrace` 可以只返回处理器自身一帧。本轮分类同时从**原始异常 CONTEXT 的 EBP 链**读取有界的 24 帧，避免只依赖观察函数自身的回溯。

## 已实现的定点取证

沿用 `CrashDump` / `enableCrashDump` 开关，无需打开逐帧或逐包日志。

1. 暴击查询：在原始 Property 返回后、VARIANT 清理前，记录 `0043819D` 的实际键名、HRESULT、类型、父对象及有界子节点摘要；`004381C4` 的 QI 异常按已验证局部布局记录源对象。
2. Boss 查询：纳入原生血条函数内的 ResMan 返回和 Canvas QI。不同 QI 调用点局部布局不一致，因此不猜测其 VARIANT/源对象；对应路径从前序资源查询记录取得。
3. PCOM 首次异常：仅 RTTI 确认 `_com_error`、值为 `800401F8`、正式 PCOM 身份匹配且帧链同时有 `+52CF/+4E67` 时采样。在异常展开前保留原始与规范化类名、工厂局部帧、分支标记和 `PCOM+1CA24` 自身句柄。
4. Boss 首次异常：仅 `_com_error / 80004003` 且帧链包含已核实的 `EXE+133315` 时采样。保存首次上下文、调用帧、该调用附近 512 字节栈、五个参数和 root/ResMan/graphics 全局指针。

只观察，不重试查询、不伪造资源、不吞原生异常、不改变 COM 引用计数或跳过绘制。原有日志最多 8 组的限制不变；新增 PCOM、Boss 两类每进程各最多一份定点样本，五类 AV 合计变为七类。主循环原有上限独立保留。新增家族追加枚举值，BDS1 的版本、布局、每块 256 字节/最多 8 块不变。文本只有故障标记等诊断数据，原始内存和类名保留在本地 dump 的 BDS1 中。

正常资源查询不调用采样函数、不读对象、不写盘。首次异常时仍会同步生成有界日志/dump，存在暂停开销。去重、并发跳过、提前退出和不可读内存可能造成未捕获；不能把没有诊断输出解释为没有故障。写 dump 失败继续传播原异常。

## 验证

- `ResourceLookupDiagnosticsTest.ps1 -ClientExe ../BeiDou-Client/BeiDou.exe`：原版指令签名、暴击/Boss 新范围及边界、原返回分支、寄存器/栈/LastError/x87/SSE、VARIANT 所有权、日志失败隔离和总量上限通过。1000 万次正常查询无诊断内存读取和日志；本机微基准约增加 2.14 ns/次，不能换算为实际 FPS 承诺。
- `ResourceExceptionSnapshotTest.ps1`：**真实加载正式 PCOM**，请求不存在的类，保留原始字符串 `MissingResourceEvidenceProbe#A`、规范化模块键 `MISSINGRESOURCEEVIDENCEPROBE`、有效自身句柄及分支标记 1，原函数仍返回 `800401F8`。只在探针内临时将自身句柄清零，再创建 Property，成功保存 `Property/PROPERTY`、零句柄和标记 0，并原样返回错误。没有加载 Write.dll 或 ijl15.dll，没有运行游戏入口。
- 同一测试的 Boss 部分使用人工提供的上下文/参数，验证范围、不可读内存、参数保留、一次上限和写入器抛异常；**尚未真实运行 Boss 绘制**。
- `TargetedCrashSnapshotTest.ps1`：原五类采样、禁用、去重、并发、写失败、本地 BDS1 和原始异常上下文回归通过。
- `NativeExitDiagnosticsTest.ps1`：真实 C++/SEH 传播、原异常解码、主循环边界、正常退出及写失败回归通过。
- `Release|x86` 构建成功，现存 EvanKillingWing C4733 及 Detours 缺 PDB 的 LNK4099 警告仍在；没有新增编译错误。

## 本地交付与下一份样本判读

构建以 DLL `BeiDou` 基线 `a63b9c0be2d38e5b26a0c533c466d5727c4fef8c` 为底，加入已有资源观察器和本轮新增取证。工作区原有 CharacterStatGuard、CopyAvailableCanvas 等未提交改动未混入该产物。源码清单、构建日志、替换前备份和交付 JSON 在 workspace `_diagnostics/disconnect-20261012-logs9/first-failure-build/`。

已确认游戏关闭，2026-10-13 10:24 +08:00 同步至本地 `BeiDou-Client/ijl15.dll` 并核对构建与客户端 SHA-256 一致：

```text
B794AB1ADBDF9E1798B15FADC26CDE86C69D552A5D5EDD42DBD1C6E552435DD6
```

这是诊断交付，**尚未证明玩家故障根治**。仅更新本地客户端，未发布给昨晚玩家。实机由用户执行；出现问题后需提供包含 `.dmp` 与 emergency 文本的完整日志包，仅截图错误码无法补全本次缺失的数据。交付后不自动启动、登录或轮询游戏。

维护者检查：

1. 核实加载 DLL 与交付版本一致；启动标记为 `maxFamiliesPerSession=7`、`resource_lookup_config enabled=1 installed=1`。
2. 查找 `targetedFamily=pcom_factory_first_throw` 或 `boss_resource_first_throw`，文件 tag 分别为 `_targeted-7`、`_targeted-8`；枚举中原 `MainLoopEscape` 占一个保留位置，所以编号不连续于“五类 AV”。它们是 first chance，不保证最终致命，必须再关联同 run 的主循环/退出记录。
3. 读取 BDS1 时按 `copied` 判定实际保留的字节，不把读取失败当零值；沿原始帧识别 PCOM `+4E67`，工厂 `EBP-10` 区分分支，`EBP+8` 是规范化 holder，上一帧 `EBP-14` 是原始类名副本。字符串本身由单独 Memory 块保存，最多 128 个 UTF-16 单元，可能截断。
4. 类名已错时优先沿 StringPool/BSTR 生命周期查；原始 Property 正确、分支 0 且句柄为零时查内存状态破坏；分支 1 且合法模块名时仍需进一步追实际 DLL 路径/即时 LoadLibrary 错误。本轮没有把异常发生后的残留 LastError 冒充 LoadLibrary 的即时错误。
5. 对暴击和 Boss，按 `resource_lookup` 的 caller、实际键名、HRESULT、VARIANT 和 QI 状态区分未命中、错误对象类型及后续绘制/对象生命期失败；不要仅按空对象就修改 IMG。

未完成：玩家使用本诊断构建后的首次现场捕获、三条故障的具体上游写入/失效来源、根因修复与玩家回归验收。

## 提交前整合验证（2026-10-13 11:04 +08:00）

后续独立调查已提交 `86af1f9`，修复资源字符串分配器错配，证据见[资源字符串内存修复](resource-string-allocator-fix-20261013.md)。本轮诊断在该提交之上整合，从仅含待提交内容的独立源码目录重新构建 `Release|x86`，保留该修复；没有使用 10:24 的旧产物覆盖它，也没有混入工作区其他未提交的防崩改动。

重新运行 `ResourceLookupDiagnosticsTest.ps1` 与 `ResourceExceptionSnapshotTest.ps1` 均通过。构建目录的 383 个跟踪文件与暂存内容逐一核对一致（文本仅忽略换行格式）；后续仅补充本文交付记录。原有编译警告仍在，客户端实机验证尚未进行，以上三条玩家故障仍不能宣称根治。

确认游戏关闭后同步至 `BeiDou-Client/ijl15.dll`，构建与客户端 SHA-256 均为 `CDB6E6D754577D9A4350AACD424D3418F7DC3A46AEEB334DDAD5F3340FD2180B`，大小 794,624 字节。该版本接替上文历史诊断产物；构建、验证、替换前备份和交付记录保存在 workspace `.tmp/logs9-diagnostic-commit-20261013/`。本次 Git 提交与推送不等于客户端线上发布。
