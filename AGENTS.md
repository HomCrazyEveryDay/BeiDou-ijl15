# BeiDou ijl15 工程协作规范

## 汉化改动禁止项

- 汉化默认属于 WZ/XML/IMG 资源工作，不属于 DLL hook 工作。
- 除非已经用搜索或反汇编证明目标文字硬编码在 `BeiDou.exe` 中，禁止为了汉化修改 `ijl15.dll`、DLL hook 源码或重新发布 DLL。
- 如果文字来自 `String.wz`、`Item.wz`、`Quest.wz`、客户端 `Data/**/*.img` 等资源，必须回到对应 WZ/XML 源和客户端 IMG 同步流程处理。

## 工程定位

- 本工程生成客户端加载用的 `ijl15.dll`。
- 目标客户端目录通常是 `C:\Users\HomCrazy\Desktop\work\BeiDou-Client`。
- 客户端 loose WZ 目录是 `C:\Users\HomCrazy\Desktop\work\BeiDou-Client\Data`。
- 服务端工程是 `C:\Users\HomCrazy\Desktop\work\myself_BeiDou_Server`，但修 DLL 时不要顺手改服务端，除非任务明确要求。

## 生成新 DLL 的标准流程

1. 先确认改动范围：
   - 用 `git status --short` 看当前工作区。
   - 只处理本次任务相关文件；不要回滚或整理别人/用户已经改过的文件。
   - 如果只是构建 DLL，不要改 `config.ini`、WZ 资源、临时日志或客户端文件。

2. 使用 Release x86 构建：
   - 推荐 Visual Studio 2019 / v142 工具集。
   - 方案文件：`ezorsia.sln`。
   - 配置必须是 `Release`。
   - 平台必须是 `x86`，不要用 `x64` 或 `Any CPU`。
   - 输出文件应为 `out\Release\ijl15.dll`。

3. 命令行构建示例：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe' `
  .\ezorsia.sln `
  /p:Configuration=Release `
  /p:Platform=x86 `
  /m
```

如果本机是 VS 2022 或其他安装路径，先找 `MSBuild.exe`，但仍保持 `Release|x86` 和工程里的 `v142` 工具集。

4. 构建后检查：
   - 确认 `out\Release\ijl15.dll` 存在且时间戳已更新。
   - 如果链接失败，先看是否缺 `detours.lib` 或 `Ws2_32.lib`。当前 `ezorsia.vcxproj` 应链接 `detours.lib;Ws2_32.lib`。
   - 不要把 `out\` 下的中间产物当源码提交。

## 替换客户端 DLL 的规范

- 替换前确认客户端已关闭，否则 `ijl15.dll` 可能被锁。
- 检查残留进程：

```powershell
Get-Process | Where-Object { $_.ProcessName -like '*BeiDou*' }
```

- 如果用户说已经关掉客户端，再复制：

```powershell
Copy-Item -LiteralPath .\out\Release\ijl15.dll `
  -Destination 'C:\Users\HomCrazy\Desktop\work\BeiDou-Client\ijl15.dll' `
  -Force
