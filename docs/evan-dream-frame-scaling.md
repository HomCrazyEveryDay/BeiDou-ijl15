# 龙神梦境前景宽屏适配

原资源 `Map/Back/dragonDream.img/ani/6..8` 是 800×600 的透明梦境前景。宽屏截图显示原尺寸居中白框；不修改原版资源或地图坐标。

已核对本地 BeiDou.exe：`CMapLoadable::LoadBack` 0x0063CD4E；局部 -0x38 是读取 bS 的 `_bstr_t`，-0x3C 是宽字符串 `ani/%d`。0x0063D9D1 调用 0x0043EA3E 构建动画层。仅当 bS=dragonDream、ani=6/7/8 时开启当前线程的缩放上下文，函数退出（含异常展开）清除。其他背景不处理。

0x0043F768 在读取原 canvas 的 delay/a0/a1/zoom0/zoom1 后，通过 0x00426BAB 调用 IWzGr2DLayer::InsertCanvas。拦截该包装函数只替换此时传入的 canvas，原动画参数直接传递。两个入口校验原机器码，不匹配则不安装。原资源保持不变，临时副本插入后释放本地引用。

通过本地 PCOM.dll 创建独立 Canvas 对象并反汇编 Canvas.dll 验证 ABI：Create=0x2C，width=0x40，height=0x48，cx=0x6C/0x70，cy=0x74/0x78，CopyEx=0x84，GetPixel=0x88。CopyEx 的最后 VARIANT 必须是 VT_EMPTY，不能传 VT_I4(0)。使用当前分辨率创建 BGRA8888 canvas，复制完整 800×600 像素并按相同比例转换原点。只处理确认尺寸，失败返回原画布。

`tests/DreamCanvasTest.ps1` 直接加载本地绘图库，不启动游戏。验证 1024×768、1280×720、1920×1080 的尺寸、中心原点、不透明边框、透明中心、半透明像素保持、源画布不变及 800×600 bypass。

Release|x86 构建通过并已同步本地客户端；保留已有构建警告。安装位置与断点字节经静态核对，最终游戏内加载链和视觉效果仍需用户实测；没有代替实机验证的声明。
