# 衣柜入口再次消失：本机 DLL 版本混用（2026-10-18）

用户反馈重新编译后 F8 无法呼出衣柜。本轮未修改键位或衣柜实现；根因是客户端运行目录使用了没有衣柜功能的主分支 DLL。

## 证据

- 最近两次本机运行分别为北京时间 18:19、18:25；生命周期日志与异常报告均记录 `ijl15Timestamp=0x6AD49ACA`，加载路径为当前 workspace 的 `BeiDou-Client/ijl15.dll`。
- 排查时该文件为 890,368 字节，SHA-256 为 `B8A5C7730A5F194EEF20F8FE71B6590D1695EC77D34D8A9A63253C35201A704E`，PE 时间戳与实际运行日志一致。二进制不含 `wardrobe.install`、`wardrobe.key`、`ready keyAction=40`。
- 其哈希与 `.tmp/ime-submit-20261018/BeiDou-ijl15/out/Release/ijl15.dll` 完全一致。该隔离 checkout 是 `BeiDou` 主分支，当前提交为 `ed19914`，初始化和项目文件均无 Wardrobe 模块。文件一致能确认构建来源，不能据此认定具体哪个进程执行了覆盖。
- 当前工作区在 `codex/wardrobe-083`，源码保留衣柜初始化、场景和 F8 路由。刚完成的完整 Release|x86 Rebuild 产物为 1,005,568 字节，PE 时间戳 `0x6AD49DA4`，包含上述衣柜入口标识。工程根目录副本与构建目录一致，但此前未同步到客户端运行目录。
- 本次读取的启动器日志对应本地开发启动，未记录下载或覆盖 DLL；没有证据将这次替换归因于启动器自动更新。
- `WardrobeKeybindTest.ps1` 通过，覆盖实际生产 RootKey、原生 manager+4 接收指针、F8、改绑、聊天/弹窗阻挡和键位初始化。该离线测试不能代替游戏实测。

## 本机修复与边界

确认无 `BeiDou.exe` 进程后，使用刚完成的当前工作区构建产物同步 `BeiDou-Client/ijl15.dll` 和工程根目录 `ijl15.dll`；三处 SHA-256 均为：

`EC6C49C6A4F9121DC8FCDC80A148B39230966069B87830812AC052A7B68F4611`

被替换文件保存在 workspace `exports/wardrobe-entry-delivery-20261018/ijl15-without-wardrobe-before.dll`。衣柜 IMG 已是紧凑版，SHA-256 `5DC3A04873B9D629D007C3F194FF5DFB5BBBBF9F2FED77FBC15A55505701ABF6`，无需重写资源。

当前工程含衣柜与其他尚未提交的修复；没有回退源码、改变主分支交付政策、提交或推送。后续本机验收衣柜时，需同时核对构建所属分支、实际运行目录 DLL 的功能标识与哈希。只检查编译成功或工程目录副本，不能保证游戏实际加载了衣柜版本。另行交付无衣柜的主分支 DLL 会使本机衣柜入口消失。

未自动启动或操作客户端；用户重新登录地图后按 F8 验收。
