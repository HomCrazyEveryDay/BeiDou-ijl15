# 高编号发型染色预览崩溃排查记录

## 背景

客户端新增大量高编号发型后，射手村爱德华比克的随机发型正常，但娜塔丽高级染色预览会崩溃。最稳定的复现条件是：当前发型或染色预览列表第一项为 `40000+` 高编号发型时，客户端在打开预览窗口后崩溃。

服务端日志证明 NPC 发送的发型列表本身正常，例如：

```text
hair=40000
styles=[40001, 40002, 40003, 40004, 40005, 40006, 40007]
```

客户端收到列表后很快断线或崩溃，所以问题主要在客户端预览渲染路径，而不是 NPC 脚本或服务端封包格式。

## 关键现象

排查过程中验证过这些点：

- `327xx` 发型染色预览不崩溃。
- `40000` 出头发型染色预览崩溃。
- 把客户端 `63803.img` 内容改成和老发型 `30000` 完全一致，仍然崩溃。
- 在预览列表第一项临时塞一个低编号老发型可以绕过崩溃，但体验很差，不能作为正式方案。
- `00063803.img` 与相邻颜色组结构一致，不是单个 WZ 发型资源坏了。
- 服务端加 NPC sendStyle 日志只能证明发包正常，最终不需要保留。

结论：崩溃不是某个发型 `.img` 结构坏了，而是客户端对高编号发型的预览路径存在兼容问题。

## 根因判断

客户端高级染色预览会调用头像图层构建逻辑。对 `40000+` 发型，预览路径会走类似 item-preview 的逻辑，而不是普通角色自身发型渲染逻辑。

排查时观察到：预览 `40001` 时，客户端内部会尝试派生或加载 `42001`、`52001` 等编号。服务端没有发送这些编号，它们是客户端预览路径自己推导出来的。单纯补 WZ、补 String.wz、复制老发型内容，都不能解决这条路径本身的问题。

所以稳定方案是：高编号发型染色预览时，不让客户端走会派生编号的 item-preview 路径，而是把预览发型写入一份临时 avatar 数据，让客户端按“角色自身发型”方式构建图层。

## 最终修复

修复点在：

```text
ezorsia/AutoTypes.h
ezorsia/dllmain.cpp
ezorsia/ezorsia.vcxproj
```

新增 `AvatarLayerBuild_Hook`，检测到调用确实属于高编号发型预览时：

1. 从 `a4` 指向的当前 avatar 数据复制一份临时结构。
2. 把临时 avatar 的 hair 字段替换成要预览的发型 ID。
3. 调用原始 `AvatarLayerBuild` 时把 `a3` 改成 `0`，并传入临时 avatar 数据。

判断分两层：

```cpp
IsKnownHairId(itemid)
IsHighHairPreviewTarget(itemid)
```

`IsKnownHairId` 只表示资源类型是发型，覆盖旧发型和新增发型。`IsHighHairPreviewTarget` 才表示需要走高编号预览兼容逻辑，目前只覆盖 `40000-49999` 和 `60000-79999`，并排除已知脸型 ID。这样普通 `30000-39999` 发型不会误入预览 workaround。

`dllmain.cpp` 中启用 hook：

```cpp
HookAvatarLayerBuild(true);
```

`ezorsia.vcxproj` 需要链接 `Ws2_32.lib`，因为项目已有 Winsock 域名解析代码，否则重编译可能链接失败。

## 为什么不改服务端

服务端发出的 style ID 列表是正常的，客户端也能收到。崩溃发生在客户端预览渲染阶段。

尝试过或讨论过的服务端绕法包括：

- 在列表第一项塞低编号老发型。
- 改 NPC 脚本发送特殊顺序。
- 补 `3999x`、`42001`、`52001` 等虚拟 WZ/String 项。

这些都不适合作为正式方案：

- 会影响玩家看到的真实预览列表。
- 依赖客户端内部派生编号，不稳定。
- 后续新增更多高编号发型仍可能复发。

