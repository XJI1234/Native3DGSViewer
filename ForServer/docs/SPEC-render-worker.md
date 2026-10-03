# Spec: render-worker（固定模型实例）

## 目标、结构与技术栈

Ubuntu22/24、C++20、CUDA Runtime/CUB、现有model-io；headless，full SH0–3、tile raster，不拆分单帧跨卡，不实现Vulkan/EGL图形桌面。src/cuda_renderer.cu为GPU热路径；src/server_main.cpp为CLI/回环serve。每模型一个进程，显式固定GPU，Go负责多用户与生命周期；多个模型可各有实例驻同一GPU，由Go卡级门控渲染。

## native接口

~~~bash
gs-server serve --catalog /path/catalog.tsv --device 0 --port 19000 --instance-model m-model
~~~

只监听127.0.0.1，受信catalog为model-id TAB absolute-path，网络不能传路径。--instance-model指定后拒绝不同模型，保留未指定的历史CLI调试模式，不用它实现生产模型切换。GET /health返回worker PID；POST /frame返回NGSFRM02及identity/stats。

NGSREQ3 request_id model_id width height yaw_deg pitch_deg distance_factor profile flip_y。非零u64，有限yaw/pitch±36000°、距离0.1..10、flip0/1；尺寸/profile依image-frame。旧NGSREQ2继续接受原pitch±89°。Go、native、Windows共用2°/81距离档的规范化定义；详细数学和码字见SPEC-instances-cache-v3.md。

相机围绕包围盒中心归一化原点，垂直FOV60°，按较小水平/垂直FOV加10%边距fit，包含3σscale支撑。不删离群点、不调整SH来改善压缩。全球面/极点有稳定解析right向量，穿极点折叠位置并翻roll。Y反转为up/right180°，保持位置/世界轴/球心不变。缓存命名另有f0/f1朝向变体。

## CLI与验证

~~~bash
gs-server devices
gs-server self-test --device 0
gs-server render --model scene.spz --out frame.ngsf --profile rgba --yaw 0 --pitch 90 --zoom 1 --flip-y 1 --width 1280 --height 720 --device 0
cmake --build build -j 8
ctest --test-dir build --output-on-failure
~~~

GoogleTest验证稳定排序、CPU参考投影/SH/raster、全剔除恢复、orbit/非有限参数、极点/Y反转；协议测试覆盖request3/完整球面/距离/predicted_views。Compute Sanitizer memcheck记录独立证据。多卡保留但本轮只测生产GPU0；上一轮逐卡证据不冒充本轮运行。模型和原SDK ABI不变。
