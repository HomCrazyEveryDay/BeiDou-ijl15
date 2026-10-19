# 背包主宠物标识（2026-10-16）

## 行为与来源

现金背包中的主宠物显示原版金色边框和小皇冠。依据服务端 CharacterStat 的第一只宠物唯一编号判断，不按物品 ID、名字或背包位置判断；两只同款宠物只标当前主宠物。召唤、收回、切换主宠物时，已打开的背包会刷新。大小背包、翻页和物品移动沿用原生格子坐标。

本轮直接核对了以下本地原始资源，使用 workspace BeiDouWzTool / MapleLib 只读解析：

- CMS186：`D:\maplestory-resources\cms186-client-wz\UI.wz` 的 `UIWindow.img/Item/bossPetIcon`。
- CMS228.2：`D:\张宝鸿\冒险岛online228\mxd\Data` 的同一路径；安装根 `LocalVersion3.xml` 标记 V228.2。展开 `_outlink=UI/_Canvas/UIWindow.img/Item/bossPetIcon` 的真实像素后反读。
- 当前客户端：`BeiDou-Client/Data/UI/UIWindow.img/Item/bossPetIcon`。

三者导出的 PNG SHA-256 均为 `AEE66277573DB416FF544B6247215FF0AA43021195B7718B86E00CE01342F5FE`，尺寸 34×38、origin=(2,2)，34 像素方框上方额外 4 像素为皇冠。复用当前已有完整画布，不改 IMG/XML。来源、提取图和 CMS228 像素反读报告存放在 workspace `exports/pet-main-mark-20261016/`。

## 083 实现证据

- EXE 时间戳 `4B7C15C9`、SizeOfImage `A94000`；StringPool `0x73A` 已含资源路径，但当前 CUIItem::Draw 未绘制该标识。
- `004E3006` 起，CharacterStat::DecodeChange 将掩码 `0x8` 对应的第一宠物 64 位 SN 写入 CharacterData+`0x1B`，其他两只分别为 +`0x23` / +`0x2B`。现有服务端登录和 petStatUpdate 均发送这些值，无需扩展协议。
- `004E36C5` 起，GW_ItemSlotBase::Decode 将 cash SN 的 8 字节写入 item+`0x18`；宠物使用 pet unique ID。
- 在 `0081DEEE` 的单个库存绘制调用点增加标识，保留寄存器、标志及原指令。只处理现金栏、GetType()==3、非零且完整 64 位 SN 相等的宠物。使用当前帧已有 CharacterData、物品和 Canvas，不保留对象引用。
- 在 `0081E2C1` 的 CWnd::Update 调用点保留原调用，并仅在第一宠物 SN 改变时使窗口重绘。重新打开背包依旧走原生绘制。
- 原图通过真实 ResMan 加载，读取 origin/尺寸，方框对齐物品格，皇冠伸出方框上方。使用 VT_I4/255 透明混合，临时 COM 引用正常释放；资源不可用时略过标识，诊断最多四条。
- 两处写入前统一验证 EXE 身份、绘制参数和 SN 解码指令签名；任一不符不安装，不修改 EXE 磁盘文件。

## 验证与交付边界

`tests/MainPetInventoryMarkTest.ps1` 已通过：隔离映射真实 EXE，执行安装后的绘制／Update 跳转，验证寄存器和栈、签名拒绝、同款不同实例、64 位高位不匹配、其他背包页、展开坐标、主宠切换／全部收回、资源缺失和 COM 引用平衡。另使用实际 PCOM/ResMan/Canvas 组件读取当前 IMG，检查皇冠像素、位置和透明区域不覆盖原图，连续绘制 100 次。未启动游戏、登录或操作现有客户端。

Release|x86 提交前构建通过，只有既有 detours.pdb 缺失警告。最终产物 `out/Release/ijl15.dll` 为 832,512 字节，SHA-256 为 `2F0F808ADAD3E85789211D5D5D5DFAE89DFE4A2D9E6566191F04CD8789BEFBD2`。

用户确认关闭客户端后，已再次核实无 BeiDou.exe 进程，将上述 Release|x86 产物同步到 `BeiDou-Client/ijl15.dll`，构建与交付文件 SHA-256 完全一致。原有 DLL 为 829,440 字节、SHA-256 `FDCFCA2852AB753D37EB094F7FA00E203498347FBB6F86D9BFDA1B8481DBFB37`，已备份至 workspace `exports/pet-main-mark-20261016/backup/ijl15.FDCFCA2852AB.dll`。该目录上级的 `delivery.json` 保留首次同步记录；提交前再次构建并同步的记录为 `commit-delivery.json`，同时备份了首次交付版本 `backup/ijl15.A4E8FE7E0E53.dll`。客户端实机验收仍待用户执行。

用户实机验收：重新启动客户端，召唤两至三只宠物，确认现金背包仅主宠物显示皇冠；收回主宠物、重新召唤／设置另一只主宠物、全部收回、翻页、展开背包和移动宠物格子，确认标识跟随当前主宠物且不残留。同款宠物也检查一次。

本轮服务端另有商城食品优先喂最饿且可食用宠物的改动，见服务端 `docs/records/cash-pet-food-20261016.md`；标识判断与饱食度无关。
