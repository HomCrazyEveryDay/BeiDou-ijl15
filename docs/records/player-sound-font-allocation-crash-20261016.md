# 2026-10-16：logs (2)(1).zip 战斗中音效与画布资源分配失败

## 结论与边界

最新一晚（10 月 15 日）的两例异常退出，直接故障分别为音效堆分配失败后的空指针写入，以及画布位图创建失败后抛出的 E_OUTOFMEMORY。两例均不是 logs(15) 中的商城 CUserLocal 空指针路径。

**直接故障机制已确认；资源分配为什么失败，尚未闭环。** 不能把“分配失败”直接解释为物理内存不足，也不能在没有现场计数的情况下认定某个缓存泄漏、进程地址空间耗尽、GDI 配额耗尽或系统提交额度耗尽。最新三份 dump 均为零字节，失败原因本身为内存不足；文本中的寄存器、帧链和异常类型仍可用于分析。

用户确认当时双开，其他硬件及分页文件设置不详。包内旧 dump 的 SystemMemoryInfo 显示可用物理总量约 31.84 GiB，符合 32 GB 机器；旧现场仍有约 20–21 GiB 可用物理内存。因此没有依据将本次问题归咎于电脑配置差。

本地证据目录：workspace `_diagnostics/player-logs2-1-20261016/`。日志、dump、临时探针不纳入版本管理。

## 样本与覆盖

- 原文件：`logs (2)(1).zip`，23,854,980 字节。
- SHA-256：`8019e59209a76b962563b5ef85f647469c3d218e2fa376011166e71f91639115`。
- ZIP 1,183 条目，其中 3 个目录、1,180 个文件：567 log、534 json、69 txt、10 dmp。解压前检查绝对路径、盘符、上级路径及展开总量。
- 全部 1,170 个文本文件已完整解码建清单，无替代字符；新一晚的所有 exit/emergency 日志、两例报告、对应 trace 与 lifecycle 已专项检查。历史文本跨 10 月 8 日至 15 日，不把报告或 dump 文件数量当作闪退次数。
- 7 份非空 dump 属于此前运行，读取了异常、帧链、模块与内存统计。最新 3 份 dump 没有内容，未拿历史 dump 冒充昨晚现场。
- 服务端通过 log-query 增量 refresh 后按两个 run 查询，server pendingFiles=0、两次查询均 nextOffset=null。未使用未完成扫描的远端 client 索引；本次客户端证据来自附件。

## 第一例：冰星，音效缓冲区分配失败

玩家电脑时间（UTC+8）：2026-10-15 22:29:06.751。

- PID 12840，线程 23088，run `0a8cf2e31bd290de73633be84f2c125f`，连接尾号 34803。
- 服务端同连接标识角色冰星，charId 1078、accId 626、频道 1、地图 100000000，cashshop=false。服务端记录为 Connection reset 后清理连接，不是商城切换。
- 异常 `C0000005`，指令 `SOUND_DX8.DLL+1A33`，写地址 0。
- 寄存器 EAX=0，EBX=0x37B04，ESI=FFFFFFFF。
- 退出监控 22:29:11.204 记录进程退出码 C0000005。

匹配正式音效 DLL 的 timestamp `4B7C1425`、ImageSize `24000`，静态指令如下：

```text
51801A20 lea  eax,[ebx+4]        ; 0x37B08 = 228104 bytes，约 222.76 KiB
51801A23 push eax               ; HeapAlloc 的字节数
51801A24 push 0                 ; HeapAlloc 的 flags
51801A26 call [5181F994]        ; GetProcessHeap，无参数
51801A2C push eax               ; heap handle
51801A2D call [5181F98C]        ; HeapAlloc
51801A33 mov  [eax],ebx         ; 返回 NULL 后仍直接写入
```

不是声卡驱动调用处，也没有证据指向音频文件损坏。当前可确认的是客户端原生音效分配器没有处理 HeapAlloc 返回 NULL。不能仅在这个底层函数返回 NULL：其调用者 `+5B6A` 随后也会 `add eax,4`、写入数组头；修复必须在资源申请/播放的完整调用路径处理失败，不能只移动崩溃位置。

帧链：`SOUND+1A33 <- +5B6A <- +AC45 <- +BB8E <- +7A35 <- EXE+3F749 <- +3FD6F <- +58A214 <- +26C0D8 <- +2793A9 <- +13155E <- +96664 <- ijl15+2715E/+27299`。续查确认实际栈经过 MOVE_MONSTER(0xEF) 的怪物技能音效分支，见下文；不再仅依赖“最后一条包名”。文本没有保存具体怪物 ID、技能动作序号或完整音效路径。

## 第二例：侠气，头顶聊天文字的字形位图创建失败后退出

玩家电脑时间（UTC+8）：2026-10-15 22:33:38.722。

- PID 23068，线程 21452，run `1f0fe13f6dd09163257ae128c2665f95`，连接尾号 34761。
- 服务端同连接角色侠气，charId 879、accId 629、频道 1、地图 100000000，cashshop=false；服务端亦记录 Connection reset。
- 主循环逃逸异常 `E06D7363`，经过已校验 RTTI 解码的类型为 `_com_error`，HRESULT=`8007000E`（E_OUTOFMEMORY）。`KERNELBASE` 是抛异常位置，不是据此判定 Windows 本身故障。
- 同线程约 32 ms 前的记录：`Canvas+4AD2 <- +4D1A <- +53D9 <- +3330 <- EXE+8ED94`，同一错误随后沿 EXE COM 包装器传播。
- EXE `9F60B0..9F6108` 读取应用对象 `+38` 的待处理错误、清零后调用 `_com_error` 抛出函数。因此仅看最终帧链会丢掉最初的 Canvas 来源；滚动异常记录保留了该来源。

