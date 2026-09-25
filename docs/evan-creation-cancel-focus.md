# 取消创建角色：失效焦点与重复窗口登记（2026-09-25）

09:06:26 首次报告捕获 E_POINTER：CLogin 鼠标处理 0x005F5390 → SetFocus 0x009E3264 → OnActivate 0x009E03A6 → 0x009E43FF，窗口 +0x18 绘图层为空。四职业界面的 ESC 原先直接 ChangeStage(2)，绕过原生 CancelRaceSelect(0x005F9805) 的返回窗口重建步骤。

前两版修复不成功。09:17 和 09:27 的转储均在 stage 3→2 后，CLogin 0x005F5FD6 销毁 CRaceSelect，DestroyWnd 0x009E0187 扫描窗口列表时，在 0x009E4395 调用旧窗口坐标读取，因绘图层为空抛错；退出清理再次抛错导致 runtime termination。09:17 转储中对象 0x21A0D4A4 的 vtable=0x00AF7360（CRaceSelect），+0x14/+0x18 均为0，但仍被列表扫描。

链路证据：CreateWnd 0x009DE781 调用 OnCreate，随后 0x009DE78D 无条件调用 0x009E43FF 登记窗口。错误修复在 OnCreate 内调用 SetFocus，触发 OnActivate 提前登记相同窗口；返回后第二次登记。0x009E43FF 的 0x009E4476/0x009E4482 插入列表项，而 DestroyWnd 的 0x009E0140 调用 0x009E44BA 只查找并删除一个匹配项，故留下空图层窗口。上述地址由本地 BeiDou.exe 反汇编核对。

此前“fastcall 参数错位”的解释是错误的：使用显式 EDX 占位参数的 fastcall 在该处与 thiscall 的实参位置相同，故第二次仅改声明未改变崩溃。不能再将调用约定作为根因。

当前修复撤掉 OnCreate 内 SetFocus，只写 CLogin+0x188 的返回目标为 self+4（与原生 CRaceSelect::OnSetFocus 0x006175D8 相同），不触发窗口列表操作。增加 OnDestroy 清理匹配的返回目标与缓存，原 OnDestroy=0x00461BA1 已核对为 ret。保留 ESC 的原生 CancelRaceSelect 返回流程，Stage 切换时清空自定义缓存。低频 creation.focus / creation.stage 日志保留。

Release|x86 构建与构建内检查通过；这只是静态和构建验证，不代表游戏内取消流程已通过。第三版于 2026-09-25 09:32:28 确认客户端退出后安装，构建产物与 BeiDou-Client/ijl15.dll 的 SHA-256 均为 D5D2673AD582AD28BFA3AFA414DFAE9373FB49EC8A1D5B3F534D8DDC995C71E6。安装前备份为 exports/evan/visual-fixes/cancel-focus-backup-20260925-093228.dll。当前版本实机结果待确认。前两版均已实测失败，不得视为可用修复。

用户后续实测范围：职业选择 ESC 返回角色列表、再次创建；外观/命名页取消返回职业页后再选择；鼠标与键盘操作均应覆盖。未授权自动启动或操作客户端。
