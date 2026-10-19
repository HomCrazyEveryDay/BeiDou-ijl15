# 2026-10-14 本地怪物攻城闪退：脸饰画布为空

调查日期：2026-10-14（Asia/Shanghai）。来源为本地客户端 22:02:15 的日志、mini-targeted dump、匹配的客户端 EXE/DLL、只读查询的生产连接日志，以及隔离进程中的真实 PCOM 资源读取。用户确认场景为“怪物攻城，很多一起打怪”。没有启动或操作游戏，没有写入生产。

## 本轮修复与边界

已实现实际恢复逻辑：在原生 Canvas 赋值返回 `E_NOINTERFACE` 且目标为空时，按原函数保存的**同一脸饰、表情、当前动作帧**重新读取真实 `default` 画布；成功才返回 `S_OK`。失败保留原 HRESULT 和编号子画布回退；有效画布、其他错误、原 1×1/info-image 兼容继续原有行为。没有伪造透明画布，没有跳过整个人物绘制，也没有修改 IMG。

旧桥接函数在所有失败 HRESULT 上直接 `js done`，这使空输入的恢复逻辑不可达。之前的空源 Copy、混色脸 origin 和角色属性保护没有覆盖这里。本案已证实的直接失败是脸饰画布为空；首次读取为什么失败，现有 mini dump 仍不足以证明。本文的恢复验证属于**已知失败结果重放**，不是完整怪物攻城自然复现，更不代表全部闪退已根治。

## 已确认的现场

- 运行：`20261014-135612909Z-pid17984`，`clientRunId=5e1cecec4988c303fc3685a60b064807`，频道连接 `16817cdb2091487e838d01ad2f67574c-29995`。
- 本地时间 `22:02:15.047`，`_com_error / 0x80004003 (E_POINTER)` 逃出主循环；`22:02:15.351` 记录 dump 写入成功。这是客户端异常，不应仅归类为网络掉线。
- 服务端同连接在 `22:02:15.248` 记录 `transport_closed_without_recorded_server_request / channelInactive`，地图 `100000000`、频道 1、角色 ID 14；没有该连接的主动踢线记录。两端时钟未经专门校准，毫秒差不用于证明精确先后。
- 同地图附近几秒还有其他连接关闭或 reset；没有它们的对应 dump，不能认定同一根因或声称整图玩家均因本案崩溃。
- dump 加载表未发现完整 basename 为 `Write.dll` 的模块。系统 `DWrite.dll` 不是该辅助。本现场不能归入已识别的 ExUI/RichEdit 退出崩溃。
- 加载的 ijl15 PE 时间标记 `6ACE6252`、镜像大小 `179000`，与当时本地 DLL 一致；本地文件 SHA-256 为 `2CDEE582176B2278FA765AEBBDE47CF86BC9B58C0CFE13CF074E9D91011A765F`，对应已交付的 `e838ec7`。这是已包含资源字符串内存修复、空源 Copy 和角色属性保护的版本，不能解释为只用了先前未修复的 DLL。mini dump 不包含整个 DLL，文件哈希来自磁盘，不能冒充整份加载内存的哈希。

## 直接失败位置

经 EXE 指令与异常栈交叉核对，角色外观刷新进入 `004515BD → 00451CF6 → 00453696 → LoadFaceAction(00407A36)`。后者向 `0045370F` 返回的帧为 `0019E6E8`。

| 栈槽 | 捕获值 | 含义 |
| --- | --- | --- |
| `EBP-138h` | `54050` | 原始脸型 ID |
| `EBP-134h` | `0` | 原始表情索引：blink（眨眼） |
| `EBP-130h` | `1012057` | 脸饰 ID |
| `EBP-20h` | `389EE320` | 本次脸饰动作帧属性对象，堆内容未捕获 |
| `EBP-1Ch` | `7B52113C` | 脸部画布指针，堆内容未捕获 |
| `EBP-14h` | `0` | 脸饰画布为空 |

