# BeiDou-ijl15

将主分支的两次Release视为v1和v2。

由于v2版本在部分机器上会导致游戏进入到登录界面的时候崩溃，而v1没有这个问题，因此本分支继承主分支的v1版本，并不再和主分支同步，单独开发。

本分支主要服务于BeiDou的客户端。

文档索引见 [docs/README.md](docs/README.md)（崩溃排查记录在 `docs/records/`）；构建、同步与交付规则见 workspace 根 `../AGENTS.md` 的 ijl15 Rules。

## 使用方法

- 方案 `ezorsia.sln`，配置 `Release|x86`，平台工具集以 `ezorsia/*.vcxproj` 为准（当前 v142；本机缺少时按根 AGENTS.md 定位已安装的 MSBuild 与工具集）。
- 产物 `out/Release/ijl15.dll`；交付时同步到 `BeiDou-Client/ijl15.dll` 并核对 SHA-256，原版 DLL 以 `2ijl15.dll` 名称保留在客户端。
- 连接线路与诊断开关由启动器下发（见 [endpoint-override.md](docs/endpoint-override.md)）；`ezorsia/config.ini` 只是本地开发示例，不提交个人配置。

## 推荐服务端

北斗 https://github.com/SleepNap/BeiDou

## 更新记录（早期，相比主分支 v1；后续变更见 docs 与 Git 历史）

相比主分支 v1

- 支持中文输入/角色中文名(修复卡门问题)
- 修复滚轮乱飞的问题
- 修复ToolTip超出游戏窗口的问题
- 支持长键盘快捷键
- 交易中心居中
- 增加中文汉化
- 增加魔攻/魔防/命中/回避/跳跃 上限突破
- 增加BossHp百分比显示（在Boss血条的头像下方）
- 免密模式（须服务端支持，客户端仅解除密码限制）
- 调整聊天框文字位置

针对中文环境的一些调整：

1. 装备tooltip字体大小
2. 道具有效期字体大小
3. 修复有效期日期顺序
4. 聊天栏的选项汉化了远征队
5. 在不修改wz的情况下解决了Eqp，Etc汉化后游戏崩溃的问题，Use汉化后吃药没声音的问题