Canvas `+4A3C` 分支构造 16 位位图，`+4AA2` 调用 CreateDIBSection。其输出像素地址仍为 0 时，`+4AB7` 直接构造 `8007000E`，`+4ACD` 抛出，返回地址即报告中的 `+4AD2`。这比泛称“图片报错”更具体，但原始宽高、GDI 句柄计数、GetLastError、具体资源路径未保存，不能继续指定是哪张图或断言 GDI 配额已满。

续查已将这一调用链定位到**头顶聊天气泡的文字排版/字形缓存**。EXE `972401` 的 `0xA2` 分支进入 `937138`，读取角色聊天内容，`9374D0` 调用 `48A935`，再经 `48B330 → 4890C4 → 489B01 → 48ED5A` 调用字体 vtable `+24`。本地原生字体对象确认该入口是 `Canvas+32E7`，内部调用的返回点就是现场 `Canvas+3330`，之后经过 `+53D9 → +4D1A → +4AD2`。最新入站记录是 `CHATTEXT(0xA2), size=45`；没有原始正文，不能指定哪句话。此处失败的是字形位图，不是依据怪物攻城场景推测的怪物贴图。

22:33:50.892 进程退出码为 0，但前面已经有主循环异常逃逸，不能把退出码 0 当作无异常。22:34:03 新进程再次启动。

同线程约 860 ms 前另有 `_com_error=80004003`。续查已定位到宠物自动喝药后的 `Sound/Item.img/%08d/Use` 音效路径；具体药水 ID、空对象来源和与后续字形失败的因果关系仍未保存，见下文。

## dump 为什么没有内容

第一例两次写 dump、第二例一次写 dump，均记录：

```text
written=0 win32Error=2147942408 = 0x80070008
```

这是 HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY)，与 ZIP 中三个零字节文件一致。捕获功能已开启，属于实际写 dump 失败，不是没开诊断，也不能解读为“硬盘满”。此证据支持资源分配受限方向，但不足以区分进程地址空间与系统提交限制。

## 与此前修复的关系

- 两个进程均记录 `string.conversion.fix ready=1`，DLL timestamp `6ACF9ED2`、ImageSize `17C000`，与字符串修复交付版本的标识一致。最新现场无可用 dump，未取得该进程 DLL 的 CodeView 或完整文件哈希，版本判断不超过上述证据。
- 商城临时状态修复的安装标记未出现；两次异常也不是该修复针对的 EXE `A20586` / `A20807`，且服务端明确 cashshop=false。不能声称更新商城修复即可覆盖这两例。
- 旧 dump 中未列出完整 basename 为 Write.dll 的模块，但最新 dump 为空、文本不含完整模块表，不能以旧模块表证明昨晚进程没有外部模块。本案没有已识别 Write/ExUI/RichEdit 故障链的证据。
- 本地正式 EXE 已设置 IMAGE_FILE_LARGE_ADDRESS_AWARE；不应未经核实建议再次打所谓“4 GB 补丁”。32 位进程可用虚拟地址与物理 RAM 是不同限制。

## 内存证据边界

旧现场 SystemMemoryInfo 按 Windows SDK `minidumpapiset.h` 的 pack(4) 布局读取，并用编译探针核对结构偏移。可用物理总量 31.84 GiB，系统提交上限 35.22 GiB，旧时点提交余额约 22.59–23.40 GiB。这不是 10 月 15 日 22:29/22:33 的实时状态，不能外推当晚仍有相同余量。

**续查纠正了旧统计的高地址解析：**部分 32 位地址在 MemoryInfoList 的 64 位字段中被符号扩展成 `FFFFFFFFxxxxxxxx`，必须还原为 uint32 后统计。纠正后 7 份旧 dump 均覆盖到 `FFFF0000`，仅最后 64 KiB 未记录，并非缺少整个高地址区。旧现场空闲虚拟地址为 1,623.24–2,252.84 MiB，最大连续空闲段为 1,281.15–1,731.84 MiB。这些快照不支持当时已用尽地址空间，但不能代表新一晚的两个故障时点。`historical-memory.json` 已纠正；完整归一化记录在 `address-space-normalized.json`。

## 续查：实际字体入口、缓存回收和受控 GDI 压力

独立 x86 `/LARGEADDRESSAWARE` 探针加载本地 PCOM/Canvas，调用同一字体 vtable `+24`。未运行游戏入口、未加载 ijl15、未使用游戏原生 Ztl 分配器、未模拟完整战斗。每条测试文字包含固定前缀和 12 个汉字，轮换 4,000 字；宽度参数 4,096，确保实际处理整段文字。

