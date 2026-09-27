# Spec: splat-types

## 目标与边界

为 Windows 和 Android 提供同一份平台无关的规范化场景及相机数学契约。模块只依赖 C++20 标准库，不包含 Win32、Vulkan、JNI、文件系统句柄或 GPU shader 布局。Windows 现有 `SplatScene` 的坐标和 SH 意义是迁移基线；任何布局变更先更新两端消费者契约测试。

## 公共契约和所有权

场景字段保持 `count`、实际 `shDegree`、来源格式、double `worldOrigin`/bounds/`maxScale`，以及只读 float32 SoA：局部中心、正尺度、xyzw 单位四元数、[0,1] opacity、训练色值域 `rgb0` 和 SH 余项。右手 RUB、+Y 向上、相机局部 -Z 向前。PLY 默认 RDF 输入需要明确转换；不能由文件名猜坐标。DC/SH 颜色转换和屏幕编码由 renderer 定义，场景不提前 clamp。

`SceneHandle` 保持底层只读共享映射与所有 span 同寿命；Android 端的共享 FD、mmap 和复制得到的缓冲均通过平台私有 RAII storage 持有。任何异步上传在最后 copy fence 前保留句柄；下游不得写入或在 storage 释放后缓存裸指针。场景布局带版本、总字节数和各数组偏移/长度，所有乘加受检；跨进程接收者重算并逐段校验，不能信任 Service 传来的单个长度。空场景、非法 SH 阶、NaN、无限坐标、无效 bounds/rotation 和溢出的场景字节都不形成可发布句柄。

相机数学与场景所有权分开测试：fit 根据 bounds、最大 splat 支撑和视口宽高比；轨道拖动方向以屏幕方向为准，轴镜像不能反转手势；自由模式的移动与视线速度按秒计。镜像只影响查看变换，不改共享场景数据。公共 C++ 源码可被两个平台内部使用，但外部 Android SDK 通过版本化 C ABI 暴露不透明句柄，不承诺跨编译器 STL ABI。

## 实现顺序与失败处理

先用当前 Windows 测试固定序列化布局、相机数值、PLY/SPZ 规范化和极值；再提取共享头/实现，维持旧 Windows 公开接口或提供无损适配层；最后在 Android 主机与 NDK 构建中编译同一源码。提取期间不更改颜色/坐标约定以图省事。布局版本不匹配、长度越界或不可表示的数值返回结构化错误，不截断点数或 SH。

## 测试与退出条件

合成样本覆盖 SH 0-3、通道顺序、透明端点、坐标翻转、远原点和最小/最大尺寸；两平台对同一输入输出相同规范化数值或约定容差。Windows 全套 ModelIo/Engine/RenderCore 回归通过，Android 主机测试在 ASan/UBSan 配置下通过，跨进程布局损坏测试拒绝越界数据。公开头可用 Android NDK 编译且无 Windows 类型，即完成本模块首期验收。
