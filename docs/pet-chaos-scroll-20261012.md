# 三宠装备混沌卷轴兼容修复（2026-10-12）

## 问题与范围

用户反馈月神兔、贝拉、托特的装备无法使用普通混沌与正向混沌。按照已有三宠记录，“月神兔”对应死神兔 5002416。

| 宠物 | 装备 | 本次放行卷轴 |
| --- | --- | --- |
| 托特 5002414 | 静默之刃 1802896 | 混沌卷轴60% 2049100、正向混沌卷轴50% 2049115 |
| 贝拉 5002415 | 寂静之刃 1802897 | 同上 |
| 死神兔 5002416 | 死神兔灵气 1802899 | 同上 |

## 证据与实现

- 通过 workspace `tools/xml-img-patcher.ps1 dump-xml` 反读三张实际客户端 `Data/Character/PetEquip/*.img` 及 `Data/Item/Consume/0204.img`。装备均为 `tuc=10`、`incSTR/incDEX/incINT/incLUK=1`、`cash=1`，与服务端 `gms-server/wz/Character.wz/PetEquip/*.img.xml` 一致；两张卷轴均有 `randstat=1`，成功率分别 60/50，且无装备白名单。此次目标数值位于公共 `wz` 层，中文名称位于 `wz-zh-CN/String.wz`。
- 当前 083 EXE 的 `004F5497` 是公共卷轴/装备类别判定。混沌、白衣分支在 `004F54FE` 通过 `itemId / 100000 == 18` 排除宠物装备，随后回落到普通类别匹配，导致目标两张混沌都返回失败。普通拖放调用点为 `004F4E1C`，砸卷界面调用点为 `0082CCC5`。
- 新增 `PetEquipScrollCompatibility`，校验 EXE 的 PE 身份及目标字节后，仅替换 `004F54FE` 的 5 字节比较/跳转。只对上表六种组合跳往原成功出口 `004F5531`；其他宠物装备及卷轴仍走 `004F5503` 的原匹配，普通装备沿用原允许分支。原有 `PrivateCleanSlateHook` 位于另一处，不覆盖本次补丁。
- 服务端 `ScrollHandler` 已为普通混沌与正向混沌跳过一般装备类别匹配，`ItemInformationProvider.scrollEquipWithId` 已处理其成功率、剩余次数及随机属性。本次只修客户端类别兼容，不更改服务端 Java、WZ/XML、IMG、现金标记或原始属性，也不要求服务端重启。
- 安装结果通过既有 `CrashReporter` 记录单次 `pet.scroll` 事件，不增加逐包或逐帧探针。

## 自动验证

执行 `tests/PetEquipScrollCompatibilityTest.ps1`：在隔离测试进程中映射真实 EXE，只运行上述无外部依赖的判定函数，不执行游戏入口。先复现六种组合全部被拒绝，再安装生产补丁并对比所有 204 类卷轴（10,000 个 ID）与 85 个装备/对照 ID，共 850,000 组，只有上表六组从拒绝变为允许。覆盖相邻装备、其他宠物装备、普通装备类别、无效非装备 ID、原生白衣和普通宠物卷轴，以及签名不匹配拒绝写入、空模块和重复安装。

`ezorsia.sln /p:Configuration=Release /p:Platform=x86` 构建通过；构建自带的 `LauncherParentPolicyTest` 通过。现有 `codecaves.h` 窄化、`EvanKillingWing.h` 内联 SEH 及 Detours PDB 警告仍在，无构建错误。`git diff --check` 通过。

## 本地交付

为保留工作区其他尚未提交的崩溃修复，构建使用 `BeiDou` 分支已提交基线 `7e893ca87afefcab56cff34155ee0ce9968e6ce8` 的独立快照，仅合入本次源码与工程登记。其他未提交源码和原构建目录保留。

- 构建快照及输出：workspace `.tmp/pet-chaos-20261012/build-source/`、其中 `out/Release/ijl15.dll`。
- 构建日志和源码指纹：`.tmp/pet-chaos-20261012/build.log`、`build-manifest.json`。
- 2026-10-12 13:25（北京时间）确认客户端关闭后，备份并替换 `BeiDou-Client/ijl15.dll`，772608 bytes，构建与安装 SHA-256 均为 `F62E2F4E6CF1A199CE5BEB961B0F0CE621E150E39B8441BC26A291B0CC3B610A`。
- 旧 DLL SHA-256：`98BC57661935C640981C7D3726DDB449728B779056B656514BD8FBF567DEAC61`。
- 交付文件、匹配 PDB、备份及清单：workspace `exports/pet-chaos-20261012/`；本地回退需关闭客户端，将其 `backup/ijl15.dll` 复制回客户端并核对旧哈希。
- 初次本地交付时未提交、推送或远端发布；后续按用户要求提交源码与客户端 DLL，生产发布另行执行。

## 用户实机验收（尚未执行）

重启并确认加载本次 DLL，在每件装备仍有升级次数时分别使用 2049100、2049115，验证普通拖放及砸卷界面能够发起请求，并按服务器结果显示成功/失败、属性和次数。正向混沌只改变装备当前已有的正数属性；三件装备初始为四维各 1。零次数装备应继续被拒绝且不消耗卷轴。自动测试确认类别判定修复，不替代完整游戏内操作验收。