| 条件 | 次数与结果 | 可支持的判断 |
| --- | --- | --- |
| 复用同一 SimSun 12 白色字体 | 24,000 次；首轮后私有内存 4,296,704 字节、GDI 225，后续 11 轮保持不变 | 未复现每条同样式聊天持续泄漏 |
| 每条重新创建、释放同样式字体 | 24,000 次；私有内存 4,247,552 字节、GDI 225 后保持不变 | 原生缓存会复用同一字体配置 |
| 3 个字体家族 × 3 个字号 | 24,000 次；字形覆盖充分后稳定于 GDI 2,136、私有内存 21,798,912 字节 | 字体种类/字形覆盖会增加占用，但本组后续稳定 |
| 仅保留低于 2 GiB 的空闲地址，不提交这些页 | 228,104 字节 HeapAlloc 成功；32 次文字测量成功 | 这个条件没有复现失败；堆申请可能复用了低地址堆段，不能当作全面高地址兼容证明 |
| 不断使用新颜色；随后受控补足 GDI 配额 | 字体本身累计到 GDI 9,902，额外 98 个临时 DC 补到 10,000；已有字体测量新字形时 CreateDIBSection 失败，返回 8007000E | 在物理/虚拟内存远未耗尽时，可触发同一原生字形分配失败路径 |

最后一组失败时私有内存约 47.9 MiB、空闲虚拟地址约 3.84 GiB，申请第一例大小的 228,104 字节堆块仍成功。探针里的字形位图为 256×12×16bpp（6,144 字节），**玩家现场宽高没有保存，不能把这个大小代入现场**。API 的 LastError 为 87，但原生 Canvas 统一转成 8007000E，不能凭该 HRESULT 区分真实内存不足、GDI 配额或其他参数/资源问题。

该测试人为大量换色并补齐 GDI 配额，是机制对照，不是玩家操作回放。不能据此认定玩家已有 10,000 个 GDI 对象，也不能用单纯 GDI 配额解释第一例 HeapAlloc 和两例 dump 写入失败。

进一步反汇编确认，字体缓存不是“永不回收”：`Canvas+3157` 每累计 128 次相关调用进入 `+5843` 扫描，只有使用计数为 0 且闲置时间超过 60,000 毫秒的字体会被普通扫描移除。探针最后调用相同原生清理函数的强制闲置清理分支，释放了全部未使用字体，GDI 降到 0。故不能把短时间大量不同字体缓存增长直接称为永久泄漏，也不能未经实际证据就在客户端周期性强清缓存。

本地 EXE 的通用字体入口有 0–55 的编号分支，聊天气泡也存在按传入样式单独构造字体的路径；当前 DLL 中没有找到持续产生任意新颜色的字体入口证据。两份含 Canvas 数据段的旧 dump 均记录字体缓存条目数 55，但对应哈希表/字体对象堆页没有保存，不能恢复字形位图总数或真实 GDI 总数。其余旧 dump 也不能补齐此信息。

证据：`font-reuse.txt`、`font-rebuild.txt`、`font-set.txt`、`font-low-address.txt`、`font-colors-pressure-sweep-final.txt`、`native-chat-path.txt`、`native-font-cache.txt`、`native-font-configuration.txt`、`historical-font-cache.json`。早期 `font-colors.txt` 记录无补足的快速换色失败；`font-colors-pressure-sweep.txt` 是未完成的中间探针输出，不用于最终统计，不叠加重复次数。

## 续查：是否由此前修复阻止释放

- `NativeLoopCleanupFix` 只把跳过初始化的局部指针置零，保留原 free(nullptr)，没有关闭资源缓存回收。
- `PcomExitFix` 只处理进程退出期间的 CRT 分配池析构顺序，正常运行的 Alloc/Realloc/Free 保留，不能把它直接解释为战斗中不释放内存。
- `LegacyStringConversionFix` 对异常容量哨兵重新求转换长度，不新增长期缓存或改变字符串所有权。
- 混色外观缓存与椅子派生画布有条目/像素预算；预算不等于全部引用资源的真实占用，不能据上限宣布它们绝无内存风险。当前样本没有指向它们增长失控的现场证据。
- 音效 `+AC40` 所申请的解码数组属于对象 `+2C`。`+5B45` 重建前先调用 `+228F` 释放旧数组，析构 `+D217` 同样释放该成员；大块释放最终进入 `HeapFree`。能确认正常释放路径存在，不能据静态代码证明实际对象一定及时析构。
- 原生 ResMan 仍有 `FlushCachedObjects` 调用（EXE `529326` 参数 180,000；`64291F/64298A` 参数 0）。前者是缓存年龄参数，不能解释成固定每三分钟执行。现有修复未找到直接禁用这些调用的证据。

因此仍需分别处理两个上游问题：第一例为何堆分配失败；第二例究竟是地址/提交压力、GDI 配额还是字形对象参数异常。现有附件不能将它们合并为已证实的单一泄漏。

## 复核与后续修复范围

隔离 x86 探针只加载本地正式 SOUND_DX8.DLL、Canvas.dll，不启动 BeiDou.exe、不加载 ijl15.dll、不连接服务端。三个函数槽分别与 GetProcessHeap、HeapAlloc、CreateDIBSection 的实际导出地址完全相等，验证了上述分配 API 识别。静态反汇编和日志交叉核对了错误传播；未伪造一次完整游戏复现。

