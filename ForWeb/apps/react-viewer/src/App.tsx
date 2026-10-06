import { useEffect, useRef, useState, useSyncExternalStore } from "react";
import { useNative3DGS } from "@native3dgs/web/react";
import { ViewerSession, number, status, diagnostic, time } from "./viewer";
import "./style.css";
const options = {
  decoder: { mode: "auto" as const, threads: 4 },
  sorting: { mode: "adaptive" as const },
  assets: {
    baseUrl: new URL(`${import.meta.env.BASE_URL}gs-assets/`, location.href),
    workerUrl: new URL(
      `${import.meta.env.BASE_URL}gs-assets/decoder.worker.js`,
      location.href,
    ),
  },
};
const empty = {
  name: "",
  candidate: "",
  message: "",
  flipY: false,
  mode: "orbit" as const,
  capturing: false,
  closing: false,
  position: "未提供",
};
const noSubscribe = () => () => {};
export default function App() {
  const host = useRef<HTMLDivElement>(null),
    file = useRef<HTMLInputElement>(null);
  const { engine, snapshot, initializationError } = useNative3DGS(
    host,
    options,
  );
  const [session, setSession] = useState<ViewerSession | null>(null);
  const [url, setUrl] = useState(""),
    [panel, setPanel] = useState(true);
  useEffect(() => {
    if (!engine) {
      setSession(null);
      return;
    }
    const viewer = new ViewerSession(engine);
    setSession(viewer);
    return () => {
      viewer.dispose();
    };
  }, [engine]);
  const ui = useSyncExternalStore(
    session?.subscribe ?? noSubscribe,
    session?.getSnapshot ?? (() => empty),
    () => empty,
  );
  const busy = ["Loading", "Uploading", "Recovering"].includes(snapshot.phase);
  const scene = snapshot.sceneCount > 0,
    ready =
      !!session &&
      !ui.closing &&
      !["Faulted", "Recovering", "Stopping", "Stopped"].includes(
        snapshot.phase,
      );
  const message = diagnostic(
    initializationError,
    snapshot.error,
    snapshot.phase,
    ui.message,
  );
  const progress = snapshot.progress;
  const pick = () => file.current?.click();
  return (
    <div className="shell">
      <header className="header">
        <div className="brand">
          <h1>Native3DGS 查看器</h1>
          <span className="framework">React 模板</span>
        </div>
        <button
          onClick={() => setPanel(!panel)}
          aria-expanded={panel}
          aria-controls="inspector"
        >
          {panel ? "隐藏信息" : "显示信息"}
        </button>
      </header>
      <nav className="toolbar" aria-label="查看器工具">
        <button className="primary" disabled={!ready} onClick={pick}>
          打开模型
        </button>
        <input
          ref={file}
          className="sr-only"
          aria-label="选择模型文件"
          type="file"
          accept=".ply,.spz"
          tabIndex={-1}
          onChange={(event) => {
            const model = event.target.files?.[0];
            if (model) session?.openFile(model);
            event.target.value = "";
          }}
        />
        <button
          disabled={!busy || snapshot.phase === "Recovering"}
          onClick={() => session?.cancel()}
        >
          取消加载
        </button>
        <button
          disabled={!scene || ui.closing}
          onClick={() => {
            void session?.close();
          }}
        >
          关闭模型
        </button>
        <span className="separator" />
        <div className="mode" role="group" aria-label="导航模式">
          <button
            disabled={!ready}
            aria-pressed={ui.mode === "orbit"}
            onClick={() => session?.setMode("orbit")}
          >
            固定
          </button>
          <button
            disabled={!ready}
            aria-pressed={ui.mode === "fly"}
            onClick={() => session?.setMode("fly")}
          >
            自由
          </button>
        </div>
        <button disabled={!ready || !scene} onClick={() => session?.fit()}>
          适配
        </button>
        <button disabled={!ready || !scene} onClick={() => session?.reset()}>
          重置
        </button>
        <button
          disabled={!ready}
          aria-pressed={ui.flipY}
          onClick={() => session?.flip()}
        >
          翻转 Y
        </button>
        <button
          disabled={!ready || !scene}
          aria-label="放大模型"
          onClick={() => session?.zoom(-1)}
        >
          放大
        </button>
        <button
          disabled={!ready || !scene}
          aria-label="缩小模型"
          onClick={() => session?.zoom(1)}
        >
          缩小
        </button>
        <button
          disabled={!ready || !scene || busy || ui.capturing}
          onClick={() => {
            void session?.capture();
          }}
        >
          {ui.capturing ? "正在截图" : "保存截图"}
        </button>
      </nav>
      <main className={`workspace${panel ? "" : " compact"}`}>
        <div
          className="viewport"
          onDragOver={(event) => event.preventDefault()}
          onDrop={(event) => {
            event.preventDefault();
            const model = event.dataTransfer.files[0];
            if (model && ready) session?.openFile(model);
          }}
        >
          <div ref={host} className="host" />
          {!scene && (
            <div className="empty">
              <h2>{busy ? "正在准备模型" : "打开一个三维场景"}</h2>
              <p>
                {busy
                  ? "加载进度显示在信息面板。"
                  : "选择本地 PLY 或 SPZ 文件，也可以将文件拖入视口。"}
              </p>
              {!busy && (
                <button disabled={!ready} onClick={pick}>
                  选择模型文件
                </button>
              )}
            </div>
          )}
          {scene && (
            <p className="scene-caption">
              {ui.name || snapshot.source} ·{" "}
              {ui.flipY ? "Y 轴已翻转" : "原始方向"}
            </p>
          )}
        </div>
        <aside className="panel" id="inspector" hidden={!panel}>
          <section>
            <h2>场景信息</h2>
            <dl className="metrics">
              <dt>状态</dt>
              <dd role="status">
                {status(snapshot.phase, !!engine, !!initializationError)}
              </dd>
              <dt>高斯点数</dt>
              <dd data-testid="count">{number(snapshot.sceneCount)}</dd>
              <dt>球谐阶数</dt>
              <dd>SH {snapshot.degree}</dd>
              <dt>显示方向</dt>
              <dd>{ui.flipY ? "Y 轴镜像" : "未翻转"}</dd>
            </dl>
            {progress && (
              <div className="load-status">
                <p>
                  {ui.candidate || "候选模型"} · {progress.stage}
                </p>
                <progress
                  aria-label="模型加载进度"
                  max={progress.total ?? 1}
                  value={progress.total ? progress.done : undefined}
                />
                <p>
                  {progress.total
                    ? `${Math.min(100, (100 * progress.done) / progress.total).toFixed(0)}%`
                    : "正在处理"}
                </p>
              </div>
            )}
          </section>
          <section>
            <h3>渲染诊断</h3>
            <dl className="metrics">
              <dt>CPU 提交</dt>
              <dd>{time(snapshot.stats?.cpuMs)}</dd>
              <dt>GPU 总帧</dt>
              <dd>{time(snapshot.stats?.gpuMs)}</dd>
              <dt>投影 / 排序</dt>
              <dd>
                {time(snapshot.stats?.projectionMs)} /{" "}
                {time(snapshot.stats?.sortMs)}
              </dd>
              <dt>绘制</dt>
              <dd>{time(snapshot.stats?.drawMs)}</dd>
              <dt>解码后端</dt>
              <dd data-testid="decoder">
                {snapshot.decoder
                  ? `${snapshot.decoder.backend} · ${snapshot.decoder.threads}`
                  : "未加载"}
              </dd>
              <dt>排序策略</dt>
              <dd data-testid="sorting">
                adaptive · {snapshot.stats?.sortReason ?? "尚未提交"}
              </dd>
              <dt>排序年龄</dt>
              <dd>{time(snapshot.stats?.sortAgeMs)}</dd>
              <dt>设备代际</dt>
              <dd>{snapshot.deviceGeneration}</dd>
            </dl>
            <p className="load-status">相机位置（未镜像坐标）</p>
            <output aria-label="相机位置">{ui.position}</output>
          </section>
          <section>
            <form
              onSubmit={(event) => {
                event.preventDefault();
                session?.openUrl(url);
              }}
            >
              <label htmlFor="model-url">远程模型地址</label>
              <input
                id="model-url"
                type="url"
                value={url}
                onChange={(event) => setUrl(event.target.value)}
                placeholder="https://example.com/model.spz"
                required
              />
              <button disabled={!ready}>加载地址</button>
            </form>
          </section>
          <section>
            <h3>操作方式</h3>
            <p>
              {ui.mode === "orbit"
                ? "左键旋转，右键平移，滚轮缩放。"
                : "点击视口后鼠标转向，WASD 移动，Q/E 升降，Shift 加速，Esc 退出。未锁定时可左键拖动转向。"}
            </p>
            <p className="load-status">
              方向键旋转，加减键缩放。Y 翻转后操作方向保持一致。
            </p>
          </section>
          {snapshot.phase === "Faulted" && (
            <section>
              <button
                onClick={() => {
                  void session?.recover();
                }}
              >
                恢复设备
              </button>
            </section>
          )}
        </aside>
      </main>
      {message && (
        <div className="alert" role="alert">
          {message}
        </div>
      )}
      <footer className="footer">
        <span className="model-name">{ui.name || "尚未打开模型"}</span>
        <span>本地模型在浏览器中处理 · WebGPU</span>
      </footer>
    </div>
  );
}
