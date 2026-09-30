# 普通职业与龙神 SP 显示及加点（083）

配套服务端 `docs/skill-point-allocation.md`、`docs/evan-sp-accounting.md`。083 普通职业共享一个 SP 数值，CUISkill 每页原本显示同一值；`0x100C` 版本 1 传递四转额度。龙神 CUISkillEx 使用版本 2 的十册 int 额度，修复累计 SP 超过 255 时的截断。

## 原生定位

以下地址依据本地 `BeiDou-Client/BeiDou.exe` 的 x86 反汇编（SHA-256 `6C4FC608E5B44708D19F07D5B88729126539E919D0B79035DA76610158F355C2`），安装时核对目标字节后才统一打补丁。

| 位置 | 原生行为 | 补丁 |
| --- | --- | --- |
| `0x008AC412`，`E8 C6 82 BC FF` | Draw 调用 `0x004746DD` 解码共享 SP | 保留原解码与参数清理，将结果替换为当前页额度 |
| `0x008AD866`，`E8 72 6E BC FF` | UpdateButtons 解码共享 SP | 同上，使加号按当前页余额刷新 |
| `0x008ACFFC`，`83 7B 2C 00 0F 85 2B 02 00 00` | OnClick 的解锁条件和旧分转检查入口 | 保留解锁判断，使用服务端额度；无快照时回到原生路径 |

Draw/UpdateButtons 的 ESI 是 CUISkill，`+0x5B8` 是 CCtrlTab，`+0x3C` 是页签索引（0 初心者，1–4 转）。点击时 EBX 指向技能结构，首字段为技能 ID，`+0x2C` 为解锁条件；前置 `0x00A08E05` 技能有效性判断仍执行。额度正数跳至 `0x008AD227` 发送，0 跳至 `0x008AD231` 返回；未知额度跳至 `0x008AD006` 原生判断。

CUISkill 单例位于 `0x00BF1080`，构造函数写入的主 vtable 是 `0x00B3B5CC`。收到快照后，确认 vtable 才调用 UpdateButtons (`0x008AD7C9`) 和 InvalidateRect (`0x009E04C9`)。

龙神 CUISkillEx 的单例是 `0x00BF10A4`（构造 `0x008B985F`，析构 `0x008B9D68`），主 vtable `0x00B3B748`，UpdateButtons `0x008BCB89`，InvalidateRect 同上。`0x00B3B654` 属于技能宏窗口，不是 CUISkillEx。

| 龙神位置 | 原生字节 | 行为 |
| --- | --- | --- |
| `0x008BBDD8` | `0F B6 F8 E8 D7 CB B6 FF` | Draw 的 AL byte 转 EDI 后替换完整额度；保留 CharacterData 释放调用，返回 `0x008BBDE0` |
| `0x008BCC07` | `E8 A9 5A C2 FF 0F B6 C0` | 保留 byte getter `0x004E26B5` 的参数清理，再替换 EAX，返回 `0x008BCC0F` |
| `0x008BC68A` | `E8 26 60 C2 FF 84 C0` | 点击将 byte 零值测试改为完整 EAX 测试，返回 `0x008BC691`；保留后续原生解锁与掌握等级校验 |

龙神 Draw 窗口指针为 ESI，按钮/点击为 EDI；页签同样位于 `+0x5B8 -> +0x3C`，索引 0 为初心者、1–10 为成长。安装前统一验证六处签名，任一不符均不部分安装。

## 状态与协议

`SkillPointSyncState.h` 只读观察 native SET_FIELD 的角色/职业，以及 STAT_CHANGED 的职业变化；不修改原封包游标。更换角色、职业会失效旧快照。自定义帧需要版本、长度、角色、职业完全匹配，额度非负、递减，且尚未到达的转数额度为 0。无效自定义帧也被消费，避免交给原生 dispatcher。

帧布局（含传输头）：4 字节头，opcode ushort=`0x100C`，版本 byte，角色 uint，职业 ushort，再跟额度。普通职业版本 1 为 4 个 ushort，共 21 字节；龙神版本 2 为 10 个非负 int，共 53 字节。后台状态使用 SRW lock，释放锁后刷新窗口。

## 离线验证

运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tests/SkillPointSyncTest.ps1`。测试编译同一 `SkillPointSync.cpp`，仅将原生地址整体平移到独立测试页，执行六个实际 naked caves，验证显示/按钮、允许/拒绝/回退跳转、调用栈，以及签名不符时不部分安装。龙神覆盖 0/1/255/256/285/550/65536、所有帧截断长度、未成长页、角色和职业不匹配。测试程序不会启动或注入游戏。

正式使用 `Release|x86` 构建并同步客户端 `ijl15.dll`，核对 SHA-256。最终窗口显示与连续加点由用户实机验证。
