# BeiDou-ijl15-BeiDou 文档索引

客户端 `ijl15.dll` 源码文档。构建、同步与交付规则见 workspace 根 [AGENTS.md](../../AGENTS.md#ijl15-rules)。本目录只放现行说明；带日期的崩溃排查、修复与交付记录在 [records/](records/INDEX.md)。按需打开单篇，不必通读。

## 平台与运行机制

- [endpoint-override.md](endpoint-override.md) — 连接线路由启动器下发，DLL 不再读 config.ini 连接配置
- [launcher-coexistence.md](launcher-coexistence.md) — 正式与开发启动器共存
- [macos-wine.md](macos-wine.md) — macOS / Wine 11 兼容总入口（字体、显示细节在 records/macos-*）
- [module-audit.md](module-audit.md) — DLL 模块加密日志；2026-10-26 加密前批次压缩和无变化快照去重为现行实现，待客户端发布
- [default-exit-monitor.md](default-exit-monitor.md) — 退出监视与条件 mini dump（诊断开关现由启动器 `GameLaunchSettings` 下发）

## 诊断

[disconnect-diagnostics.md](disconnect-diagnostics.md)、[death-dialog-recovery.md](death-dialog-recovery.md)、[login-request-initialization.md](login-request-initialization.md)、[reactor-timing-diagnostics.md](reactor-timing-diagnostics.md)、[snail-timing-diagnostics.md](snail-timing-diagnostics.md)、[evan-attack-diagnostics.md](evan-attack-diagnostics.md)、[evan-timing-diagnostics.md](evan-timing-diagnostics.md)

## 功能与兼容

- 椅子小文件：[现行交付与验证](../../BeiDou-Server/docs/records/november-refresh-20261028.md#椅子小文件与发布体积2026-10-28) — 保留 release 45 的旧合集，新增椅子用五位小文件；DLL 合并原生根与重定向深路径，配套服务端按 ID 优先读取小文件。旧椅子、覆盖/回退、真实 PCOM 读取与宠物座位回归通过，已同步本地，实机待用户验收。
- 十一月焕新：[通用武器、ItemEff、套装站姿和椅子兼容](../../BeiDou-Server/docs/records/november-refresh-20261028.md) — 已接入 16 件独立特效、往期两套校服的完整随机同伴、运动套服原版浮动站姿及特殊动作特效规则，修复商店武器类别、名片接缝和标题性别重复。12:48 用户反馈小天使穿戴崩溃后，已修正动作表的引用类型标记，补测真实加载分支及 112 个身体/衣服图片引用；12:57 新 DLL 已安装本地，实机待用户复测。现行依据见记录中的“12:48 小天使穿戴崩溃修复”。17:55 补上 GM 隐身时特效闪烁的通用透明度修复，真实绘图组件复现与修复回归通过，新 DLL 已替换本地，详见“GM 隐身时装备特效反复闪亮”。

- 界面与系统：[buff-countdown.md](buff-countdown.md)、[character-slots-20.md](character-slots-20.md)、[hired-merchant-clock.md](hired-merchant-clock.md)、[maple-life-localization.md](maple-life-localization.md)、[high-id-face-preview.md](high-id-face-preview.md)
- 技能跳字与指令：[combo-tempest-damage-timing.md](combo-tempest-damage-timing.md)、[shadow-partner-damage-timing.md](shadow-partner-damage-timing.md)、[aran-combo-command.md](aran-combo-command.md)、[skill-point-sync.md](skill-point-sync.md)
- 外星访客：[visitors-morph.md](visitors-morph.md) — Morph 64–67 输入、等级、动作、固定伤害与原生 Reactor 协议兼容；以当前实现和离线测试为依据，实机待验收
- 战甲强化：[visitors-scroll.md](visitors-scroll.md) — 限定材料/装备/资源分类的拖放放行、247/249 双击保护与原生 0x56；以当前实现及离线测试为依据，实机待验收
- 护送怪梯绳：[visitors-mob-climb.md](visitors-mob-climb.md) — 42 项私有动作表、rope/ladder stance、懒加载缺帧回退及缓存生命周期；实机待验收
- OS4 飞船：[visitors-shuttle.md](visitors-shuttle.md) — 1932037 模型分类、四职业 1065 施放/效果、原生限制与上下马计数；含 2026-10-24 限时技能及相关到期提示汉化，实机待验收
- 骑宠抓绳：[mount-rope-alignment-20261018](records/mount-rope-alignment-20261018.md) — 以 2026-10-24 井口回归修正为准：显示位置查询未命中时回退原生位置，离线通过、实机待验证
- 访客套装：[set-item-bonus.md](set-item-bonus.md) — 0x1011 权威快照、装备循环前安全整数加算和游戏线程属性重算；实机待验收
- 套装提示框：[set-item-tooltip.md](set-item-tooltip.md) — 092 布局、set 5 中文资源；修复实测发现的坐标偏移/抖动，原生坐标转换和幂等定位已离线验证，本版未安装、待复测
- 龙神 084 → 083：[单点重置卷与窗口兼容](../../BeiDou-Server/docs/evan-sp-accounting.md)（10-28，待用户实机验收）、[evan-ui-localization.md](evan-ui-localization.md)、[evan-killer-wings.md](evan-killer-wings.md)、[evan-phantom-imprint.md](evan-phantom-imprint.md)、[evan-soul-stone.md](evan-soul-stone.md)、[evan-maple-hero-animation.md](evan-maple-hero-animation.md)、[evan-dark-fog-animation.md](evan-dark-fog-animation.md)、[evan-thrust-flame-wheel-effects.md](evan-thrust-flame-wheel-effects.md)、[evan-dragon-visibility.md](evan-dragon-visibility.md)、[evan-dragon-follow-facing.md](evan-dragon-follow-facing.md)、[evan-dream-frame-scaling.md](evan-dream-frame-scaling.md)、[evan-incubation-scaling.md](evan-incubation-scaling.md)

## 主题的现行依据在 records 中

| 主题 | 依据 |
| --- | --- |
| 崩溃修复 | 最新：[聊天中文补丁越界修复 10-26](records/chat-text-bounds-crash-20261026.md)（真实原生循环隔离复现，60 个修复后边界用例通过，本地 DLL 已同步，实机待验证）。10-08 系列见 [remaining-crashes-followup-20261008](records/remaining-crashes-followup-20261008.md)，10-11 [crash-fixes-20261011](records/crash-fixes-20261011.md)，其他见 records 中 `*-crash*`、`pool-guard-*`、`player-logs*` |
| 衣柜客户端 | [wardrobe-entry-delivery-20261018](records/wardrobe-entry-delivery-20261018.md)、[wardrobe-preview-ownership-20261018](records/wardrobe-preview-ownership-20261018.md)；2026-10-24 一键整理、跨来源分类/编号排序，10-20 界面重做及 10-21 原生音效、空部位暗化、套服裤裙标红见[服务端衣柜记录](../../BeiDou-Server/docs/records/wardrobe-redesign-20261020.md) |
| 宠物 | [pet-potion-settings-20261018](records/pet-potion-settings-20261018.md)、[main-pet-inventory-mark-20261016](records/main-pet-inventory-mark-20261016.md) |

记录中的 SHA-256、Windows 路径（含旧目录名 `BeiDou-ijl15`）与“当前/待验证”措辞均为当时状态。
