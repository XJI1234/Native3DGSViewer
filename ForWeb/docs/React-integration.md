# React 19 接入指南

适用React19、TypeScript严格模式、`@native3dgs/web@0.1.0`。浏览器需要实际WebGPU，本轮实测环境为Windows/Edge154/RTX3080。先按[SDK指南](SDK-guide.md)构建本地tgz、安装，并复制全部资产。SDK没有发布到npm，不要直接复制本文包名安装未知公共同名包。

## 1. 依赖与文件布局

宿主需要react/react-dom19、TypeScript、React类型；Vite可作为示例构建器。引擎从`@native3dgs/web`导入类型，hook从`@native3dgs/web/react`导入。无需Three.js、WebGL shim或React状态中的大点云数组。

```text
host/
  public/gs-assets/{decoder.worker.js,decoder.mjs,decoder.wasm,manifest.json,licenses/}
  scripts/copy-gs-assets.mjs
  src/Native3DGSViewer.tsx
  src/App.tsx
```

在dev/build之前运行资产复制脚本；部署子路径时使用宿主的BASE_URL。开发和生产使用同一套显式资产配置，不把库内部new URL的构建产物地址当成通用部署地址。

## 2. 可直接使用的组件

以下组件使用容器ref；hook自己创建/释放Canvas。组件不要额外render第二个Canvas，也不要把引擎加入Redux/序列化状态。

```tsx
import { useEffect, useMemo, useRef, useState } from 'react';
import type { EngineError, Source } from '@native3dgs/web';
import { useNative3DGS } from '@native3dgs/web/react';

type Props = { modelUrl?: string; height?: number };
export function Native3DGSViewer({ modelUrl, height = 600 }: Props) {
  const host = useRef<HTMLDivElement>(null);
  const [file, setFile] = useState<File | null>(null);
  const [loadError, setLoadError] = useState<EngineError | null>(null);
  const cancel = useRef<(() => void) | null>(null);
  const options = useMemo(() => {
    const base = new URL(`${import.meta.env.BASE_URL}gs-assets/`, location.origin);
    return {
      assets: { baseUrl: base, workerUrl: new URL('decoder.worker.js', base) },
      pixelRatio: Math.min(window.devicePixelRatio, 2),
      limits: { inputBytes: 256 * 2 ** 20, gpuBytes: 512 * 2 ** 20 },
    };
  }, []);
  const { engine, snapshot, initializationError } = useNative3DGS(host, options);
  const source = useMemo<Source | null>(() => {
    if (file) return { kind: 'blob', blob: file, name: file.name };
    return modelUrl ? { kind: 'url', url: new URL(modelUrl, location.href).href } : null;
  }, [file, modelUrl]);

  useEffect(() => {
    if (!engine || !source) return;
    let alive = true;
    setLoadError(null);
    const operation = engine.open(source);
    cancel.current = operation.cancel;
    void operation.result.then(result => {
      if (alive && !result.ok && result.error.code !== 'Cancelled') {
        setLoadError(result.error);
      }
    });
    return () => {
      alive = false;
      operation.cancel();
      if (cancel.current === operation.cancel) cancel.current = null;
    };
  }, [engine, source]);

  const failure = initializationError ?? loadError ?? snapshot.error;
  return <section aria-label="3D Gaussian Splatting 查看器">
    <div role="group" aria-label="模型与视角控制">
      <label>打开PLY/SPZ
        <input type="file" accept=".ply,.spz" onChange={event => {
          setFile(event.target.files?.[0] ?? null);
        }} />
      </label>
      <button type="button" onClick={() => cancel.current?.()}>取消加载</button>
      <button type="button" disabled={!engine} onClick={() => engine?.fitScene()}>适配视角</button>
      <button type="button" disabled={!engine} onClick={() => {
        engine?.camera.reset(); engine?.requestFrame();
      }}>重置</button>
      <button type="button" disabled={!engine} onClick={() => {
        cancel.current?.(); void engine?.closeScene();
      }}>关闭场景</button>
    </div>
    <div ref={host} style={{ height, width: '100%', minHeight: 1 }} />
    <p role="status" aria-live="polite">
      {snapshot.phase} · {snapshot.sceneCount.toLocaleString()}点 · SH{snapshot.degree}
      {snapshot.progress && ` · ${snapshot.progress.stage}: ${snapshot.progress.done}`}
    </p>
    {failure && <p role="alert">{failure.code}: {failure.diagnostic}</p>}
  </section>;
}
```

这是客户端组件。若在SSR框架使用，不应在render的useMemo中访问window/location；将options构造移入客户端边界，或用静态绝对资产地址。Next.js页面可用`'use client'`组件加`dynamic(...,{ssr:false})`；是否使用具体Next版本由宿主验证，当前没有Next产品验收证据。纯ESM导入本身SSR安全。