`004090FC` 查询脸饰当前动作帧属性下的 `default`，通过 `00409119` 的 Canvas 赋值；若为空，则在 `0040918E` 查询当前帧（现场为 0），通过 `004091AE` 赋值。随后 `004091FC` 调用 `00414576`，后者检测 Canvas 智能指针为空，在 `00414583` 抛 `E_POINTER`。异常栈保留了返回位置 `00414588`、`00409201`，与空的 `EBP-14h` 一致。

这是直接失败链；原始查询 VARIANT 已在原函数中清理，不能从零值反推原查询的 HRESULT、键名或对象类型。现有堆内存不足以判断为什么首次 default 查询没有得到画布。没有证据把最近的伤害数字、宠物渲染日志或栈上的 `DeathDialogDiagnostics::UpdateHook` 指定为根因。

## 文件与真实组件核查（包含修正后的完整路径）

`0045E8C0` 初始化的 `00BEC284` 表明确索引 0 为 `blink`，不是顶层 `default`。原来只检查 `default/default` 不足以验证现场。按真实路径补查：

- `01012057.img/blink/0..2/default` 的直接属性返回均是 UOL，值 `../../default/default`。
- UOL 的 `+2Ch` 方法给出绝对目标 `Character/Accessory/01012057.img/default/default`；ResMan 可以正常解析为同一 4×4 Canvas。
- **原生 `00414ADA` 本来就有 UOL 解析逻辑**：通过 `00416838 → 0041D01D` 查询 UOL IID，调用 `00414C70` 取绝对目标，再调用 ResMan。不能把绕过这个转换、直接对原始 UOL 做 Canvas QI 的失败，当成整条原生链必然崩溃的证据。
- 因此，早期关于“原生不解析链接，因此已证明根因”的判断已撤回；健康读取路径可成功。本轮针对转换后空画布的真实失败形态补齐恢复。

使用独立 x86 探针载入正式 PCOM/ResMan/Canvas，挂载本地 Data；不执行 BeiDou.exe 入口、不加载 ijl15 或 Write、不连接服务器、不修改资源。

| 查询 | 结果 |
| --- | --- |
| `Character/Accessory/01012057.img/default/default` | `S_OK`，Canvas QI 成功，4×4 |
| 同目录 `default/0` | `S_FALSE / VT_EMPTY` |
| 同目录 `info/image` | `S_FALSE / VT_EMPTY` |
| 对脸饰顶层 `default` 属性直接 `get_item("default")` | `S_OK`，原始返回对象即可 QI 为 Canvas，并非未解析的 UOL |
| `Character/Face/00054050.img/default/face` | `S_OK`，Canvas QI 成功，29×15 |

因此没有证明磁盘资源缺失或 UOL 本身不兼容。`default/0` 缺失对这个静态脸饰本身不是损坏证据：正常路径应已取得 `default/default`。也没有依据修改 IMG、伪造画布或简单跳过角色绘制。运行时查询、缓存或对象生命周期失效仍须继续取证。

## 恢复实现细节

- 仅覆盖已校验签名的 `004081B2`、`00409119` 两个 default Canvas 赋值点。`E_NOINTERFACE` 允许进入恢复，其余失败仍直接返回。
- 原生表情顺序由 EXE 字符串池和初始化指令核对，越界表情按原函数归一为 blink。两条遍历分别使用 `EBP-40h` / `EBP-30h` 中的原始脸饰 `_bstr_t` 帧键，不能拿脸部的取模帧键代替。
- 恢复路径为 `Character/Accessory/%08d.img/<expression>/<frame>/default`。它保留原图、origin、brow、z、原生引用所有权及后续合成流程，不缓存裸 COM 指针。
- 恢复实际成功才改 HRESULT 为 `S_OK`；缺少资源/ResMan 时维持原失败和原回退。保护寄存器、栈、x87/SSE 和 LastError，事件沿用最多 24 条限额。

## 配套定点诊断

