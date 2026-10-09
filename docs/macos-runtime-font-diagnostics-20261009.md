# 字体改动未改善后的运行时取证

2026-10-09 用户再次反馈整体观感没有明显变化。最新会话 `20261009-044711246Z-pid400` 已记录 `fontAntialias=original`，客户端 DLL 为上一轮 `020169fd...`，排除未同步或仍使用旧模式。随后只检测到启动器进程，游戏已经退出，无法读取其字体缓存。

此前 SimSun 和 Arial Narrow 的离线渲染对比只验证指定请求，不能证明截图里的聊天、角色名实际使用了这些请求。此次不再改变字形、缩放、颜色或纹理参数，先获取运行时证据。

## 实现

`BEIDOU_WINE_FONT_TRACE=1` 显式开启采集，默认关闭。现有 CreateFontIndirectA/W hook 成功创建非符号字体后，按 LOGFONT 去重，最多记录 32 种。采集记录请求、有效及 GDI 实际名称、字号、字重、字符集、质量、实际字体 OpenType name 表哈希和调用地址。只对固定 U+4E2D 检查字形覆盖，不读取玩家文本。

不改变字体返回值、调用方 LOGFONT 或字形绘制；诊断内部保存恢复 GetLastError。不增加绘制时逐帧记录，不持续扫描内存。字体表读取上限 64 KiB、每次仅使用 512 字节栈缓冲。

启动器仓库 `macos/font-diagnostics.command` 沿用 11.18 对照环境和关闭异步渲染设置，只为本次启动开字体采集，覆盖掉通用异常诊断开关，避免日志混杂。

## 验证

- `Release|x86` 构建及自带启动器父进程测试通过。
- 使用真实 PCOM/Canvas 和生产 hook，6 行文字只生成 4 条去重字体记录，实际 nameHash=`7493D6BB`，字符集和调用地址符合预期；开启前后输出图片逐像素相同。
- Mac 入口 14 项、日志 4 项测试通过。
- 待用户实际进入游戏后读取 `wine_font_sample`。尚未获得本次真实字体样本，问题未修复，不能据构建或离线检查宣称画质恢复。

诊断产物 SHA-256：`d66fbe038868d77a3cb8d15979d1c81efd573d2a78dacaa5cb618af485af5363`。同步及备份记录位于 workspace `exports/macos/font-runtime-20261009/validation.json`。没有自动启动、登录或操作客户端；未提交、未推送。

## 12:58 实际游戏样本与修正

用户从诊断入口进入地图并保持游戏运行。会话 `20261009-045846226Z-pid400` 中获取 11 个实际请求：6 个 Arial，4 个 Tahoma，1 个未知字体名（按 CP936 解码为“蹈框”）；没有 Arial Narrow。6 个 Arial 均被匹配成 Arial Unicode MS，nameHash=`BD52E5AB`。所有请求都被现有 hook 从 DEFAULT_CHARSET=1 强制改为 GB2312_CHARSET=134，尽管该 prefix 的 ACP 已为 936。

这证明前面针对 Arial Narrow 的离线修复没有命中此次主要显示路径，也说明只比较 SimSun 样本不足以判定游戏字体相同。未知字体名暂未定位，不凭名称猜来源或一并替换。

修正为只在 `fixLocale`（原 ACP 非 936）时更改默认字体字符集。中文环境保留原值；现有 GBK 兼容、字体模式和 `csmt=0` 不变。这恢复了最初代码页修复的边界，移除后续始终安装字体 hook 时引入的副作用。

`WineFontCanvasTest` 新增 runtime 参数，回放真实记录的 Arial 11/12/14/15 像素、正常/粗体及质量 0/4。旧分支复现实际日志：Arial→Arial Unicode MS、charset 1→134。修正后 Arial→Arial、charset 1→1；普通/粗体的 nameHash 分别为 `5B91EC4D` / `F309D1B5`，与已安装的原 Windows Arial 文件对应。中文仍由字体链接处理，图片有文字而不是空白或方框。

真实参数回放与日志保存在 workspace `exports/macos/font-charset-20261009/`。前后图是离线原版 Canvas 输出，不能代替实际游戏观感。`Release|x86`、父进程授权检查、Windows 单元测试以及 Wine 11.18 ACP936/1252 回归通过；还没有在用户游戏中加载该修正。待关闭当前游戏后同步新 DLL，交付后停止检查，等待用户反馈。

用户确认关闭后，已核对游戏、启动器和退出监视器均退出且 DLL 无占用，备份后同步字符集修正版到客户端。源码产物与 `BeiDou-Client/ijl15.dll` 的 SHA-256 均为 `12cb283e08abde283b5206bb973da99b2d244f785be37d5ee314e22ebea8c949`。旧诊断版的备份路径和交付时间见上述目录的 `validation.json`。未自动启动游戏，实际字体记录及视觉效果待用户下一次验证。
