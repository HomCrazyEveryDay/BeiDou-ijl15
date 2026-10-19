# 龙神梦境前景宽屏适配

原资源 `Map/Back/dragonDream.img/ani/6..8` 是 800×600 的透明梦境前景。宽屏截图显示原尺寸居中白框；不修改原版资源或地图坐标。

已核对本地 BeiDou.exe：`CMapLoadable::LoadBack` 0x0063CD4E；局部 -0x38 是读取 bS 的 `_bstr_t`，-0x3C 是宽字符串 `ani/%d`。0x0063D9D1 调用 0x0043EA3E 构建动画层。仅当 bS=dragonDream、ani=6/7/8 时开启当前线程的缩放上下文，函数退出（含异常展开）清除。其他背景不处理。

0x0043F768 在读取原 canvas 的 delay/a0/a1/zoom0/zoom1 后，通过 0x00426BAB 调用 IWzGr2DLayer::InsertCanvas。拦截该包装函数只替换此时传入的 canvas，原动画参数直接传递。两个入口校验原机器码，不匹配则不安装。原资源保持不变，临时副本插入后释放本地引用。

通过本地 PCOM.dll 创建独立 Canvas 对象并反汇编 Canvas.dll 验证 ABI：Create=0x2C，width=0x40，height=0x48，cx=0x6C/0x70，cy=0x74/0x78，CopyEx=0x84，GetPixel=0x88。CopyEx 的最后 VARIANT 必须是 VT_EMPTY，不能传 VT_I4(0)。使用当前分辨率创建 BGRA8888 canvas，复制完整 800×600 像素并按相同比例转换原点。只处理确认尺寸，失败返回原画布。

`tests/DreamCanvasTest.ps1` 直接加载本地绘图库，不启动游戏。验证 1024×768、1280×720、1920×1080 的尺寸、中心原点、不透明边框、透明中心、半透明像素保持、源画布不变及 800×600 bypass。

Release|x86 构建通过并已同步本地客户端；保留已有构建警告。安装位置与断点字节经静态核对，最终游戏内加载链和视觉效果仍需用户实测；没有代替实机验证的声明。

## 2026-10-16：阿弗利埃回忆黑框

用户反馈任务 22591「过去，玛瑙龙和黑魔法师」的 900030000 回忆画面出现原尺寸黑框。地图 back/40..42 引用 `dragonRoad/ani/17..19`，共 12 帧均为 800×600、原点 (400,300)，使用原始 alpha/delay。用 MapleLib 从当前客户端 IMG 导出 ani/17 的 PNG，确认其为截图中的黑色渐变边框；该路径此前未列入缩放选择器。

复用同一 LoadBack → InsertCanvas 通路，仅增加 `dragonRoad` 的 `ani/17`、`ani/18`、`ani/19` 精确匹配。原始像素文件、动画参数、地图坐标不变。巨龙、场景、back/41 和 ani/13..16 等非全屏画面保持原有绘制。原生 800×600 不创建缩放副本。

本地 BeiDou.exe 的 0x0063D9D1 调用字节仍为 `E8 68 10 E0 FF`。测试直接加载 PCOM/Canvas，验证两种背景路径隔离，以及 1024×768、1280×720、1366×768、1920×1080、2560×1440 的尺寸、中心、透明度和源画布保留。孵化动画的五种分辨率回归也通过。Release|x86 构建成功，只有已有 detours PDB 缺失警告。

确认本地 BeiDou.exe 已关闭后，已将产物同步到 `BeiDou-Client/ijl15.dll`；两者 SHA-256 均为 `2D5B0F478D20DC63751A378EFC322368C7CF4F6393861D1D9EFD1A3C9F032E01`。尚未进行游戏内视觉验收。

同次「点击阿弗利埃无反应」为服务端 NPC 1013205 缺少数字脚本入口，需服务端配套修复，见服务端 `docs/records/evan-memory-entry-20261016.md`；更换 DLL 不会修复服务端脚本入口。
