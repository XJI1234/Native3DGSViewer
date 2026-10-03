# Web 引擎任务与验收

- [x] T01 固定 Node/pnpm/Emscripten、建可重现 WASM 构建；build:wasm + 实际WASM执行通过；开发环境记录固定版本。
- [x] T02 共享类型、输入预检/限制与38模型hash manifest；7个WASM契约测试通过。
- [x] T03 PLY/SPZ规范化、SPZ1–4×SH0–3、gzip精确长度安全验证；取消/总deadline浏览器通过。
- [x] T04 全局稳定radix；0/1/255/256/257/1025/65537/1048576键CPU参考通过；4/8bit实验保存。
- [x] T05 SH全部基项逐像素、各向异性/behind-camera/preblur、两页透明、九视角Spark质量通过。
- [x] T06 18个CPU测试、实际联合/资源准入/恢复/关闭/订阅测试通过；真实硬件reset仍未测试。
- [x] T07 独立tgz/SSR、生产和开发StrictMode、文档TSX/SFC编译、真实WASM、卸载0device/0Canvas通过。
- [x] T08 38模型完整加载/明确拒绝、阶段数据；100次生命周期+30分钟/60采样无错误、dispose资源全零。
- [x] T09 SparkJS2.3.1双引擎有节流/无节流三模型30s+60s×3、九图；prefix/PLY优化与8bit回退完成；20%中位间隔目标未达成，物理呈现/动态等质量仍待验收。
- [x] T10 独立审查/复审必改项关闭、接口与机制矩阵、Vue/React专业文档及文档示例严格编译/真实消费通过。

每个任务必须附命令、结果与局限。checked 表示实测完成，不能只凭文件存在勾选。

完整命令/证据/局限见docs/verification/{implementation-report,performance-report,review-and-interface-matrix}.md。大型超预算模型拒绝不算显示成功，跨平台、渲染Worker、SIMD/pthreads、LoD、云客户端是独立后续项。
