# ijl15 Repo Notes

- 先读取 workspace 总规则：`..\AGENTS.md`。
- 本仓库生成客户端加载用的 `ijl15.dll`；只在确认问题属于 DLL hook 或硬编码逻辑时修改。
- 构建 DLL 使用 `Release|x86`，方案文件 `ezorsia.sln`，输出应为 `out\Release\ijl15.dll`。
- WZ/IMG 资源同步不要放进 ijl15 仓库；共享工具从本仓库根目录使用 `..\tools\<ToolName>`。
- `config.ini` 通常是本地配置，不应混入提交或 DLL 构建基准，除非任务明确要求。