```

- 不要自动删除用户已有备份。需要备份时，优先说明并使用清晰名字，例如 `ijl15.dll.bak-YYYYMMDD-HHMM`。
- 不要在用户没有要求时替换正式服客户端、远端机器或压缩包里的 DLL。

## 验证要求

- 至少确认构建成功和目标 DLL 已更新。
- 对崩溃、渲染、登录、封包、界面 Hook 等问题，优先用最小复现路径验证。
- 验证范围要覆盖：
  - 修改前稳定的旧路径，确认没有回归。
  - 本次修复的直接路径，确认问题消失。
  - 相邻或同类路径，确认没有引入新的崩溃或无响应。
- 涉及登录或加载流程时，至少验证进入登录界面、选角、进频道。
- 如果需要玩家手动验证，改完并替换 DLL 后就停止，等待用户反馈；不要自作主张连续改多版。
- 如果加了临时日志，最终提交前必须删掉或关闭。

## 调试和清理规范

- 可以临时加日志、dump、内存扫描，但最终不要保留：
  - `avatar_preview_debug.log`
  - 崩溃 dump
  - 宽泛路径日志
  - 大范围内存扫描日志
  - 临时探针 hook
  - 临时占位/欺骗式绕过逻辑
- 排查客户端问题时，先确认输入数据、资源文件、调用路径和客户端崩溃点，再决定是否改 DLL。
- 对客户端内存结构、Hook 地址、调用约定、工具用法等通用经验，可以写进 `docs\*.md`。
- 不要把具体业务策略、具体 NPC/道具/外观池配置、临时业务绕法写进工程规范。

## Hook 日志规范

- Hook 日志用于确认客户端内部执行路径，不用于表达业务含义。
- 日志内容优先记录：
  - 函数名或 Hook 名称。
  - 当前线程 ID。
  - 原始入参值。
  - 关键指针地址，但不要默认大范围 dump 内存。
  - 分支判断结果。
  - 调用原函数前后的返回值。
  - 捕获到的异常码。
- 日志文件统一使用类似 `ijl15_hook_debug.log` 的清晰名字。
- 日志必须可开关，默认关闭。优先复用 `Client::debug` 或配置项控制。
- 高频 Hook 不要无条件刷日志；必要时只打印前 N 次、按条件打印或加采样。
- 指针读取必须保护，避免日志代码本身导致崩溃。
- 不要在 Hook 日志里写具体业务名、具体 NPC、具体道具或具体配置池。
- 不要在 Hook 函数里做复杂计算、阻塞等待、网络请求或大文件读写。
- 问题确认后，删除临时日志代码；如果需要长期保留，必须默认关闭且不影响正常性能。

通用日志函数示例：

```cpp
static bool ShouldLogHook()
{
    return Client::debug;
}

static void HookLog(const char* fmt, ...)
{
    if (!ShouldLogHook()) {
        return;
    }

    FILE* fp = nullptr;
    fopen_s(&fp, "ijl15_hook_debug.log", "a");
    if (!fp) {
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d.%03d][tid=%lu] ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentThreadId());

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}
```

通用 Hook 包装示例：

```cpp
static int LogHookException(unsigned int code)
{
    HookLog("TargetFunc exception code=0x%08X", code);
    return EXCEPTION_CONTINUE_SEARCH;
}

typedef int(__fastcall* TargetFunc_t)(void* pThis, void* edx, int arg1, int arg2);
static auto TargetFunc = reinterpret_cast<TargetFunc_t>(0x12345678);

static int __fastcall TargetFunc_Hook(void* pThis, void* edx, int arg1, int arg2)
{
    HookLog("TargetFunc enter this=%p arg1=%d arg2=%d", pThis, arg1, arg2);

    int ret = 0;
    __try {
        ret = TargetFunc(pThis, edx, arg1, arg2);
        HookLog("TargetFunc leave ret=%d", ret);
    }
    __except (LogHookException(GetExceptionCode())) {
    }

    return ret;
}

bool HookTargetFunc(bool enable)
{
    return Memory::SetHook(enable, reinterpret_cast<void**>(&TargetFunc), TargetFunc_Hook);
}
```

## WZ 和资源注意事项

- 读取客户端 loose `.img` 可用 `@tybys/wz@1.7.1`，一般用 `WzMapleVersion.GMS`。
- `@tybys/wz` 适合只读结构检查，不适合写回。
- MapleLib / HaRepacker 可用于 String 类 `.img` 生成，但不要轻易重写大型 UI canvas `.img`，曾出现游戏内花屏。
- `config.ini` 通常是本地配置，不应混入提交或 DLL 构建基准，除非任务明确要求。

## 提交信息规范

- 提交信息必须使用中文。
- 标题必须以 Conventional Commit 类型开头，例如 `fix:`、`feat:`、`docs:`、`chore:`、`refactor:`、`test:`、`build:`、`ci:`、`perf:` 或 `style:`。
- 正文用 `-` 列出主要改动，每条一行。
- 不要提交本地环境文件、日志文件、构建产物或与当前任务无关的文件。

示例：

```text
fix: 修复客户端渲染路径异常

- 调整目标函数 hook，避免异常参数进入崩溃路径
- 保持原有稳定路径行为不变
- 移除临时调试日志
```
