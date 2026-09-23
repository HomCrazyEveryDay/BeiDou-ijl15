# 正式与开发启动器共存

客户端根目录可同时放置 `ZhuMengLauncher.exe` 和 `ZhuMengLauncher-Dev.exe`。
`LauncherGate.cpp` 使用 `LauncherParentPolicy.h` 校验父进程完整路径，只允许游戏所在目录内的这两个名称（Windows 大小写不敏感）。token、permit、设置共享内存和 acknowledgement 校验仍全部执行。

`Release|x86` 构建自动运行 `tests/LauncherParentPolicyTest.ps1`：覆盖两个入口、大小写、中文目录、错误目录及相似文件名，并检查生成 DLL 包含两个入口名称。失败会使构建失败。

2026-09-23 的授权日志显示，09:51 开发版授权成功，10:14 加载另一版 DLL 后在 parent-validation 阶段被拒绝（161）。因此交付必须核对客户端 DLL 与本轮构建产物 SHA-256 相同，不能只保留源码修复。

开发版 EXE 仍由发布器精确排除，并在客户端本地 Git exclude 中忽略；支持两个入口的 DLL 可随正常 DLL 交付。正式启动器的更新逻辑不变。

此构建检查无法约束其他旧分支或已发布旧 DLL。兼容源码、测试及构建配置须一起纳入后续提交；正式更新渠道也发布兼容 DLL 后，才能避免更新又装回旧版本。提交和部署分别需要用户授权。

实机验证：依次用正式入口和开发入口启动，确认授权成功；本地开发使用 127.0.0.1:8484。默认由用户执行，不自动启动客户端。
