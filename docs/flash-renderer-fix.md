# Flash 渲染与退出异常修复（2026-09-13）

## 根因与证据

本地 20260912-195940137Z-pid15252 生命周期日志中，高频异常 EIP=1A751298，ECX=0，ESI=1A7BE080，读取地址 0。对应 WzFlashRenderer.dll 基址+1298 与全局上下文+6E080。Gr2D_DX8.dll 的 +7CE3 调用对象 RenderFlash 回调，返回点 +7CE5 与日志栈一致。

WzFlashRenderer.dll（PE timestamp 486B58AE、SizeOfImage 82000）LoadMediaFile 在未加载或失败时会让上下文 +8（动画定义）或 +C（实例）为空。RenderFlash 只检查另一个标志，没有检查这两个对象；内部 +1298 对空指针解引用。自身 SEH 接住异常并返回 1，主循环每帧重试。尚无证据证明玩家一定缺失某个 SWF 文件；没有加载动画本身也能复现。

Gr2D_DX8.dll（timestamp 4B7C13FD、SizeOfImage 40000）+35E9 与 +35F0 连续对同一对象调用 +7B4C，后者执行 ReleaseFlash 后 FreeLibrary，不清空回调。第二次调用可能执行已卸载的 ReleaseFlash 地址，与退出阶段模块入口 +1000 的异常吻合。

## 修复

FlashRendererFix 在原生图形工厂返回后获取已加载 Gr2D 的引用，不主动加载模块，并保留该引用。校验 PE 与指令后挂钩 +7CA6：仅当回调属于已核实 Flash 版本且动画定义/实例为空时，提前返回原生失败码 1；正常对象交回原生函数。不同 Flash 版本不套用该检查。将 +35F0 的第二次重复清理 call 改为 NOP，保留第一次完整清理及后续设备清理。

仅修改运行时内存，磁盘 Gr2D_DX8.dll/WzFlashRenderer.dll 不变。未增加异常吞掉逻辑，未关闭异常日志。补丁只解决已确认的这两条图形路径，不能证明修复旧的 EXE 004181D8 字符串赋值崩溃或其他掉线。

## 验证

运行 tests/FlashRendererFixTest.ps1，加载客户端原版 DLL，不启动游戏：
- 原生空动画渲染 100 次产生 100 次 AV；修复后 10000 次产生 0 次 AV，返回值同为 1。
- 不存在的媒体文件加载失败后，渲染无 AV；只有定义没有实例也安全失败。
- 构造有效虚表对象、使用屏幕外坐标，验证实际 RenderFlash 仍调用动画虚函数并返回成功，不要求创建 D3D 设备。
- 执行真实 +35DF 清理指令块，原版释放回调 2 次，修复后 1 次。测试夹具在指令块后返回，避免无设备时运行其余销毁流程；不代表实机完整设备退出已经验证。
- 重复安装与拒绝不匹配模块通过。Release|x86 构建通过。

实机仍由用户验证：进入地图、换线、返回登录、关闭窗口，并检查 flash.fix install result=1 及上述异常是否消失。

## 实机反馈与安装时机修正

04:22:17–04:23:42 的 pid15124 测试显示 install result=0 gr2d=0；主动 LoadLibrary 的模块初始化调用到了未就绪 EXE 回调并发生地址0异常。该次没有高频 Flash 异常不能归功于补丁；退出仍有旧的 +35F5 → ReleaseFlash 异常。

改为在 RefreshRateTrace 的原生图形工厂返回后调用安装，使用 GetModuleHandleEx 获取既有模块，禁止提前 LoadLibrary。测试补充：未加载模块时调用安装不会加载模块；原生加载完成后通过同一生产入口安装，并重新验证渲染和清理。仍待用户重新实机验证 install result=1。

## 修正版实机验证

04:26 与 04:27 两轮测试均记录 install result=1 stage=after_native_graphics_init。两轮 C0000005 和 exception_repeat 均为 0；有进图、打怪、换线与返回登录记录。第一轮进程已结束且未记录旧的释放异常；第二轮检查时仍运行，因此未据此宣称其关闭窗口验证完成。保留的 E06D7363 记录发生在启动和返回登录流程，程序随后继续运行。
