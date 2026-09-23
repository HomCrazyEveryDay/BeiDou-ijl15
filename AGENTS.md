# ijl15 Repo Notes

- 先读取 workspace 总规则：macOS/Linux `../AGENTS.md`；Windows `..\AGENTS.md`。
- 本仓库生成客户端加载用的 `ijl15.dll`；只在确认问题属于 DLL hook 或硬编码逻辑时修改。
- 构建 DLL 使用 `Release|x86`，方案文件 `ezorsia.sln`；构建成功后输出路径 macOS/Linux 写作 `out/Release/ijl15.dll`，Windows 写作 `out\Release\ijl15.dll`。Windows 不要直接调用未必在 PATH 中的 `msbuild`；优先使用已安装 Visual Studio 的完整路径（例如 `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`），或先通过 `vswhere.exe` 定位 `MSBuild.exe`。
- WZ/IMG 资源同步不要放进 ijl15 仓库；共享工具从本仓库根目录访问：macOS/Linux 使用 `../tools/<ToolName>`，Windows 使用 `..\tools\<ToolName>`。
- `config.ini` 通常是本地配置，不应混入提交或 DLL 构建基准，除非任务明确要求。
