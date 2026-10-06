# Native3DGS Vue 查看器模板

独立项目，版本 `0.2.2-preview.3`。使用安装后的 `@native3dgs/web` 公开接口及 `/vue` 适配器，不引用仓库内部源码。模板源码包自带 `vendor/native3dgs-web-0.2.2-preview.3.tgz`，不需要先发布到 npm。锁文件固定全部依赖。

## 快速开始

Node.js 24.14+、pnpm 11.4.0。解压后在本目录执行：

```powershell
pnpm install --frozen-lockfile --ignore-scripts
pnpm dev
```

访问终端给出的 localhost 地址。需要支持 WebGPU 的浏览器及可用硬件；部署必须 HTTPS。点击打开模型或将一个 PLY/SPZ 拖入画面。不会把本地文件上传到服务器。远程 HTTP(S) 地址需要服务端允许 CORS，HTTPS 页面不能加载 HTTP 混合内容。

```powershell
pnpm test
pnpm format:check
pnpm build
pnpm preview
```

源文件由固定Prettier版本统一格式化；构建包含类型检查，`pnpm assets` 从实际安装包读取资源清单并核验 WASM/JS SHA-256，再复制到 `public/gs-assets`（含第三方许可证）。无需 WASM 编译环境。分发 `dist/` 时保持资源路径与许可证完整；WASM 应返回 `application/wasm`，Worker 与 decoder.mjs 返回有效 JavaScript MIME，不能被 SPA fallback 改成 HTML。

## 项目结构

- `src/App.vue`：SDK生命周期适配器、状态展示、文件/地址输入及控件。
- `src/viewer.ts`：宿主操作会话，处理load identity、取消、消息、PNG下载，渲染/解码仍由SDK负责。
- `src/main.ts`：标准挂载入口，`mount(host)`返回卸载函数。
- `src/style.css`：同一套响应式、中性色和单一强调色；遵循系统深浅主题及reduced motion。
- `src/viewer.test.ts`：候选加载竞态、关闭/取消、非法来源、截图失败、卸载与翻转持久性。
- `scripts/copy-sdk-assets.mjs`、`vendor/`：安装包资源部署及固定SDK。

源码包不包含模型、node_modules、dist、机器路径或凭据。普通包安装仍需下载框架依赖；完全离线部署可预先准备pnpm store。

## Windows 功能对照

|桌面操作|模板入口|
|---|---|
|本地打开/拖放，候选就绪前保留旧模型|文件按钮、拖放、`engine.open`|
|加载进度/取消/关闭|snapshot.progress、LoadOperation.cancel、closeScene|
|固定旋转/右键平移/滚轮|SDK DOM适配器|
|自由浏览/WASD/QE/Shift/Esc|Camera.mode与SDK Pointer Lock/键盘适配器|
|适配/重置/Y翻转|fitScene、camera.reset、camera.setFlipY|
|诊断/设备恢复|公开snapshot及recover|
|截图、远程地址|capture、HTTP(S) Source|

Y翻转是关于场景中心Y的显示反射，不改变文件或canonical相机；旋转、平移、自由浏览方向翻转前后保持一致，fit/reset/open/recover保持翻转状态。**不要添加Canvas CSS镜像，也不要给鼠标增量再乘翻转符号。** 自由模式点击视口请求Pointer Lock；拒绝后左拖仍能转向，方向键与加减键可键盘浏览。Esc、失焦、切模式或卸载都会结束捕获/移动。

桌面CLI路径、WinUI交换链、系统安装器/日志及原生自动减SH/抽样策略不属于浏览器模板。Web SDK保留完整质量，超出WebGPU/内存/存储预算时返回结构化错误并保留旧场景；模板不伪造成功。GPU timestamp不可用时显示“未提供”，GPU时间可能来自早先完成帧，不能当成当前帧准确延迟。

## 集成到现有网站

保留SDK安装依赖，复制App、viewer和style，或只采用SDK hook/composable并替换UI。一个挂载视口对应一个Canvas/engine；卸载由适配器异步dispose，宿主会话取消未完成请求并忽略迟到结果。不要手动复用Canvas，不要读取私有renderer/active，也不要按响应式深层代理包裹WebEngine。

资源地址依据Vite `BASE_URL`，支持子路径发布。例如PowerShell：

```powershell
$env:VIEWER_BASE = '/viewer/'
pnpm build
```

将生成目录部署到同一`/viewer/`路径。preview.3 默认使用 auto/4 多线程 WASM 与 adaptive 排序；Vite dev/preview 已配置 COOP/COEP。生产服务器必须添加这些头才能自动启用 pthreads；缺少隔离时 auto 仍可回退单线程。CSP应允许应用脚本、同源Worker、WASM执行及必要模型connect-src；按网站实际域名配置，不能让资源请求返回登录页或跨域重定向。

