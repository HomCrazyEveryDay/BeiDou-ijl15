# 宠物水下动作 UOL 兼容修复（2026-10-11）

## 故障与证据

用户提供 `ZhuMeng-diagnostics-20261011-003446.zip`。通过三个 clientRunId/connectionId 对照生产只读日志，确认角色「勇气」在频道 3、1、2 进入 `230040400`「受难船的墓地」（`swim=1`）时出现同类退出。

客户端时间 UTC+8：00:31:27.480、00:32:17.951、00:33:13.969。客户端与服务器墙钟约差 30 秒，关联以运行/连接 ID 为准。三次均先记录死神兔 `5002416` 的 `action=3`（jump）有一帧，随后 `action=4`（fly）为零帧；原生主循环捕获 `CTerminateException(0x22000006)`。服务端记录 `transport_closed_without_recorded_server_request`。不根据这些记录推断宠物一定属于该角色，也可能是同屏宠物。

本地 orange-wz-cli 反读及 MapleLib 交叉读取确认，三只现代宠物（托特 5002414、贝拉 5002415、死神兔 5002416）IMG/XML 均已有 `fly -> jump` UOL，目标动作有完整画布。无需补传或改写本地资源。玩家诊断包不含 IMG，未直接核对玩家磁盘的 IMG 哈希。

隔离进程加载现有 PCOM/ResMan 证实：`IWzProperty::get_item("fly")` 返回 `IWzUOL`，直接请求 `IWzProperty` 得到 `E_NOINTERFACE`；`ResMan::GetObject("Item/Pet/5002416.img/fly")` 正确解析引用并得到一帧。普通宠物 5000042 的 fly 原本为普通属性目录，不受影响。

083 `LoadPetAction` 在 `0x0040E2FD` 调用 `0x004052AD` 直接赋值 `IWzProperty`，没有解析动作 UOL。返回空帧后，`CPet` 在 `0x00703DFB` 检查列表长度，并于 `0x00703E17` 抛出 `0x22000006`；与三份 dump 的堆栈一致。

## 修改

`ezorsia/ModernEquipmentCompatibility.h` 只修改宠物动作加载器的这一处赋值调用：

- 先执行原生接口赋值，普通动作和其他 HRESULT 保持原逻辑。
- 仅在 `E_NOINTERFACE`、500 类宠物且输入确为 UOL 时，使用原始宠物 ID 和原始动作名组成完整资源路径，让现有 ResMan 解析引用。
- 使用引用目标的真实动作目录；保留动作编号、缓存键、全部帧、坐标、延时及原有宠物装备合成流程，没有把 fly 硬编码换成 jump，也没有修改 IMG。
- 无法解析时保留原失败，未吞掉崩溃或填充假帧。成功结果持有一个 COM 引用，旧目标由原生赋值释放；保留 GetLastError。
- 安装检查原始调用字节并恢复页面保护；刷指令缓存失败时恢复原调用。`equipment.compat` 增加 `petActionUol` 安装状态，`equipment.pet-action-uol` 每进程最多记录 24 次解析结果。

服务端逻辑、WZ/XML、客户端 IMG 均无需变动，未操作生产数据库或部署服务器。

## 验证与交付

`tests/ModernEquipmentCompatibilityTest.ps1` 通过。新增回归使用真实 IMG 和 PCOM，执行 EXE 中的原生接口赋值函数及新 x86 桥接，覆盖：

- 重现三只现代宠物的原始 UOL 读取失败，并验证新桥接取得真实帧与可解码像素。
- 普通宠物、jump 原动作、重复赋值/释放、GetLastError 保留。
- 解析器不可用、错误物品类别、缺失动作和空源保持失败。
- 调用点签名、修改权限失败时不写入、重复安装拒绝、页面保护恢复。

既有图标、脸饰、宠物装备匹配/绘制诊断、名牌原生组件回归一并通过。测试没有启动游戏或建立游戏服务器连接。

`ezorsia.sln` 的 `Release|x86` 构建成功，保留既有 detours.pdb 缺失警告。确认 BeiDou.exe 关闭后，已备份并同步到本地 `BeiDou-Client/ijl15.dll`；构建产物与安装文件 SHA-256 一致：

`B5C44AC3D93AF19B95C4EDA48A62461A3F79BA50CD2021ACD904FB0D1764550B`

DLL 为 772,096 字节。备份和安装清单位于 workspace `exports/pet-action-uol-20261011/`。本次不包含线上客户端发布。

尚待用户实机验证：携带三只现代宠物分别进入水下地图，覆盖进入地图、移动/跳跃、重新登录以及同屏其他角色宠物；重点确认死神兔在 `230040400` 重新登录不再触发退出。当前结果是原生资源组件验证通过，不代表已完成实际游戏验收。