后续修复应同时关注两层：音效/画布分配失败时如何安全结束本次资源操作，以及战斗运行中的资源占用、释放和缓存增长。只吞异常、底层简单返回 NULL 或关闭全部音效不能证明根治，也不能保证后续资源操作安全。要确定占用来源，仍缺最新故障时的进程地址空间/堆分布、系统提交余量及 GDI 资源计数。

本轮只分析、只读查询生产日志并保存本地证据与排查文档，没有修改业务源码、替换客户端、提交推送或部署。不是修复交付，也没有宣称玩家场景已经复现。

证据：`archive-manifest.json`、`text-inventory.json`、`crash-summary.json`、`dumps-summary.json`、`historical-memory.json`、`allocation-code.txt`、`api-probe.txt`，以及 `extracted/logs` 中对应 PID 的报告和 emergency/exit/trace 日志。临时探针源码为 workspace `.tmp/player-allocation-api-probe.cpp`。


## 续查：约 48 MB 的来源与限制位置

约 48 MiB 是独立字体探针在失败时的 `PROCESS_MEMORY_COUNTERS_EX.PrivateUsage` 实际读数，不是玩家游戏进程的占用，也不是内存上限。该程序没有加载完整地图、角色与游戏主循环；大量字形位图本身很小，因此可以先耗尽对象数量而仍只有几十 MiB 私有提交。

