# Visitor 套装：服务器权威快照

## 协议

2026-10-22 实现。使用已分配的 S2C **0x1011 UPDATE_SET_ITEM_BONUS**，不采用早期勘探报告建议的 0x1025 或扩展体。

| body 偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0 | u8 | version=1 |
| 1 | u8 | pieces，0–4 |
| 2 / 4 / 6 / 8 | i16 LE | PDD / MDD / ACC / EVA |

body=10 字节；含 opcode 的 wire 数据=12 字节；现有 CInPacket 含四字节前缀，故 `DataLen=16`、opcode 在 data+4、body 在 data+6。逐字段解析，不 memcpy 结构体。长度、版本、件数不符或任一加值为负时消费该扩展包并清零快照；其他 opcode 透传，opcode 0 先清空快照及待刷新状态。

属性注入不扫描装备、不在 DLL 重新判断套装 ID/件数阈值。服务器目前 2/3 件为 `(20,20,0,0)`，4 件为 `(20,20,20,20)`；DLL 使用收到的非负 i16 值，不把这组数值硬编码成另一套规则。[套装提示框](set-item-tooltip.md) 独立读取本地装备用于显示，不参与属性注入；游戏线程完成一次 pending 属性刷新后通知提示框更新。

## 接收、线程与生命周期

- `HpMpAlert.cpp` 的现有 ProcessPacketBody 链注册 `SetItemBonus::HandlePacket`，没有新增接收 detour。
- 原生 `0x004965F1` 接收分派来自 `0x0049651D`，上层包含游戏消息循环 `0x009F5C50` 和 socket 事件处理链。模块仍在第一次本地 `SecondaryStat::SetFrom` 中绑定线程 ID；只有该线程可调用 `ValidateStat`。
- 每个新快照标记一次 pending；角色 ready 时在该包的原有游戏线程回调内调用 `0x00A0843C`，ECX=`*(void**)0x00BE7918`，无栈参数。其他线程或角色尚未 ready 时只暂存，后续接收回调在已绑定线程消费 pending。未再收到包时不会主动创建线程或轮询。
- 若快照早于第一次本地重算，第一次重算可应用它，随后接收回调再完成一次原生 ValidateStat；两次均先重建基础值，不直接累加当前总值。刷新重入被抑制。
- 用 SRW 锁保护整份快照与 pending；调用原生方法前释放锁。已激活快照绑定 context 和 CharacterData 指针；发现身份改变即清空。opcode 0 清空未激活快照，防止换账号带入旧值；同角色换图、死亡不主动清零。
- 当前协议没有 characterId/sessionEpoch/revision，不能识别同一连接内服务器重放的过期快照，也不能仅凭指针解决对象地址复用。依赖服务器在新登录后和装备改变时按序发送完整快照（包括零值）；这里不声称提供早期方案的跨会话序号校验。

## 属性注入与 ABI

补点 **0x0077F86F**，原字节 **`89 45 D8 83 C8 FF`**，新字节 `E9 rel32 90`，续 **0x0077F875**。ESI=SecondaryStat。gate 保存 flags 和通用寄存器，`__stdcall ApplyBonus(secondary)` 后恢复，再重放 `mov [ebp-0x28],eax; or eax,-1`。原栈上残留参数留给续接处 `pop ecx`，不提前清理。

只有 `secondary == *(void**)0x00BE7918 + 0x2134` 且角色 ready 时生效；不改远端或临时 SecondaryStat。注入发生在基础初始化之后、装备循环之前，继续沿用后续装备、被动和原生限幅，不改临时 buff 字段，也不 hook 面板 getter。

| 字段 | encoded 偏移 | checksum 偏移 |
| --- | --- | --- |
| PDD | +0x30 | +0x38 |
| MDD | +0x90 | +0x98 |
| ACC | +0xC0 | +0xC8 |
| EVA | +0xF0 | +0xF8 |

reader **0x00416563**：`int __cdecl(const void* encoded,unsigned checksum)`。writer **0x004165B1**：`unsigned __fastcall(int value,void* encoded)`，ECX=value、EDX=encoded、EAX=新 checksum，调用者必须写 encoded+8。加算使用溢出保护；普通游戏数值的后续上限仍由原生代码决定。

每次 ValidateStat 重建基础和装备总值，快照重复下发、穿脱或原生重复重算不会在旧总值上叠加。面板和原生计算读取同一组总值；被动覆盖、限幅和具体战斗数值仍需实测，不能假设所有情况下最终显示都严格多 20。

## 安装与测试

安装先检查 PE 基线、补点、reader/writer/ValidateStat 入口字节；不符整组拒绝，日志一次。写入后刷新指令缓存并恢复原页面保护，刷新失败回滚。

`tests/SetItemBonusTest.ps1` 对真实 EXE 做 SHA-256/字节验证，在隔离映射执行 gate 和 mock 原生服务：覆盖精确包长、大小端、负值/版本/件数非法、清零、独立权威数值、重复快照不累计、换角色、pending/reset、异线程延迟、刷新重入、本地对象限定、安全整数 ABI/checksum、栈参数及 flags。

用户实机验收：登录时 0/1/2/3/4 件，穿脱和销毁/到期，属性窗口即时刷新、与 buff/被动叠加和限幅，死亡/换图/换频道/换账号，以及原生受击/命中表现。测试不启动游戏或服务端，不自动部署 DLL。