1. 在两条已核实的“默认脸饰画布不可用后查询当前帧”返回点 `0040822F`、`00409193` 观察失败 HRESULT、空值和空对象，记录真实查询键、父节点字典、脸型/表情/脸饰。允许缺少 default 的正常动画资源不单独消耗诊断额度；有效帧不扫描、不写日志。
2. 对已确认 `_com_error / E_POINTER` 且原始帧链包含 `0045370F` 的首次异常，新增 `face_resource_first_throw`，在展开前保留原始 ID、参数、COM 指针槽和有界对象头。只读取内存，不调用 COM、不重试、不处理原异常。
3. 继续沿用 `CrashDump` 开关。资源查询最多 8 组，本案每组多一条 `resource_face_fallback` 身份行；总上限相应最多 216 条查询相关诊断。新增异常家族每进程最多一份，原家族编号不变，新文件标签 `_targeted-10`，配置标记为 `maxFamiliesPerSession=9`。BDS1 版本/布局、最多 8 块及每块 256 字节不变。
4. 与其他故障观察一样，去重、并发跳过、不可读内存及 dump 写失败会导致缺失；没有输出不能证明没有故障。首次采样存在同步写盘开销。

## 验证与未完成项

- `ModernEquipmentCompatibilityTest.ps1`：隔离映射正式 EXE，运行真实 Canvas QI/Release 和 `00414576` 空指针检查；仅把隔离进程里的抛异常出口换成可捕获的测试出口，核对传入 HRESULT 为 `E_POINTER`。正式 PCOM/ResMan/IMG 提供真实图像。
- 先验证原生 UOL 绝对目标的健康读取成功，再分别注入“转换后为空”和“尚未转换的 UOL”。旧 HEAD 桥接在空结果测试失败；修复后 3 帧 × 2 调用点 × 2 输入 × 200 轮，共 **2,400 次恢复通过**。输出与原始 Canvas 同一对象，像素可解码，引用计数无泄漏；缺失资源/ResMan 和无关 `E_FAIL` 不伪造成功。
- 既有真实脸饰、1×1/info-image、武器图标、宠物配装/UOL、名牌回归通过。旧脸饰 2,000 次加载/释放的 private bytes 未增长。

- `ResourceLookupDiagnosticsTest.ps1`：两个调用点的原版指令关系、正常路径无诊断读取、空值原样返回、ID/键名保留、寄存器/栈/LastError/x87/SSE、日志异常隔离、限量通过。
- `ResourceExceptionSnapshotTest.ps1`：原始上下文分类、ID/对象捕获、实际空槽形态、不可读内存、写失败及去重通过。脸饰部分使用人工上下文，不冒充怪物攻城重放；既有真实 PCOM 首次异常测试通过。
- `TargetedCrashSnapshotTest.ps1`、`NativeExitDiagnosticsTest.ps1` 回归通过。
- 构建和本地交付结果见下方补记；实机怪物攻城场景尚未复现验证，尚未确定 default 查询为空的上游原因，也未证明根治。

本地证据与探针输出保存在 workspace `_diagnostics/local-crash-20261014-220215/`，探针源码为 `.tmp/local-face-20261014.cpp`；日志、dump、产物及本地路径记录不作为源码提交。

## 构建交付状态

后续交付补记：23:43 已在游戏退出后同步包含本脸饰恢复和[高地址转换修复](high-address-string-conversion-fix-20261014.md)的合并 DLL（`5ADF7073…`）；下面的 `81D492EC…` 是 22:38 首次交付记录，其“未提交、未推送”为当时状态。

实际恢复版 `Release|x86` 构建成功（沿用已有 Detours PDB 警告）。DLL 818,688 字节，SHA-256 `81D492ECE7327CE991A4DEB4FFFC6E6B12C5714DA84F5164A353E579F9660A71`。独立留存于证据目录 `ijl15-face-recovery.dll` / `.pdb`。

最初因 `BeiDou.exe` PID 17984 仍存在而停止替换。用户确认关闭后，于 2026-10-14 22:38（Asia/Shanghai）再次核对进程已退出，备份原 DLL，并将上面的实际恢复版同步到 `BeiDou-Client/ijl15.dll`；安装文件与已验证产物 SHA-256 完全一致。旧 DLL（`2CDEE582…`）备份在本地证据目录 `ijl15-before-face-recovery-2CDEE582176B.dll`，安装记录为 `face-recovery-delivery.json`。先前只含诊断的 `C8A64002…` 产物未交付。没有自动启动或操作客户端。未提交、未推送、未发布；怪物攻城实机验证交由用户执行，尚未完成。