### Nginx 部署排错

公网网站必须使用 HTTPS；本地 localhost/127.0.0.1 的 HTTP 开发例外不适用于公网 IP。若加载时出现 `DecoderFailure · Load: Failed to fetch dynamically imported module: .../gs-assets/decoder.mjs`，检查该文件的响应类型：即使状态为 200，`application/octet-stream` 也会使浏览器拒绝导入 ES 模块。

在网站的现有 HTTPS `server` 块内，与其他 `location` 同级添加：

```nginx
add_header Cross-Origin-Opener-Policy "same-origin" always;
add_header Cross-Origin-Embedder-Policy "require-corp" always;

location = /gs-assets/decoder.mjs {
    types { }
    default_type application/javascript;
    try_files $uri =404;
}
location = /gs-assets/threaded/decoder.mjs {
    types { }
    default_type application/javascript;
    try_files $uri =404;
}
```

此配置沿用 `server` 的 `root`，应指向部署后的 `dist` 目录；若使用 `alias` 或目录仅配置在其他 `location`，需按实际路径配置本块的 `root`。子路径 `/viewer/` 部署时将匹配路径改为 `/viewer/gs-assets/decoder.mjs`。宝塔用户可在“网站 → 对应域名 → 设置 → 配置文件”中修改。

运行 `sudo nginx -t`，成功后执行 `sudo systemctl reload nginx`（面板安装可使用其重载功能）。用 `curl -I https://你的域名/gs-assets/decoder.mjs` 检查类型已变为 `application/javascript` 或 `text/javascript`；同时确认 Worker 为 JavaScript、WASM 为 `application/wasm`，缺失资产不能回退到 HTML。清除 CDN/Service Worker 旧缓存并按 `Ctrl + Shift + R` 刷新后重试。

完整配置与排查流程见仓库 [Web SDK 部署说明](../../docs/SDK-guide.md#12-nginx-的解码资产-mime-配置)。独立模板源码包用户可直接按本节操作。增强解码需要 COOP/COEP；这些头不能修复错误 MIME 或替代 HTTPS。

升级SDK时替换vendor中的tarball、更新package.json文件依赖并重新生成pnpm-lock.yaml；运行unit/typecheck/build及真实浏览器加载/翻转/卸载验收，再发布。两模板故意各自携带相同viewer/style/tests以保持解压即用；维护时同时更新，仓库工具校验一致性。SDK与模板均为preview，附于原生v0.2.2 release，实际来源commit/hash以MANIFEST.json为准。模型和SparkJS不随包分发；第三方许可在安装包资源licenses中，仓库自有代码遵循仓库所有者条款。


## preview.3 默认特性与迁移

模板通过公开 EngineOptions 配置 `decoder: {mode:'auto',threads:4}` 与
`sorting: {mode:'adaptive'}`，并在诊断面板展示实际解码路径和排序状态。
完整递归复制包内 dist/assets，保留 threaded/、manifest.json 与 licenses。
增强启动复用 threaded/decoder.mjs，不需要额外 worker.js。

自动 pthreads 需要安全上下文、crossOriginIsolated、SharedArrayBuffer、
分块路径至少 262,144 点、至少两个硬件线程及足够预算。小模型、SPZ v4、
非隔离或增强初始化受限时使用 single；gzip 和重定位仍为串行。
实际后端以 snapshot.decoder 为准，线程更多不保证更快。

```ts
// App 的固定 options：保留 preview.2 排序/解码行为
decoder: { mode: 'single' as const },
sorting: { mode: 'strict' as const },
```

这些是创建时选项，更改后需卸载并重建。默认 adaptive 仍实时更新投影、
SH 和裁剪，仅在小位移时暂时复用透明度顺序，截图强制刷新；无需隔离也能
使用自适应排序。GPU 耗时属于 gpuFrameId，排序状态属于提交 frameId。

部署后检查 `isSecureContext`、`crossOriginIsolated` 及 SharedArrayBuffer，
实际加载较大 PLY/SPZ 后确认面板显示 pthreads。require-corp 影响第三方
资源，跨源模型/CDN 需正确 CORS/CORP。子 location 声明自己的 add_header
时核验 Nginx 继承，文档与 Worker 响应都需包含隔离头。
旧版本迁移详情：SDK 包的 docs/SDK-migration-preview3.md；
[仓库迁移指南](https://github.com/XJI1234/Native3DGSViewer/blob/codex/web-sdk-preview3/ForWeb/docs/SDK-migration-preview3.md)。

新版源码包内 MANIFEST.json 记录实际模板和 SDK 提交、文件完整性清单；
Release 不附独立哈希文件。Web 包独立于原生 v0.2.2 tag，不表示发布 npm。
