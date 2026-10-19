# 游戏实际字体家族与 Windows 回退

用户在安装宋体配置后仍报告字体观感奇怪。上轮只验证直接请求 SimSun，不能覆盖游戏实际使用的其他字体；整幅画面的缩放也不能作为所有差异的解释。

## 新证据

对本机 `BeiDou.exe` 字符串池做只读解码：表位于 `00BDC9D4`，16 字节密钥位于 `00B001EC`，解码算法核对原函数 `0079ECDE` / `0079EBF3`。原生字符串 8 为 Tahoma、9 为 Arial Narrow、5527 为 Arial。没有修改 EXE 或字符串池。

使用相同 Windows GDI 调用读取实际字体名与 OpenType name 表：本机 Windows 11 对 Arial Narrow 请求回退到宋体，name 表 FNV1a=`7493d6bb`；Wine 则从 macOS 的 `/System/Library/Fonts/Supplemental/Arial Narrow.ttf` 找到真实同名字体，name 表=`f76d5386`。两者中文渲染链不同。默认 Tahoma 的 name 表也不同：Windows 为 `624ea412`，原 Wine 为 `dc48de13`。

这只是本机核实的回退行为，不表示所有 Windows 都缺少 Arial Narrow。此本地兼容配置目标是匹配用户提供的原版观感，不能推广为任意 Windows 字体的通用替代策略。

## 变更

- Windows 字体模式增加本机 Windows 的 Tahoma 普通/粗体，以及 Arial 普通/粗体/斜体/粗斜体；不提交字体文件，不打包分发。
- Wine 且 `BEIDOU_WINE_FONT_STYLE=original` 时，CreateFontIndirectA/W 将显式 Arial Narrow 家族映射到已验证的 SimSun。其他字体名保持原样；符号字体不改。字号、字重、颜色、原请求和稳定渲染设置不改。
- 不依赖 Wine Fonts/Replacements 覆盖已存在的同名字体：独立测试证明存在 macOS Arial Narrow 时，该条目没有替换实际选择，因而采用明确的进程内家族映射。探索用注册表条目只在临时测试 prefix，没有写入用户运行 prefix。
- `WineFontCanvasTest` 增加可选字体家族参数，用真实 PCOM/Canvas 对比 Tahoma、Arial、Arial Narrow，避免继续只测 SimSun。

## 验证及交付

Arial Narrow 测试由与 Windows 的 9111 个不同像素减少为 5 个非文字区像素，文字区相同。Tahoma 改用同一原字体文件后 name 表一致，但独立渲染仍有约 500 个差异像素，不能宣称所有字体像素完全一致。字体比较图位于 workspace `exports/macos/visual-check-20261009/actual-fallback-comparison.png`：上为 Windows，中为修改前 Wine，下为修正回退后的 Wine。

`Release|x86` 构建、父进程授权回归、Windows/Wine 字体及编码回归、实际 Canvas 渲染和 13 项 Mac 入口测试通过。同步前确认游戏、启动器和退出监视器均已退出且 DLL 未占用；备份后同步到客户端，两处 SHA-256 均为 `020169fdd02d65f1c775eb67e2366a278c3ad44f48af3878a4f6cde212006714`。备份和时间见同目录 `validation.json`。

不自动启动客户端。下一次仍由用户使用 Wine 11.18 对照入口，验证角色名、聊天及界面文字；该交付不代表已消除与 Windows 的显示缩放差异。未提交、未推送。
