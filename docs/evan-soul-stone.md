# 灵魂之石与熊熊烈火核对

## 原版资源

来源为已验证的 GMS084 `exports/evan/gms084/Data`；Character/00002000.img 源 SHA-256：`A175EE36D44972777A71C51F61D047F63B9EFF976B3CA81263D65AECA565E5E1`。

灵魂之石 22181003 的 Skill 节点（277 个签名）和 Dragon/soulStone（57 个签名）均与原版一致，包含像素。缺失的是 Character/soulStone：本次增量合并原版全部 35 个签名，同步服务端 Character XML。IMG 从 139889 增至 140320 字节，工具反读及无关结构、像素比较通过。

StringPool 5501 原型 dragonShield 无现有技能引用，原生人物和龙注册点为 004A5EA4、004A9C47；对应表项 BED478/BEC43C 只有初始化引用。将该空闲槽注册为 soulStone，读取原名、原始帧，不替换为其他技能动作。

熊熊烈火 22181001 的 Skill（266）、Dragon/blaze（45）、Character/blaze（49）签名及像素与 GMS084 相同。原版具有 ball/hit，没有独立 effect；保持原版素材与数值。既有 Blaze/Flame Wheel 原生分派测试通过，本次没有修改熊熊烈火。

## 083 原生状态与复活协议

本地 BeiDou.exe 反汇编确认：

- 00791547 初始化 SecondaryStat 第 81 位，标志对象 BEFAC0。其编码对应网络第一个 long 的 `0x2000000000000`。
- 0078677B 在本地状态解码中读取该位：short 数值、int 技能 ID、int 持续毫秒，写入 SecondaryStat C7C/C88/C94。
- 00897CCC 的死亡界面读取 C7C；大于零使用 `UI/UIWindow.img/Notice/4`。此画布已存在。
- 008981E1 的“是”按钮将 1 传给 00898224，再调用 0053035D。在 00530435 写入普通复活请求的 Yes 字节，和复活道具共用，未增加新字段。
- SHOW_ITEM_GAIN_INCHAT 效果 0x1A 由 009377D9 的跳表 00938AD2 分派至 0093879C，读取 StringPool 5460 的灵魂之石复活消息，无后续字段。

因此不需要伪装其他状态位或另建复活界面；DLL 本次只补动作注册。

## 服务端配套

新增 SOUL_STONE 状态，读取原 WZ x/y/time/cooltime；按范围筛选同队存活角色，随机不重复选取至多 y 人，包含施法者。无队伍时只给自己；不额外保证施法者获得状态。接收者播放原 affected，施法消耗和冷却只在主调用处理。

死亡时保留原生状态供死亡界面读取，将有效状态保存为一次性的同地图复活资格。选择原地复活时优先消耗该资格，恢复 x% HP 并回到死亡坐标，不消耗复活道具；跨图、普通回城、其他复活清理资格。死亡事件及原有经验/护符规则保留。CPQ 沿用自身死亡流程。

满级：2 人，持续 300 秒，恢复 50% HP，冷却 600 秒。没有修改技能数值或当前中文冷却描述。黑暗迷雾此前的 20→4 秒冷却修改保留。

## 验证与实测

- Java Maven 定向测试：EvanResourceEffectTest、SoulStoneRevivalTest，覆盖全部等级、网络状态字段、组队人数、非队友/死亡过滤、单人、自身在候选内、资格过期/重复/跨图与原地 HP 恢复。
- Release|x86 构建通过；EvanBlazeDispatchTest 通过。
- 关闭客户端后已安装 Character IMG 与 DLL，安装 DLL 与构建输出 SHA-256 相同：`5A5BFE16459F2F03F39FBBCD8438AB1E2F890FB6BFF3BFE050D56899539B3051`。
- 客户端未自动启动。待用户验证人物与龙完整施法、受益特效、状态倒计时；分别测试单人和三人以上组队的 1/20 级技能；死亡选择是/否，核对原地 HP、单次消耗及复活道具数量。熊熊烈火测试左右朝向、弹道与多个目标命中特效。

## 后续汉化与 Boss 驱散免疫

用户反馈原地复活弹窗是韩文、聊天提示是英文，并要求灵魂之石不被 Boss 消除增益技能清除。

- `UIWindow.img/Notice/4` 原图的韩文已替换为“借助灵魂之石的力量，你可以在当前地图复活。是否立即原地复活？”。保留墓碑、边框、尺寸、origin 和原生按钮。通过 `tools/LocalizeSoulStoneNotice` 增量生成；中文 UI XML 的目标 canvas 使用 basedata 内嵌 PNG，保留可重建的像素源，不全量重建 IMG。无关结构与像素一致；IMG 11809508→11811996 字节。
- 英文确认为原生 StringPool 5460，使用既有 StringPool hook 精确匹配原文后返回 GBK 中文“你已借助灵魂之石的力量在当前地图复活。”。
- 服务端 `isMonsterDispelImmuneSkill` 增加 `Evan.SOUL_STONE`，Boss 的 `MobSkill.applyDispelEffect` 经 `Character.dispel` 使用该规则。正常到期、主动取消及复活消耗不变。技能描述补充免驱散说明，XML/IMG 已通过 orange-wz diff patch 与反查。
- Maven 定向测试 StatEffectTest、SoulStoneRevivalTest、EvanResourceEffectTest 通过。Release|x86 构建通过，客户端关闭后同步；新 DLL SHA-256：`89D792412B4BBF698DCFA43672EB81DDC787D95A5535C9B89480ACF1B172F747`。待用户重开验证汉化及 Boss 驱散后的状态保留。