因此最终选择修客户端预览路径。

## WZ 读取工具

本工作区建议用两类工具。

### 1. `@tybys/wz` 读取 `.img`

适合做结构检查和只读验证，不适合写回。

临时目录：

```text
C:\Users\HomCrazy\Desktop\work\.tmp\WzProbe
```

安装方式：

```powershell
npm install @tybys/wz@1.7.1 --ignore-scripts
```

读取客户端 loose `.img` 的关键点：

```js
const { WzImage, WzMapleVersion } = await import('@tybys/wz')
const img = await WzImage.createFromFile(path, WzMapleVersion.GMS)
img.offset = 0
await img.parseImage(true)
```

经验记录：

- 客户端 `Data\Character\Hair\*.img` 用 `WzMapleVersion.GMS` 可以解析。
- `WzMapleVersion.BMS` 在这些客户端 loose `.img` 上解析不正确。
- 检查发型结构时，可以递归统计 `WzCanvasProperty`、`WzUOLProperty`、`WzVectorProperty`，收集所有 `/z` 字符串值，再比较 `path|type|value`。
- 已验证 `00040590.img` 到 `00040597.img` 结构一致，`00060520.img` 到 `00060527.img` 结构一致，`00063803.img` 与同组相邻颜色结构一致。

### 2. MapleLib / HaRepacker 写 `.img`

适合从 XML 生成 String 类 loose `.img`，例如 `String\GLcloneC.img`。

临时目录：

```text
C:\Users\HomCrazy\Desktop\work\.tmp\Harepacker
C:\Users\HomCrazy\Desktop\work\.tmp\GlclonePatchTool
```

来源：

```text
lastbattle/Harepacker-resurrected
```

需要初始化 submodule，因为 MapleLib 在仓库子目录里。

`GlclonePatchTool` 的思路：

```csharp
using MapleLib.WzLib;
using MapleLib.WzLib.Serializer;
using MapleLib.WzLib.Util;

var iv = WzTool.GetIvByMapleVersion(WzMapleVersion.GMS);
var deserializer = new WzXmlDeserializer(false, iv);
var objects = deserializer.ParseXML(inputXml);
var image = objects.OfType<WzImage>().First();

using var fs = File.Open(temp, FileMode.Create, FileAccess.Write, FileShare.None);
using var writer = new WzBinaryWriter(fs, iv);
image.SaveImage(writer, true);
```

这次用它把服务端 XML 版：

```text
C:\Users\HomCrazy\Desktop\work\myself_BeiDou_Server\gms-server\wz\String.wz\GLcloneC.img.xml
```

生成客户端文件：

```text
C:\Users\HomCrazy\Desktop\work\BeiDou-Client\Data\String\GLcloneC.img
```

生成后再用 `@tybys/wz` 读回确认职业名、Step4/Finish 描述已是中文。

## 关于 UI WZ 图片写回的坑

这次还尝试过修改：

```text
C:\Users\HomCrazy\Desktop\work\BeiDou-Client\Data\UI\UIWindow.img
```

目标是把创建角色券界面的图片文字改掉，例如 `Name of Character`、`BACK`、`NEXT`。定位确认这些文字在 `CharacterClone` canvas 图片里，例如：

```text
CharacterClone/MainStep2
CharacterClone/MainStep4
CharacterClone/BtBack/normal/0
CharacterClone/BtNext/normal/0
```

曾写过临时 C# 工具读取 canvas、用 `System.Drawing` 重绘文字，再通过 MapleLib 保存回 loose `.img`。核心 API 是：

```csharp
var img = manager.LoadDataWzHotfixFile(input, WzMapleVersion.GMS);
if (!img.Parsed) img.ParseImage();

var canvas = ... as WzCanvasProperty;
using var bmp = (Bitmap)canvas.PngProperty.WzValue;
// 修改 bmp 后：
canvas.PngProperty.PNG = patchedBitmap;
img.Changed = true;
img.SaveImage(writer, true);
```

