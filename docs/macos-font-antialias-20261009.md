# macOS 游戏文字灰度抗锯齿

2026-10-09，用户确认关闭 Wine 异步渲染线程后暂未复现卡死，但聊天及角色名文字仍有明显锯齿，并提供两张截图。此轮保留 `csmt=0`、分辨率、字号和 IMG，只处理 Wine 下明确禁用抗锯齿的文本。

## 定位

通过客户端原版 `PCOM.dll` 创建 `Canvas#Font`，没有启动游戏入口。字体 IID 为 `2bef046d-ccd6-445a-88c4-929fc35d30ac`，`Create` 为 vtable `0x0c`，`DrawText` 为 `0x30`。

本地 `Canvas.dll` 反汇编显示字体样式解析函数 `500045C5` 将 `n` 设为 `0x08`、`a` 设为 `0x10`；`50004F6B` 构造 LOGFONTA，`50004FFE` 将 n 样式的 lfQuality 写成 3（NONANTIALIASED_QUALITY），`50005006` 将 a 样式写成 4（ANTIALIASED_QUALITY）。字体组件支持灰度边缘，单纯替换字体名称不能覆盖显式 n 样式。

独立测试在相同 Wine 11.18 prefix、苹方替代映射下用原版 Canvas 字体组件绘制 6 行、12/14 像素、普通/加粗、中英文与数字：未安装补丁的 n/bn 样式输出 5452 个不透明像素、0 个半透明像素，重现截图中的硬边小字。

## 变更

`WineCompatibility.cpp` 的 CreateFontIndirectA/W hook 对非 SYMBOL_CHARSET、显式 NONANTIALIASED_QUALITY 请求改为 ANTIALIASED_QUALITY，使用灰度覆盖率。字体名字、高度、字重、倾斜、颜色及字符定位由原逻辑处理；已有抗锯齿请求不改。传入 LOGFONT 先复制，不写调用方内存。

字体 hook 改为在 Wine 下总是安装，不再被 `GetACP()==936` 跳过。代码页转换 hook 仍只在非 936 时安装；原生 Windows 仍不安装整个 Wine 兼容模块。没有修改 Canvas.dll 本身、字体资源或 WZ/IMG。

## 验证与产物

- VS 2026 v145，`Release|x86` 构建成功；父进程授权回归通过。
- `WineCompatibilityTest` 增加字号/字重/符号字体/显式平滑保持检查，以及真实 W hook 检查。Windows 和 Wine ACP936/1252 验证通过。
- `WineFontCanvasTest` 调用真实 PCOM/Canvas 和生产 hook，安装前后各用独立进程，避免字体缓存混用。补丁后 8194 个半透明像素、222 个全不透明像素，各行高度仍为 12 或 14。
- workspace `exports/macos/font-antialias-20261009/` 包含 `baseline.png`、`patched.png`、原尺寸上下对比 `comparison.png`，以及对应 PPM。图片来自真实客户端字体组件，未使用插值放大。
- 产物：`out/Release/ijl15.dll`，SHA-256 `860951af9bf77a2814ef948c97a6411d35e41bfbb5ceb8548ecf349aad5fbb2b`。

## 交付状态

用户确认关闭后，已检查无游戏、启动器或退出监视器进程，备份并同步新 DLL 到 `BeiDou-Client/ijl15.dll`，与构建产物 SHA-256 一致：`860951af9bf77a2814ef948c97a6411d35e41bfbb5ceb8548ecf349aad5fbb2b`。

旧 DLL 的 SHA-256 为 `33ee27c20110dea4288d792162aea14811572b54039fd9bfa77ef40446c7449c`，保存在 workspace `exports/macos/font-antialias-20261009/backup-*/ijl15.dll`，精确路径与时间记录在同目录 `validation.json`。未自动启动客户端；由用户检查聊天、角色名、加粗标签、混染和商店文本。

该变更改善硬边字形，不增加 UI 的物理像素数量。固定在 IMG 中的图片文字和低分辨率贴图不受影响；独立 Canvas 验证不能代替游戏内布局与观感验收。未提交、未推送。
