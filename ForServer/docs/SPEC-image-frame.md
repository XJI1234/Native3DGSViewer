# Spec: image-frame v2

## 目标与边界
客户端只持有图像及受限预测图像缓存，不接收模型或高斯数据。删除所有对外二维高斯 profile 和客户端 Gaussian pipeline；服务器内部 Ellipse 和独立 CPU raster reference 保留以测试投影、排序与合成。旧 v1 帧明确拒绝，不静默解读。API v3/NGSREQ3不改变本图像包v2契约。

## 契约
64 字节 little-endian header：magic `NGSFRM02`；offset 8 version=2 u32；12 profile u32（0=rgba、85..95=JPEG quality）；16/20 width/height u32；24 reserved=0；28 compression u32（0=raw、1=Zstd）；32 uncompressed RGBA bytes u64；40 payload bytes u64；48 payload IEEE CRC32；52 header CRC32（计算时该字段置零）；56..63 reserved=0。严格检查长度、版本、保留位和 CRC。

RGBA 默认 Zstd level 1，压缩不能节省字节时 raw fallback；JPEG 4:4:4，不再包 Zstd，alpha 解码为255。尺寸1..4096，最大64 MiB RGBA，payload最多64 MiB。无三维模型/SH/排序数据字段。参数 `rgba`、`jpeg85`..`jpeg95`；UI先暴露95/90/85。

## 实现、命令与风格
`include/gs_server/frame.h` 和 `src/frame.cpp` 是 provider；平台 JPEG adapter 保持现有库。C++20，四空格、PascalCase 类型、snake_case 函数。`cmake -S ForServer -B out/server-windows -DGS_SERVER_CUDA=OFF`；`cmake --build out/server-windows --config Release`；`ctest --test-dir out/server-windows -C Release --output-on-failure`。

## 验收
GoogleTest 验证RGBA逐字节、JPEG所有11档、损坏/截断/尾随/旧协议/非法profile/过大长度、Zstd framing、恢复；Windows图像copy逐字节。不删除历史测量，不将新结果与旧协议混为同一次实验。所有输入先校验再分配。
