# Spec: windows-client（离散视角与预取）

## 目标与控件

WindowsClient/，WinUI3 C++/WinRT、自包含unpackaged x64；复用原GUI浅色工具栏/画布/44px状态栏，不改原GUI/SDK。两行工具栏，模型栏可横向滚动。WriteableBitmap显示图片；客户端没有模型解码、SH/投影/排序/高斯光栅化。

连接页要求地址/端口/HTTPS/token密码框/显式可信私网确认；protocol2健康与认证目录成功才进入。查看页有模型/打开/关闭/适配/重置、方向键/按钮、鼠标拖动/滚轮、RGBA及JPEG95/90/85、反转Y、预渲染启用及低/中/高（5/10/15档）。默认JPEG85，预渲染每方向5档。

## 空间与状态

方向每次2°，拖动吸附2°，pitch整圆循环而非±89°；滚轮按81个0.1..10对数距离档移动，fit为d40。模型固定居中，不支持Fly。完整方位/极角码aNNN-tNN-dNN；极点和折叠朝向由canonical_request确定。反转Y不改位置编码/世界基准，以独立朝向图片变体显示倒置模型。

前台最多一个在途及一个最新pending；generation/request-id/model/dimensions/profile验证，过期响应不得覆盖当前状态。网络/WIC/Zstd在后台，UI只做BGRA复制/bitmap更新。模型/尺寸/质量/朝向是cache key一部分。关闭窗口/断开安全使回调失效。

当前帧显示后独立单协程预取四方向各5/10/15档。移动时更新窗口，复用重叠项并停止未开始的旧请求。缓存压缩包64MiB/256项LRU，保护当前和最近四邻域，仍严格守预算；只显示时解码。当前后台HTTP可能完成但不会显示为当前帧。状态栏包含position、local/hit/miss、累计预取数与解码/网络时长；累计数不等于当前窗口完整率。

POST /sessions先开租约，每10秒heartbeat；关闭/切换/断开释放。预测带session及X-GS-Prefetch，前台也校验session。410 heartbeat重建会话后清本地图片并重新请求，避免服务升级/模型变更后的旧帧。租约是访问活跃状态，不是账户登录。

WinHTTP默认非回环HTTPS、证书校验，显式私网HTTP只对字面RFC1918；token不持久化/日志/命令行。帧CRC、identity、尺寸/profile和预算校验复用image-frame。

图像请求使用异步WinHTTP并在工作线程等待完成；切换视角/模型/关闭时只标记取消，由拥有请求的线程关闭handle，等HANDLE_CLOSING后释放callback状态和读缓冲区。控制请求使用独立短同步超时。退出主循环后执行有界最终租约释放；关闭显式清缓存，迟到结果禁止入缓存。缓存presence查询不复制packet，本地解码失败驱逐对应包。smoke成功/失败退出码为0/1，报告或截图写失败也返回1。

## 命令与验收

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ForServer/WindowsClient/build.ps1
ctest --test-dir out/server-windows -C Release --output-on-failure
.\out\cloud-client\Release\Native3DGSCloud.exe
~~~

输出需携带整个自包含目录。GoogleTest验证帧/transport/session route/LRU保护；真实WinUI smoke走连接、模型、角度/缩放、四质量、过期帧、Y反转、等完整预测窗口后相邻本地命中、90°极点，记录JSON及截图。401/HTTP无确认/不可达错误检查不得进入查看。证据见verification-instances-v3.md；真实输入设备/多DPI/长稳/实际服务器重启恢复尚需单独人工或E2E测试，不以CLI替代。