```tsx
// 普通Vite React项目
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { Native3DGSViewer } from './Native3DGSViewer';
createRoot(document.getElementById('root')!).render(
  <StrictMode><Native3DGSViewer modelUrl="/models/site.spz" /></StrictMode>,
);
```

模型URL切换自动取消旧请求；File对象存React state但不复制文件内容。source用useMemo保持身份，避免每次统计更新重新open。加载失败保留旧模型；UI仍显示当前活动sceneCount，错误属于新请求，不应清空视口误导用户。

## 3. Hook语义和StrictMode

`useNative3DGS(hostRef,options)`返回`{engine,snapshot,initializationError}`；engine初始化前为null，snapshot初始Idle，初始化失败单独返回initializationError。useSyncExternalStore稳定订阅，不用每帧setState。GPU资源不会交给React深代理。

每次effect创建自己的Canvas，因此React开发StrictMode的setup/cleanup/setup不会让旧实例夺走下一实例的surface。异步初始化若在cleanup之后完成，会立即dispose。实际tgz开发消费测试确认3次device创建（React重放两次、Vue一次）、最后只有2个活动device；真实WASM解码并卸载后0device/0Canvas且均Stopped。生产模式创建2个device并同样完整释放；生产StrictMode不能代替开发重放验证。本文主组件TSX也从Markdown抽取，和Vue SFC一起经严格类型检查及Vite生产编译。

options仅在第一次mount确定；更改assets、limits或pixelRatio需要用key卸载重建组件。modelUrl/File变化通过open处理，不重建设备。不要把含新对象的hostRef每次重新生成，应useRef。容器必须有明确高度；display:none/零宽高不会提交首帧，加载等待有总timeout。

## 4. 业务扩展

暂停播放/恢复用engine.pause/resume，不改变source。截图在Ready或有稳定场景的Suspended阶段调用：

```ts
const captured = await engine.capture();
if (!captured.ok) {
  showError(captured.error.code, captured.error.diagnostic);
} else {
  const { width, height, rgba } = captured.value;
  const image = new ImageData(Uint8ClampedArray.from(rgba), width, height);
  const canvas = document.createElement('canvas');
  canvas.width = width; canvas.height = height;
  canvas.getContext('2d')!.putImageData(image, 0, 0);
  // canvas.toBlob / 宿主的下载或图库流程
}
```

不要在GPU渲染Canvas上请求2d context。截图结果独立拥有内存，可用于导出，但调用capture不会授权上传用户模型或图片。

通过camera.setPose设置业务相机。camera同步输入错误用try/catch，引擎异步操作检查Result。鼠标轨道会重置roll；若业务需要自由飞行，自己绑定键盘/手柄并调用fly/look，清理时移除这些宿主监听器。本版没有pinch/键盘完整适配。

## 5. 路由、异步竞态与预算

加载effect清理必须cancel，不只是设置alive；alive防止过期UI更新，cancel释放Worker/上传事务。hook卸载负责引擎dispose，宿主不要复用已经Stopped的engine。closeScene与当前open并发会使其返回Cancelled；恢复中的open返回DeviceLost，应等recover完成。

React多个查看器各自占用device/显存。默认每实例GPU512MiB、input256MiB；更高DPR会增加像素填充和截图成本。选择pixelRatio上限属于显式宿主画质策略，基准对照必须记录物理分辨率。大模型ResourceLimit不是网络故障；默认WASM1GiB限制不能仅靠增大gpuBytes解除。

## 6. 上线前核验与排障

| 症状 | 核验 |
| --- | --- |
| initializationError UnsupportedCapability | 安全上下文、浏览器策略、adapter/device及实际硬件 |
| Worker报错/空diagnostic | workerUrl请求是不是JS而非SPA fallback HTML，是否同源/CSP允许 |
| wasm fetch/compile失败 | baseUrl、application/wasm MIME、完整资产版本、CORS/CSP |
| 一直Uploading后Timeout | 容器CSS宽高是否0，标签页/路由容器是否隐藏 |
| ResourceLimit但其他页面能开 | 本实例limits、旧模型事务峰值、WASM估计峰值，避免无限重试 |
| 不断重新加载 | source/options身份是否每次render变化，effect依赖是否正确 |
| 卸载后回调更新 | effect的alive/token和cancel是否齐全 |
| 黑屏Faulted | snapshot.error、deviceGeneration和recover结果；不要吞GPU错误 |

宿主应测试SPA重复进出、StrictMode、模型快速切换、取消、零尺寸/恢复、模型错误、真实生产URL和实际CSP。已实测tgz/Vite的结果不能替代宿主自己的部署验证。证据见[实现验收](verification/implementation-report.md)。
