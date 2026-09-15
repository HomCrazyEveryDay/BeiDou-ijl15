# 龙神冒险岛勇士动作恢复

技能 22171000 原版 GMS084 使用 mapleHero，只有 affected，无普通 effect；龙 Skill/Dragon/2217.img 和 2218.img 各有25帧 mapleHero。当前客户端身体00002000缺少动作，083动作注册表也没有 mapleHero。

从完整084提取资源增量合并 Character/00002000.img/mapleHero（4阶段引用alert动作），配套追加服务端源XML。IMG由138019变138287字节，工具确认无关像素不变、反读成功。客户端及源XML动作字段一致。

DLL StringPool 5505 原 elementalRegistance 是未被当前任何技能引用的083原型动作，通过既有原生注册流程改注册 mapleHero。083 004A5DE7 和004A9C05均使用该字符串，分别建立人物与龙动作表；与已有blaze/recoveryAura注册方案一致，不改技能action名称，不复制其他职业特效。原生BUFF处理已有22171000分支。

验证：原版084和当前技能资源字段读取、龙9/10阶25帧读取、身体动作原始4阶段反读、无旧动作引用、DLL Release x86构建及安装哈希一致。游戏实际动作由用户验证；此轮不改变buff数值或服务端效果逻辑。
