import type {
  EngineError,
  LoadOperation,
  Result,
  Source,
  WebEngine,
} from "@native3dgs/web";

export interface ViewerState {
  name: string;
  candidate: string;
  message: string;
  flipY: boolean;
  mode: "orbit" | "fly";
  capturing: boolean;
  closing: boolean;
  position: string;
}
/** Host concerns only. SDK owns loading, rendering, cancellation and recovery. */
export class ViewerSession {
  private state: ViewerState = {
    name: "",
    candidate: "",
    message: "",
    flipY: false,
    mode: "orbit",
    capturing: false,
    closing: false,
    position: "未提供",
  };
  private listeners = new Set<() => void>();
  private operation: LoadOperation | undefined;
  private alive = true;
  private epoch = 0;
  private cameraTimer: ReturnType<typeof setInterval>;
  constructor(readonly engine: WebEngine) {
    this.cameraTimer = setInterval(() => {
      const position = engine.camera
        .getPose()
        .position.map((v) => v.toFixed(3))
        .join(", ");
      if (position !== this.state.position) this.publish({ position });
    }, 100);
  }
  getSnapshot = (): ViewerState => this.state;
  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };
  private publish(patch: Partial<ViewerState>): void {
    if (!this.alive) return;
    this.state = { ...this.state, ...patch };
    this.listeners.forEach((listener) => {
      listener();
    });
  }
  private failure(error: EngineError): void {
    if (error.code !== "Cancelled")
      this.publish({
        message: `${error.code} · ${error.stage}: ${error.diagnostic}`,
      });
  }
  private result(result: Result<unknown>): void {
    if (!result.ok) this.failure(result.error);
    else this.publish({ message: "" });
  }
  private async open(source: Source, name: string): Promise<void> {
    if (!this.alive || this.state.closing) return;
    const epoch = ++this.epoch;
    this.publish({ candidate: name, message: "" });
    const operation = this.engine.open(source);
    this.operation = operation;
    const result = await operation.result;
    if (!this.alive || epoch !== this.epoch) return;
    this.operation = undefined;
    this.publish({ candidate: "", ...(result.ok ? { name } : {}) });
    this.result(result);
  }
  openFile(file: File): void {
    if (!/\.(ply|spz)$/i.test(file.name)) {
      this.publish({ message: "请选择 PLY 或 SPZ 模型文件。" });
      return;
    }
    void this.open({ kind: "blob", blob: file, name: file.name }, file.name);
  }
  openUrl(text: string): void {
    try {
      const url = new URL(text);
      if (!["https:", "http:"].includes(url.protocol))
        throw Error("请使用 HTTP(S) 模型地址。");
      void this.open(
        { kind: "url", url: url.href },
        decodeURIComponent(url.pathname.split("/").pop() || "远程模型"),
      );
    } catch (error) {
      this.publish({
        message: error instanceof Error ? error.message : String(error),
      });
    }
  }
  cancel(): void {
    this.operation?.cancel();
  }
  flip(): void {
    this.engine.camera.setFlipY(!this.engine.camera.flipY);
    this.engine.requestFrame();
    this.publish({ flipY: this.engine.camera.flipY });
  }
  setMode(mode: "orbit" | "fly"): void {
    this.engine.camera.setMode(mode);
    this.publish({ mode });
  }
  fit(): void {
    this.result(this.engine.fitScene());
  }
  reset(): void {
    this.engine.camera.reset();
    this.engine.requestFrame();
  }
  zoom(steps: number): void {
    this.engine.camera.dolly(steps);
    this.engine.requestFrame();
  }
  async close(): Promise<void> {
    if (!this.alive || this.state.closing) return;
    ++this.epoch;
    this.operation?.cancel();
    this.operation = undefined;
    this.publish({ closing: true, candidate: "", message: "" });
    try {
      await this.engine.closeScene();
      this.publish({ name: "" });
    } catch (error) {
      this.publish({ message: String(error) });
    } finally {
      this.publish({ closing: false });
    }
  }
  async recover(): Promise<void> {
    this.result(await this.engine.recover());
  }
  async capture(): Promise<void> {
    if (!this.alive || this.state.capturing) return;
    this.publish({ capturing: true, message: "" });
    try {
      const result = await this.engine.capture();
      if (!this.alive) return;
      if (!result.ok) {
        this.failure(result.error);
        return;
      }
      const { width, height, rgba } = result.value;
      const canvas = document.createElement("canvas");
      canvas.width = width;
      canvas.height = height;
      const context = canvas.getContext("2d");
      if (!context) throw Error("无法创建截图画布。");
      context.putImageData(
        new ImageData(new Uint8ClampedArray(rgba), width, height),
        0,
        0,
      );
      const blob = await new Promise<Blob>((resolve, reject) =>
        canvas.toBlob(
          (blob) => (blob ? resolve(blob) : reject(Error("PNG 编码失败。"))),
          "image/png",
        ),
      );
      if (!this.alive) return;
      const url = URL.createObjectURL(blob),
        anchor = document.createElement("a");
      anchor.href = url;
      anchor.download = "Native3DGS-view.png";
      anchor.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    } catch (error) {
      this.publish({ message: String(error) });
    } finally {
      this.publish({ capturing: false });
    }
  }
  dispose(): void {
    this.alive = false;
    clearInterval(this.cameraTimer);
    ++this.epoch;
    this.operation?.cancel();
    this.listeners.clear();
  }
}
export const phases: Record<string, string> = {
  Idle: "等待打开",
  Loading: "正在加载",
  Uploading: "正在上传",
  Ready: "可以浏览",
  Suspended: "渲染已暂停",
  Recovering: "正在恢复设备",
  Faulted: "设备异常",
  Stopping: "正在释放",
  Stopped: "已停止",
};
export const number = (value: number): string =>
  new Intl.NumberFormat("zh-CN").format(value);
export const time = (value: number | null | undefined): string =>
  value === null || value === undefined ? "未提供" : `${value.toFixed(2)} ms`;

export function diagnostic(
  initialization: EngineError | null,
  sdk: EngineError | null,
  phase: string,
  hostMessage: string,
): string {
  if (initialization)
    return `${initialization.code}: ${initialization.diagnostic}`;
  if (phase === "Faulted" && sdk) return `${sdk.code}: ${sdk.diagnostic}`;
  if (hostMessage) return hostMessage;
  if (sdk && sdk.code !== "Cancelled") return `${sdk.code}: ${sdk.diagnostic}`;
  return "";
}
export function status(
  phase: string,
  initialized: boolean,
  failed: boolean,
): string {
  if (!initialized && failed) return "初始化失败";
  if (!initialized) return "正在初始化";
  return phases[phase] ?? phase;
}