注意两个坑：

1. 如果不设置 `img.Changed = true`，`SaveImage` 可能直接复制原始 data block，导致看似修改了内存图，实际写回仍是旧图。
2. 即使 MapleLib 写回后的 `UIWindow.img` 能被工具重新解析，游戏客户端打开后仍出现整个弹窗花屏。

因此当前结论：

- MapleLib 可以用于 `String\*.img` 这类文本资源生成。
- 不建议直接用 MapleLib 重写客户端 `UI\UIWindow.img` 这种包含大量 canvas 的 UI 图片资源，至少不能在没有游戏内验证前用于打包。
- 本次已经恢复 `UIWindow.img`，没有保留导致花屏的图片改动。
- 图片上的英文按钮/标题暂时不替换，只保留内容文本汉化。

## 文件锁和残留进程处理

客户端运行时会锁住 `Data\UI\UIWindow.img` 等资源文件。遇到看不到窗口但文件无法覆盖时，可以查残留进程：

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*BeiDou*' }
Get-CimInstance Win32_Process | Where-Object { $_.Name -like '*BeiDou*' }
```

普通 `Stop-Process` / `taskkill /F` 有时会被拒绝。曾用 WMI 成功结束残留进程：

```powershell
wmic process where ProcessId=<PID> call terminate
```

如果仍失败，最稳的是重启系统或注销登录后再覆盖文件。

## 验证记录

发型预览修复验证：

- `327xx` 发型预览正常。
- `40000` / `40001` 高编号发型进入娜塔丽高级染色后不再崩溃。
- 不再需要服务端临时日志。
- 不再需要补 `42001` / `52001` 这类客户端内部派生编号。
- Release x86 编译通过。
- 新 `ijl15.dll` 覆盖客户端后测试通过。

## 2026-06-23 普通角色渲染误入预览分支

后续线上出现 `0x80004003 / E_POINTER` 崩溃。x86 dump 的异常上下文里同时出现：

```text
/Data/Character/Hair/00037545.img
error code : -2147467261 (无效指针)
```

`00037545.img` 本身存在，且与同组 `37540-37547` 结构一致。数据库确认 `37545` 属于同场景玩家 `红脸大蘑菇`，不是崩溃角色自身发型。

根因是前一次脸型预览修复把发型判断扩成了 `30000-49999`、`60000-79999`。`AvatarLayerBuild_Hook` 是全局头像图层构建 hook，只靠 ID 范围会把普通角色发型 `37545` 当成发型预览处理，并把栈上的临时 avatar 数据传给原始构建函数，正常角色渲染后续再读到这份临时数据时可能触发无效指针。

修复策略：

- 资源类型判断和预览 workaround 判断分离。
- `30000-39999` 普通发型不再进入高编号发型预览 workaround。
- 高编号发型预览若目标与当前发型不同，继续使用临时 avatar 数据替换 hair slot。
- 高编号发型预览若目标与当前发型相同，只把 `a3` 改为 `0` 并保留原始 avatar 指针，避免走客户端会派生编号的 item-preview 路径，也避免给普通渲染留下栈上临时指针。

WZ 文本汉化验证：

- `Data\String\GLcloneC.img` 已由中文 XML 生成。
- 用 `@tybys/wz` 读回确认创建角色券职业、Step4/Finish 描述为中文。
- `Data\UI\UIWindow.img` 已恢复，不保留图片改动。

## 发布前注意事项

- 不要保留服务端 NPC 调试日志。
- 不要保留 crash dump、debug log、临时 DLL 备份。
- 不要把低编号发型塞进高级染色列表作为长期方案。
- 不要假设 `42001` / `52001` 是服务端发送，它们更可能是客户端内部派生。
- 打包前确认客户端只包含需要发布的资源改动，例如 `Data\String\GLcloneC.img` 和正式 `ijl15.dll`。
- `config.ini` 通常是本机配置，不应混入正式提交或打包基准，除非明确需要。
