# Web SDK 独立查看器模板

[React 模板](react-viewer/README.md)与[Vue 模板](vue-viewer/README.md)均消费固定的已安装SDK包，使用公开的生命周期、相机、事务和截图接口。它们是两个可独立解压安装的工程，无相对仓库源码依赖。共同viewer/style/test文件在各工程内保留，发布工具及审查要求两份同时维护。

## 工作流

在ForWeb目录执行：

```powershell
pnpm --dir apps/react-viewer install --frozen-lockfile --ignore-scripts
pnpm --dir apps/vue-viewer install --frozen-lockfile --ignore-scripts
pnpm --dir apps/react-viewer test
pnpm --dir apps/vue-viewer test
pnpm --dir apps/react-viewer format:check
pnpm --dir apps/vue-viewer format:check
pnpm --dir apps/react-viewer build
pnpm --dir apps/vue-viewer build
$env:GS_MODEL_ROOT = '<包含 changjin_v1.ply 和 spz/shengyi_v1.spz 的模型目录>'
node tools/template-tests.mjs
node tools/record-template-build.mjs
```

真实浏览器验收使用Edge/WebGPU，串行验证两个框架的production/development。包含非对称自生成fixture的Y镜像像素、同方向旋转/平移、Pointer Lock、WASD、截图内容、迟到取消、失败保留旧模型、注入validation error后设备恢复画面，以及真实PLY/SPZ与四档宽度。注入故障不是硬件物理丢失。外部模型不进入仓库或模板源码包。

记录源文件、锁文件、vendored SDK和完整dist哈希后，审查、提交模板，再运行`node tools/package-templates.mjs`。打包要求clean committed source与完整构建inventory一致，校验SDK release archive及模板内SDK tarball完全相同，并只打包源码allowlist。源码包带MANIFEST.json与sha256文件，标明模板commit、实际SDKcommit和旧native release tag。

SDK: `Native3DGS-SDK-0.2.2-Web-WebGPU-preview.2.zip`；模板：`Native3DGS-Template-0.2.2-Web-React-preview.2.zip`、`Native3DGS-Template-0.2.2-Web-Vue-preview.2.zip`。保留原生v0.2.2所有已有资产。发布前将两份源码包分别解压到干净目录并使用frozen lockfile安装、测试和构建。