2026-10-16 只读核对本地测试机注册表：`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows\GDIProcessHandleQuota = 10000`，WOW6432Node 视图所见亦为 10000。此设置是 Windows 每进程 GDI 句柄配额；位图、字体、内存 DC 都会占用相应对象。依据：[Microsoft GDI Objects](https://learn.microsoft.com/en-us/windows/win32/sysinfo/gdi-objects)。`PrivateUsage` 是私有提交的字节数，与 GDI 个数属于不同指标，依据：[PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)。

新增同一原生字体路径的对照：GDI 已为 10000 时，额外 `VirtualAlloc` 提交 64 MiB 并逐页写入成功；私有提交从 50,049,024 字节（47.73 MiB）升至 117,518,336 字节（112.07 MiB），但字体 `CreateDIBSection` 仍失败，原生包装返回 8007000E。此结果明确排除了该实验存在约 48 MiB 的进程内存硬上限。清理临时 DC 与闲置字体后 GDI 归零，程序退出码 0。

对当前 DLL `ezorsia` 与启动器源码的定向搜索没有发现 `GDIProcessHandleQuota`、`SetProcessWorkingSetSize`、进程/Job 内存限制设置或 48 MiB 限制常量；这是指定源码范围的搜索结果，不等于证明玩家环境没有外部限制。已核实的客户端原生限制是字体闲置回收条件，不是 48 MiB 内存上限。

未修改注册表、未扩大系统配额、未替换 DLL。玩家机器的 GDI 配额和故障时计数仍未知，不能将本机实验配置代入玩家现场。证据：`local-gdi-quota.json`、`font-quota-memory-control.txt`、`font-quota-validation.json`；临时探针 `.tmp/player-font-quota-probe.cpp`。

## 再续查：音效的实际触发者和申请量

第一例的 EXE `67930E/67936D` 按 `opcode - 0xEF` 分派，`6793A3` 调用 `66BE61`，返回地址正是现场 `6793A9`。`66C089..66C095` 仅接受动作 21–37，随后减一，将 20–36 传入 `989BA1`。读取其 `98A238` 跳转表并解码原生 StringPool 后，这些编号对应 `Skill1`–`Skill16`、`SkillF`，路径模板是 `Sound/Mob.img/%07d/%s`。这确认了**处理怪物技能动作时的音效操作**，不是依据最后一条龙神技能 trace 推断为龙神技能崩溃。

`SOUND+AC0B` 调用的 API 经导入表核对为 `acmStreamSize`。得到的完整解码缓冲区长度是 228,096 字节；加上数组计数头和分配器头各 4 字节，最终 HeapAlloc 请求 228,104 字节。不是一次数 GB 的异常尺寸申请，也没有在这次加 8 的运算中发生溢出。

本地正式 `Sound/Mob.img`、`Sound/Item.img` 用已构建 MapleLib 只读反查，共读出 5,176 个音频或链接节点。怪物 Skill 节点包含 267 个实际音频、213 个链接；实际音频均为 MP3，单声道/双声道、22,050/44,100 Hz，最大压缩音频 96,967 字节，最长标注 15,663 ms，未见非正长度或超出上述集合的声道/采样率。此处只验证了本地文件的结构和元数据，**没有验证玩家文件哈希、全部音频解码或 213 个链接的完整解析，也没有从大小反推唯一怪物**。不能把压缩文件大小与解码后申请量直接相比。

第二例此前的 E_POINTER 帧链经过 `989705 → A09632`。`A095B5` 发送 `0xAB`，服务端 `RecvOpcode.java` 将其定义为 `PET_AUTO_POT`；`A09621..A0962D` 随后调用物品音效，StringPool 解码得到 `Sound/Item.img/%08d/Use`。可确认是在怪物伤害链中触发的宠物自动喝药音效。客户端之后仍处理封包，约 860 ms 后才遇到致命字形失败；音效入口 `43FB25` 自身也有异常处理分支 `43FECA`。因此该早期 E_POINTER 不能直接当作最终退出原因，也不能仅凭发生在前就推断它是同一内存压力导致。

原生 `CSoundMan` 以资源路径缓存音效数据，命中时更新时间并调整列表顺序；每次播放都经过 `43FD28 → 440C96(-1)`，按对象 `+18` 配置的年龄移除过期项，分别从查找表和列表删除。结合解码数组的正常释放路径，尚无证据证明“重复播放同一音效必然永久泄漏”，也未找到此前修复禁用这段回收的证据。

证据：`followup-native-confirmed.txt`、`followup-sound-loading.txt`、`native-sound-cache-cleanup.txt`、`sound-path-templates.json`、`mob-sound-action-table.json`、`sound-predecode-fallback.txt`、`local-sound-metadata.json`、`followup-validation.json`。临时资源探针为 `.tmp/player-sound-metadata/`；依赖已有 MapleLib，构建有 WindowsBase 版本引用警告，本次读取正常完成，无客户端资源写入。

## 再续查：GDI 上限不能独立解释整个现场

新增独立 x86 对照保持 GDI=10,000，再调用原生字体入口，确认 `CreateDIBSection` 失败且返回同一 `8007000E`。此时同等大小的 228,104 字节 HeapAlloc 仍成功，额外提交并逐页写入的 64 MiB 也成功。

在上述状态下，用与玩家 main-loop 报告相同的 `MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo`（`0x1820`）写 dump，并附带相同大小 3,688 字节的 BDS1 Snapshot 结构。对照写入成功：`written=1 error=0 bytes=564648`；反读确认 MDMP 签名及 BDS1 stream。这里的异常上下文是独立程序构造的对照，不是游戏现场；Windows/DbgHelp 版本、游戏堆和模块集合也不同。

结论只能是：**本机单纯耗尽每进程 GDI 配额，可以触发第二类字体失败，但不足以同时解释音效堆申请失败和玩家 dump 写入失败。** 不能据此排除玩家的字体崩溃涉及 GDI，也不能将本次对照等同于证明玩家发生了地址空间或系统提交耗尽。共同资源压力仍是需要优先核实的方向，尚没有当时的计数把它闭环。

两个报告中的 256 条最近入站记录分别覆盖 1,415 ms、1,160 ms。它们不能被描述为“256 个包全挤在几毫秒内”，也不能仅凭这些时间戳证明收包积压引起瞬时内存峰值。

证据：`font-gdi-dump-control.txt`、`font-gdi-limit-control.dmp`（仅本地对照样本）、`followup-validation.json`；探针 `.tmp/player-font-dump-control.cpp`。结束时释放临时 DC 和闲置字体，GDI 归零，退出码 0。

## 找到的直接修复入口，尚未实施

音效 `SOUND+AB30` 在申请完整解码缓冲区之前已经保留原压缩数据及解码状态。`+AC10..+AC23` 在 acmStreamSize 失败或尺寸超过预解码阈值时，直接进入 `+ACA1` 结束本次初始化，不申请这块完整缓冲区。只有尺寸满足预解码条件时才走 `+AC40 → +5B45 → HeapAlloc`，后者恰恰缺少失败检查。

因此后续应优先验证：让**这一处可选的整段预解码申请失败**回到原生已有的非整段预解码分支，而不是让底层分配器简单返回 NULL、跳过任意异常、取消全部音效或调整 Windows GDI 配额。落地前须验证旧数组释放、两层数组头、解码对象状态、后续播放与失败时的收尾；静态存在该分支尚不等于已经验证回退可安全完成播放。

特别注意：`+AC25..+AC30` 在数组申请之前已通过 `+AF1D` 构造栈上临时解码对象并进入受保护状态。原成功路径 `+AC8A..+AC9C` 恢复状态并调用 `+AFC6` 析构，后者会收尾解码状态、释放两个数组及引用。因此不能在 `+AC40` 失败后直接裸跳 `+ACA1`；必须保留相应析构及异常状态恢复，或在临时对象构造前安全处理分配决策。证据：`sound-fallback-cleanup.txt`。

聊天字形则必须单独处理分配失败及临时排版对象收尾；没有现场 GDI/地址空间证据时，不把增大句柄配额或定期强清字体缓存当作根治。对资源申请失败的容错只能解决直接致死路径，不能替代追查资源为什么不足。

本次续查仍未修改运行时代码、构建或替换交付 DLL、启动游戏、提交推送或部署。直接崩溃链和首例可选预解码的缺失失败分支已明确；最上游的资源压力来源仍未证实。

## 第三轮续查：确认了一块实际的大映射，不能混同私有内存

继续向上追查资源占用，识别出旧现场的最大单块文件映射：`Data/Item/Install/0301.img`。本地文件长度 329,685,244 字节，按页取整的映射长度 329,687,040 字节，即 **314.414 MiB**。

- 10 月 14 日 PID 21124 的映射基址为 `7FFF0000`。dump 保存的 32 段内容，分布于文件内部不同偏移，与本地文件相应位置全部一致，本轮重新比较共 32,768 字节。
- 同日 PID 14832 的映射基址为 `9C2A0000`，17 段、共 17,408 字节同样一致。
- 每份旧现场只识别到一块这一长度的映射；没有证据说玩家当时同时映射了三份椅子文件。以上是分散内容比对，不是完整文件哈希验证。

旧现场 PID 21124 有 848.47 MiB 已提交的映射区域、927.76 MiB 已提交的私有区域；PID 14832 分别为 727.27 MiB、654.55 MiB。`MEM_MAPPED` 页、进程私有提交量、工作集和空闲地址空间是不同指标，不能相互替换，也不能将映射大小直接说成消耗了同等物理 RAM 或私有提交。

进一步按本地 IMG 页取整长度筛选，再对 dump 保存的内容逐段核对，分别识别出 7 块 / 2 块至少 1 MiB 的资源映射，合计 344.92 / 322.64 MiB。包括第一份中的 `Map/Obj/acc1.img`、`Skill/Dragon/2218.img` 和第二份中的 `UI/Login.img`。没有保存内容或内容不一致的候选未认定为相应文件，剩余映射也不能一概归为 IMG。

这定位到了真实占用来源，但旧现场仍有前述 1.6–2.2 GiB 空闲地址空间，不能用旧现场中的大映射反过来证明旧现场或最新两例已经耗尽地址空间。

证据：`historical-chair-mapping.json`、`historical-mapped-file-identities.json`、`historical-allocation-ownership.json`、`historical-heap-headers.json`。后两个文件包含未捕获的对象页及堆头，不能把未捕获内存当作全零或已损坏。

## 第三轮续查：原生缓存参数和显式清理入口

原 EXE 初始化传入 `SetResManParam(0x11, -1, -1)`。旧 PID 21124 的实际 ResMan 对象也保存着两项 `-1`，第一类缓存条目数为 1,142，第二类为 0。

静态代码确认 `GetObject` 在 `ResMan+38AE/+38B8` 将对象 `+14/+4C` 分别传给 `+33F8/+349B`。两个清理函数对负数年龄立即返回。因此 **这组配置下，普通查询不会通过这两条路径按年龄淘汰缓存**。这不等于全客户端没有清理缓存。

重新扫描 EXE 中对全局 `BF14E8` 的直接引用，去重后确认 10 个 vtable `+24` 调用点，补齐了前轮只列举三处的范围：

| 调用点 | 参数及已核对的上下文 |
| --- | --- |
| `529326` | 180,000；位于 `529269`，该入口在 CField 虚表 `AF44B8+4`，也被多个派生场景初始化入口调用 |
| `5F4463` | 180,000；所在函数加载 `UI/MapLogin.img`、`UI/Login.img/Common/frame` |
| `5D0748 / 5D0C1E / 5D8A6F / 5D98F4 / 5DA2D0 / 5DA5C7` | 0；物品目录枚举/初始化调用链，直接或递归来自 `5CA71C` 的初始化序列；该序列由 `9F8B9B` 调用 |
| `64291F / 64298A` | 0；仅在对象 `+E8/+EC` 状态改变时进入，所在 `642890` 由 `49EAA4` 条件调用 |

扫描覆盖的是对该全局的直接引用，不保证包含所有经别名传递或间接调用的路径。180,000 仍然表示条目年龄门槛，不能据此声称有“三分钟定时器”。目前未找到此前修复删除这些原生调用的证据。

证据：`resman-cache-policy.txt`、`resman-cache-eviction.txt`、`resman-flush-context.txt`、`resman-flush-global-xrefs.json`、`resman-flush-callers-and-paths.json`、`native-cache-flush-owner-context.txt`。

## 第三轮续查：可复现的重复映射机制及反例

新增独立 x86/LAA 探针 `.tmp/player-resource-retention.cpp`，加载本地 PCOM、NameSpace、ResMan、Canvas，挂载同一 `Data`，配置同一 `0x11,-1,-1`。使用 `VirtualQuery` 和 `GetMappedFileNameW` 确认映射属于 `0301.img`，用 `GetProcessMemoryInfo` 分别记录私有提交和工作集。未运行游戏入口、未加载 ijl15、未配置游戏原生 Ztl 分配器。

最终对照结果（MiB）：

| 操作 | 椅子文件映射 | 私有提交 | 结果 |
| --- | ---: | ---: | --- |
| 读取并同时持有三把椅子的 `info`，中途不清缓存 | 314.414 | 3.254 | 共用一份文件映射 |
| 释放这三份引用，再调用 `FlushCachedObjects(0)` | 0 | 2.504 | 文件映射回收 |
| 持有第一把椅子 `info`，清缓存，再读并持有第二把；再次清缓存后读取第三把 | 943.242 | 5.320 | 同一文件出现三份映射 |
| 上述三份属性引用全部释放，缓存已清空 | 0 | 3.152 | 三份映射均回收 |
| 连续四次取已解码的 33×28 图标，只保留 Canvas，各次均清缓存 | 0 | 约 3.88–3.91 | 图标自身没有保留整份文件映射 |

普通读取后即使调用者释放返回值，ResMan 缓存仍可持有文件映射；立即调用 `FlushCachedObjects(180000)` 不移除刚读的条目。调用 `FlushCachedObjects(0)` 可以移除缓存引用，但**已有外部属性引用仍会维持旧映射**。之后新查询会建立另一份映射，故“缓存计数归零”并不保证这些外部引用占用的地址空间已释放。

已解码图标对照是重要边界：不能把“持有属性父对象”的结论扩展为“每保留一个图标就永久占住 314 MiB”。该实验也没有证明客户端实际保留了上述三份父对象，或昨晚进行了相同的清理/重载序列。没有出现不可回收的永久泄漏，也没有复现最新两例完整崩溃。

可确认的进展是：**资源文件映射能够在很低的私有提交量下占用大量 32 位地址空间，而且不恰当的缓存清理/重载与父对象持有会放大这种占用。** 因此排查不能仅盯私有内存，也不能直接把“周期性强清所有缓存”作为已验证修复。仍须有最新故障时的地址分布或实际持有者证据，才能把这条机制与那次 228,104 字节 HeapAlloc 失败、字形 DIB 失败以及 dump 写入失败连成因果链。

最终证据：`resource-retention-controls-final.txt`；较早独立运行分别保存在 `resource-retention-probe.txt`、`resource-reload-retention-probe.txt`、`resource-canvas-retention-probe.txt`，不同运行的绝对私有内存数值不能拼接成一条增长曲线。

本轮增加了离线探针和分析记录，没有修改运行时代码或交付文件。最新两例的直接致死缺陷已确定，上游资源耗尽的具体责任对象仍未闭环；本节不得作为“已确定椅子导致昨晚闪退”的结论引用。


## 2026-10-16 修复与取证落地：音效预解码回退、dump 预留、地址空间记录

### 复盘补充：为什么优先怀疑每进程地址空间耗尽

- 10 月 15 日两个双开进程启动只差 20 秒（UTC 13:27:35 / 13:27:55），分别运行 61、65 分钟后在攻城期间崩溃，前后只隔 4 分钟。整机提交额度耗尽时，第一个进程退出会释放资源；两个进程按相近曲线各自失败，更符合**每个 32 位进程各自的地址空间**被耗尽。BeiDou.exe 已启用 LAA，每进程上限约 4 GiB。
- 同机 10 月 14 日 PID 21124 的 dump 显示，启动 1 分钟时已占用约 2.27 GiB 虚拟地址（映射 848 MiB、私有 928 MiB、模块 239 MiB），剩余空间约 1.7 GiB。
- 攻城结束后重开的两个进程运行了 80 分钟以上，其中一个重登 26 次也没有崩溃，不支持“反复登录导致”的解释。
- 堆申请失败、DIB 创建失败、dump 写入报 `80070008` 三者都能由地址空间耗尽同时解释；单纯 GDI 配额解释不了后两者（见上文对照）。**这仍是候选结论**，需要下面新增的现场计数确认。
- 本轮未处理 `0301.img` 的拆分或瘦身（用户决定暂不做）。

### 音效分配链（静态反汇编，正式 Sound_DX8 `4B7C1425`）

- `+AC40 → +5B45`：先调用 `+228F` 释放旧数组并清零成员；`count == 0` 时直接返回 0；否则调用 `+19C0(count+4)`，将返回值加 4 写入成员，并在 `[p-4]` 写入 count。
- `+19C0` 是模块自带的分配器（this=`5181FDB0`）。小块走内部池；大块走 `+1A20`：`HeapAlloc([+1F994](), 0, size+4)`，随后在 `+1A33` 写入 size 头并返回 `p+4`。该路径不维护其他计数。`+1AC1` 释放时根据 `[p-4]` 的 size 头区分大小块。
- `+1F994`、`+1F98C` 是运行期填充的函数槽，不在按名导入表中。补丁始终通过这两个槽调用，不直接调用 kernel32。

### 改动

| 模块 | 内容 |
|---|---|
| `SoundAllocationFallback` | 第一版“试分配后立即释放”无效：原生代码随后重新申请，仍可能失败，故障注入可复现 `+1A33` 崩溃。现改为在 `+AC18` 的阈值判断后，经模块自己的槽**真正分配** `decoded+8` 字节，此时 `+AF1D` 临时解码对象尚未构造。分配失败时走原生 `+ACA1` 非整段预解码分支，没有需要收尾的对象；分配成功则把这块内存记为当前线程待用（thread_local）。`+1A2D` 的 `call [HeapAlloc 槽]` 改为调用 `AllocatorHeapAlloc`：堆句柄与字节数完全一致时直接交出这块内存，其他请求一律透传原槽。上一次未被领取的待用块，会在下一次决策时释放。`count==0` 不分配。两处补丁均在 timestamp/SizeOfImage 吻合、`+AC10..+AC2F` 与 `+1A20..+1A34`（含重定位后的槽地址）完全一致时才写入，且先写领取点、再写决策点。 |
| 安装时机 | 不再使用 `LdrRegisterDllNotification`：该回调处于加载器锁内，不宜调用其他模块函数或写日志。现用 Detours 挂接 `LoadLibraryA`（PCOM 实际使用的 API），在原调用返回之后（加载器锁已释放，调用方尚未使用模块）再检查并打补丁；安装时如果模块已加载，则直接补丁。 |
| `CrashReporter` dump 预留 | 启用 crashDump 时预留 4×8 MiB 地址空间（只 reserve，不占用提交额度）。每次写 dump 前全部释放，写完后尽量补回。实测仅释放 8 MiB 时，堆已满状态下 DbgHelp 仍失败（`8007001F`），因此改为一次全部释放。 |
| `CrashReporter` 现场计数 | 释放预留前用 `VirtualQuery` 统计地址空间，崩溃报告新增 `memoryAtFault=` 和 `memoryTop00..07=`（最大 8 个分配块、类型及映射文件路径末三级），emergency 日志新增 `memory_at_fault`。统计只使用栈内存；日志在释放预留之后才写，因为 `CreateFileW` 转换路径时也需要分配内存。 |
| 系统提交量 | 改用 `GetPerformanceInfo`，字段为 `sysCommitTotalMiB`、`sysCommitLimitMiB`、`sysPhysAvailMiB`。`GlobalMemoryStatusEx` 的 page file 字段可能返回进程或作业的较小限额，不能当作整机数值。 |
| `MemoryTelemetry` | 新增 `ijl15-memory-*.log`，默认每 60 秒记录一行 `memory_sample`。剩余地址空间 < 768 MiB 或最大连续块 < 256 MiB 时改为每 15 秒一次，且每 10 分钟最多写一次 `memory_top`。环境变量 `BEIDOU_MEMORY_TELEMETRY_SECONDS=0` 关闭，其他取值限制在 10–3600 秒。该功能与 crashDump 开关无关。 |

### 判读

- `memory_sample` 中 `sinceStartSec` 是进程启动后的秒数。重点看 `vaUsedMiB`/`largestFreeMiB` 的增长曲线，以及 `commitMappedMiB`、`commitPrivateMiB`、`reservedMiB` 中是哪一项在涨。`gdi`/`gdiPeak` 接近 10000 时说明 GDI 配额也受压；`sysCommitTotalMiB` 接近 `sysCommitLimitMiB` 才表示整机提交额度紧张。
- `memory_top`/`memoryTopNN` 中 `type=mapped file=Item\Install\0301.img` 之类的条目可直接定位映射文件；出现同一文件的多份映射即对应上文的重复映射机制。
- `sound_predecode_fallback skip` 出现时，说明当时进程已经申请不到这段音频的整段缓冲区，原版会在此处闪退。这是资源压力的早期信号，回退本身不等于问题已解决。

### 验证

- 新增 `tests/MemoryExhaustionEvidenceTest.ps1`（独立 x86/LAA 进程，经 `LoadLibraryA` 加载正式 `BeiDou-Client/Sound_DX8.dll`，不启动游戏）：
  - `LoadLibraryA` 返回后，两处补丁均已写入，周边原生指令保持不变；以伪造栈帧执行决策桩，esi/edi/ebx/ebp 保持不变。
  - **复现审查中的故障注入**：将模块 HeapAlloc 槽替换为“第一次调用成功、之后一律失败”的注入函数。决策桩完成真实分配后，再直接调用原生 `+5B45(228096)`：领取到同一块内存（数据指针 = 待用块 + 8），槽调用次数保持 1 次，size/count 头与原生布局一致。原生 `+228F` 释放后，`HeapValidate` 通过。
  - 槽第一次调用就失败：走 `+ACA1`，且没有残留待用块。其他尺寸的请求透传；地址空间与堆全部耗尽时回退，释放后恢复预解码；未被领取的待用块会被替换释放，堆保持有效；空音频不分配。
  - 同样的耗尽状态下，不释放预留的对照连建 dump 文件都失败（错误 8，即 ERROR_NOT_ENOUGH_MEMORY，对应玩家日志中的 `80070008`）；使用预留后，main-loop dump（约 55 KB）和随后的完整 triage dump（约 1.19 MB）都成功写出，报告包含 `memoryAtFault`/`memoryTop`。
- 回归通过：ClientLogTest、DisconnectDiagnosticsTest、NativeExitDiagnosticsTest、PoolGuardTest（`-ClientExe BeiDou-Client/BeiDou.exe`）、TargetedCrashSnapshotTest。LifecycleLoggingHarness 本轮第一次运行退出码为 1（该次输出未保存），随后连续 3 次全部通过，原因未查明。NativeLoopCleanupFixTest 在 HEAD `2104d23` 上就因未编入 TargetedCrashSnapshot.cpp 而链接失败（LNK2019），与本次改动无关。
- `Release|x86` 构建产物已同步到 `BeiDou-Client/ijl15.dll`，SHA-256 `fdcfca2852ab753d37eb094f7fa00e203498347fbb6f86d9bfda1b8481dbfb37`。
- 尚未实机验证、未发布玩家版本。下一场攻城需要玩家使用新版，并上传含 `ijl15-memory-*.log` 和 dump 的完整日志包。

### 2026-10-16 提交前复核

- 复核三项整改：保留并交付真实音频缓冲区、移除 DLL 通知回调中的补丁与日志操作、系统提交量改用 GetPerformanceInfo。未发现新的提交阻塞问题。
- 再次执行 MemoryExhaustionEvidenceTest、ClientLogTest、LifecycleLoggingHarness，均通过；本次生命周期测试的 14 个模式全部通过。历史偶发失败仍按上文保留记录。
- 从暂存区导出独立源码快照执行 Release|x86 构建，0 错误。基线包含已独立提交的龙神洗点修复 ea3ea26，本轮新增提交仅含崩溃修复、配套验证与本记录。
- 构建 DLL 与客户端 DLL 按上述 SHA-256 一致交付。该验证覆盖离线故障注入及资源耗尽场景，不代表已完成玩家攻城实测，也不能证明字体申请失败及整体资源耗尽根因已彻底修复。
