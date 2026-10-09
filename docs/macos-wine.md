# macOS / Wine 11 兼容改造

日期：2026-10-09。源码基线：`BeiDou` 分支 `7cf16cc`。参考 [上游 PR #25](https://github.com/BeiDouMS/BeiDou-ijl15/pull/25)，参考版本 `05a38333e77c05dcb7bf112f4e7602a790054449`。

## 本轮最终状态

本机后续切换 Wine 11.18，关闭异步渲染线程后用户反馈运行恢复正常；100% 显示下的局部字体修正得到“这次好像对了”的反馈。原生 Windows 自动化数据路径测试通过，完整 Windows 游戏和长期稳定性尚未验收。最终字体范围及验证见 [macos-small-ui-fonts-20261009.md](macos-small-ui-fonts-20261009.md)，崩溃证据及规避边界见 [macos-attack-exit-20261009.md](macos-attack-exit-20261009.md)。下文早期版本、哈希和实验记录保留作历史，不代表当前推荐运行时。

## 实现边界

- `WineCompatibility` 通过 `ntdll!wine_get_version` 识别 Wine。原生 Windows 不安装新增编码或网卡 hook。
- Wine 的系统代码页不是 936 时，把默认 ANSI/OEM/线程代码页映射为 GBK，修正双字节前导字节和默认字体字符集。显式 UTF-8 等代码页与显式字体字符集保持原样。相关 hook 以一个 Detours 事务安装，失败会终止初始化并记录生命周期日志。
- 2026-10-09 字体修正：Wine 下字体 hook 无论 ACP 是否为 936 都安装，将非符号字体的显式禁用抗锯齿请求改为灰度抗锯齿；原字号、字重和颜色不变。证据、对比图和待同步状态见 [macos-font-antialias-20261009.md](macos-font-antialias-20261009.md)。
- 后续运行时取证修正：Windows 字体模式保留原质量；默认字符集仅在原 ACP 非 936 时映射，避免中文环境的 Arial 被错误选成 Arial Unicode MS。真实游戏样本与验证见 [macos-runtime-font-diagnostics-20261009.md](macos-runtime-font-diagnostics-20261009.md)。
- 原客户端在 `GetAdaptersInfo` 失败后仍遍历栈缓冲区。补丁仅在失败且调用方空间足够时提供合法的单节点以太网适配器；成功结果、空缓冲区尺寸查询和不足一条记录的调用保持原结果。地址由 Wine C 盘卷序列号与计算机名确定，为本地管理单播地址；它是兼容标识，不是真实网卡身份，复制相同环境也可能复制此标识。
- 复用既有 `D3D8DisplayModeHook` 的分辨率规则。在 Wine 下替换 COM vtable 槽位，避免旧 Detours 依赖 Wine 方法入口指令布局；原生 Windows 沿用原拦截路径。不引入第二套 `ModeTableFix`。
- 不引入 PR 中的音频流池。先在固定的 Wine 11 构建上实测跳跃/技能音效，出现卡顿再定位。
- 初始化仍在进程入口完成，不回到 `DllMain` 内加载 DLL。保留启动器授权、设置共享与 EXE 校验。

本轮是平台兼容工作，不改变技能、WZ/XML 或客户端 IMG，也不需要服务端业务改动。

## 构建和自动验证

按仓库规则使用 Windows Visual Studio / MSBuild 构建 `ezorsia.sln`，配置 `Release|x86`。macOS 本机 Windows 11 虚拟机只有 v145 时可在命令行显式传 `/p:PlatformToolset=v145`，不修改项目默认 v142。产物仍为 `out/Release/ijl15.dll`，同步前确认客户端关闭，同步后核对 SHA-256。

Parallels 的 `\\Mac\Home` 共享路径在 PowerShell 中可能解析成带 provider 前缀的路径，传给 .NET 文件 API 时使用 `Resolve-Path` 的 `ProviderPath`。启动器的 Windows manifest 工具不接受该共享目录的扩展 UNC 形式，需要先映射盘符再构建。

`tests/WineCompatibilityTest.ps1` 构建 x86 静态运行库测试程序，覆盖 GBK/UTF-8、字体字符集、网卡缓冲区边界和错误返回。在 Windows 上运行数据路径；在 Wine 上额外安装真实 hook，创建 D3D8 接口并验证目标分辨率可枚举。测试程序不启动或登录游戏。

```powershell
.\tests\WineCompatibilityTest.ps1
```

Mac 使用同一 Wine 和独立测试 prefix 运行生成的 `out/wine-tests/WineCompatibilityTest.exe`。实际启动入口、运行时准备、开发启动器和用户验收清单见启动器仓库 `macos/README.md`。

## 实测边界

2026-10-09 本机验证记录：

- Apple M4 / macOS 26.2，Gcenx Wine 11.0_1（`wine --version` 输出 11.0）。
- VS 2026 / v145 14.51.36231 的 `Release|x86` 构建通过，构建附带的父进程路径及 DLL 内启动器文件名检查通过。
- Windows x86 的 GBK/UTF-8、字体、网卡 ABI/错误分支测试通过；Wine ACP=936 和 ACP=1252 两种环境中的实际 hook 安装、网卡调用和 D3D8 模式枚举均通过。
- Mac 启动入口 5 项测试通过；生成的 plist 与 shell 语法校验通过，`--check` 确认独立环境已初始化。
- 开发启动器 2.0.30.0 与正式启动器 2.0.29.0 共存；已验签的当前发布清单最低版本为 2.0.29。
- 新 DLL 已同步到客户端，两处 SHA-256 均为 `33ee27c20110dea4288d792162aea14811572b54039fd9bfa77ef40446c7449c`。本地备份与完整交付记录位于 workspace `exports/macos/`。

编译和独立 API 测试不等于客户端已能正常游玩。完整的启动器授权、选角色、地图、中文输入、音效及长时间游玩仍需要用户实机验收。交付时记录实际构建工具集、DLL 哈希与测试结果，不能把未执行的客户端测试写成通过。
