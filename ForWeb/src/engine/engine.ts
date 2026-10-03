import { decode } from '../model-io/loader';
import type { FrameStats, GpuScene } from '../render-core/renderer';
import { Renderer } from '../render-core/renderer';
import type { EngineError, ErrorCode, Limits, Progress, Result, Scene, Source } from '../splat-types/index';
import { defaultLimits, error, validateLimits } from '../splat-types/index';
import { Camera } from './camera';
export interface Snapshot {
    readonly phase:
        | 'Idle'
        | 'Loading'
        | 'Uploading'
        | 'Ready'
        | 'Suspended'
        | 'Recovering'
        | 'Faulted'
        | 'Stopping'
        | 'Stopped';
    readonly requestId: number;
    readonly sceneCount: number;
    readonly degree: number;
    readonly source: string | null;
    readonly progress: Progress | null;
    readonly error: EngineError | null;
    readonly stats: FrameStats | null;
    readonly deviceGeneration: number;
    readonly viewportRevision: number;
}
export interface EngineOptions {
    canvas: HTMLCanvasElement;
    assets?: { baseUrl: URL | string; workerUrl?: URL | string };
    limits?: Partial<Limits>;
}
export interface LoadOperation {
    readonly requestId: number;
    readonly result: Promise<Result<void>>;
    cancel(): void;
}
export class WebEngine {
    readonly camera = new Camera();
    private active: GpuScene | undefined;
    private pending: AbortController | undefined;
    private requestId = 0;
    private generation = 1;
    private revision = 0;
    private dirty = true;
    private raf = 0;
    private stopped = false;
    private disposal: Promise<void> | undefined;
    private snapshot: Snapshot = Object.freeze({
        phase: 'Idle',
        requestId: 0,
        sceneCount: 0,
        degree: 0,
        source: null,
        progress: null,
        error: null,
        stats: null,
        deviceGeneration: 1,
        viewportRevision: 0,
    });
    private observers = new Set<() => void>();
    private lastPublish = 0;
    private paused = false;
    private uploadBarrier: Promise<void> = Promise.resolve();
    private recovery: Promise<Result<void>> | undefined;
    private retainedScene: Scene | undefined;
    private sceneEpoch = 0;
    private readonly limits: Limits;
    private readonly assets: string;
    private constructor(
        private renderer: Renderer,
        private options: EngineOptions,
    ) {
        this.limits = { ...defaultLimits, ...options.limits };
        this.assets = new URL(
            String(options.assets?.baseUrl ?? new URL(/* @vite-ignore */ './assets/', import.meta.url)),
            location.href,
        ).href;
        this.watchDevice();
        this.schedule();
    }
    static async create(options: EngineOptions): Promise<Result<WebEngine>> {
        let renderer: Renderer | undefined;
        try {
            const limits = { ...defaultLimits, ...options.limits };
            validateLimits(limits);
            if (!options.canvas) throw Error('InvalidInput: canvas');
            renderer = await Renderer.create(options.canvas, limits.gpuBytes);
            return { ok: true, value: new WebEngine(renderer, options) };
        } catch (reason) {
            await renderer?.dispose();
            return {
                ok: false,
                error: error(
                    String(reason).includes('UnsupportedCapability')
                        ? 'UnsupportedCapability'
                        : 'InvalidInput',
                    'Initializing',
                    reason,
                ),
            };
        }
    }
    get capabilities(): { adapter: GPUAdapterInfo; pageBytes: number; execution: 'main' } {
        return { adapter: this.renderer.info, pageBytes: this.renderer.pageBytes, execution: 'main' };
    }
    getSnapshot = (): Snapshot => this.snapshot;
    subscribe = (observer: () => void): (() => void) => {
        if (this.stopped) return () => {};
        this.observers.add(observer);
        return () => this.observers.delete(observer);
    };
    private publish(patch: Partial<Snapshot>): void {
        const next = { ...this.snapshot, ...patch };
        this.snapshot = Object.freeze({
            ...next,
            progress: next.progress ? Object.freeze({ ...next.progress }) : null,
            error: next.error ? Object.freeze({ ...next.error }) : null,
            stats: next.stats ? Object.freeze({ ...next.stats }) : null,
        });
        for (const observer of this.observers) {
            try {
                observer();
            } catch {
                /* Host observers cannot abort another subscriber or GPU work. */
            }
        }
    }
    private restingPhase(): Snapshot['phase'] {
        return this.paused || !this.options.canvas.width || !this.options.canvas.height
            ? 'Suspended'
            : this.active
              ? 'Ready'
              : 'Idle';
    }
    private sceneMetadata(): Pick<Snapshot, 'sceneCount' | 'degree' | 'source'> {
        const scene = this.active?.scene ?? this.retainedScene;
        return { sceneCount: scene?.count ?? 0, degree: scene?.degree ?? 0, source: scene?.source ?? null };
    }
    private surfacePhase(): Snapshot['phase'] {
        return ['Loading', 'Uploading', 'Recovering', 'Faulted'].includes(this.snapshot.phase)
            ? this.snapshot.phase
            : this.restingPhase();
    }
    private schedule(): void {
        if (!this.stopped && !this.raf) this.raf = requestAnimationFrame(() => this.tick());
    }
    private tick(): void {
        this.raf = 0;
        if (this.stopped) return;
        if (
            !this.paused &&
            this.active &&
            this.options.canvas.width &&
            this.options.canvas.height &&
            !document.hidden &&
            this.snapshot.phase !== 'Recovering' &&
            this.snapshot.phase !== 'Faulted'
        ) {
            if (this.dirty || this.active.revision !== this.camera.revision + this.revision) {
                try {
                    const stats = this.draw(this.active);
                    this.dirty = false;
                    if (performance.now() - this.lastPublish > 250) {
                        this.lastPublish = performance.now();
                        this.publish({ stats });
                    }
                } catch (reason) {
                    this.publish({ phase: 'Faulted', error: error('DeviceLost', 'Render', reason) });
                }
            }
        }
        this.schedule();
    }
    private draw(scene: GpuScene): FrameStats {
        return this.renderer.render(
            scene,
            this.camera.frame(
                scene.scene,
                this.options.canvas.width,
                this.options.canvas.height,
                scene.scene.count,
                scene.scene.degree,
                scene.scene.stride,
                scene.scene.pageCapacity,
            ),
            this.camera.revision + this.revision,
        );
    }
    resize(width: number, height: number): Result<void> {
        try {
            if (this.stopped) throw Error('Stopped');
            if (
                !Number.isInteger(width) ||
                !Number.isInteger(height) ||
                width < 0 ||
                height < 0 ||
                width > this.renderer.device.limits.maxTextureDimension2D ||
                height > this.renderer.device.limits.maxTextureDimension2D
            )
                throw Error('Invalid viewport');
            this.options.canvas.width = width;
            this.options.canvas.height = height;
            this.camera.resize(width, height);
            this.revision++;
            this.dirty = true;
            this.publish({
                viewportRevision: this.revision,
                phase: this.surfacePhase(),
            });
            return { ok: true, value: undefined };
        } catch (reason) {
            return { ok: false, error: error(this.stopped ? 'Stopped' : 'InvalidInput', 'Resize', reason) };
        }
    }
    open(source: Source): LoadOperation {
        const requestId = ++this.requestId;
        this.pending?.abort();
        const controller = new AbortController();
        this.pending = controller;
        const result = this.load(source, requestId, controller);
        return { requestId, result, cancel: () => controller.abort() };
    }
    pause(): void {
        this.paused = true;
        if (!this.stopped) this.publish({ phase: this.surfacePhase() });
    }
    resume(): void {
        if (this.stopped) return;
        this.paused = false;
        this.dirty = true;
        this.publish({ phase: this.surfacePhase() });
    }
    fitScene(): Result<void> {
        try {
            if (!this.active) throw Error('No active scene');
            this.camera.fit(this.active.scene, this.options.canvas.width, this.options.canvas.height);
            this.dirty = true;
            return { ok: true, value: undefined };
        } catch (reason) {
            return { ok: false, error: error('InvalidInput', 'Camera', reason) };
        }
    }
    private async load(source: Source, id: number, controller: AbortController): Promise<Result<void>> {
        let uploaded: GpuScene | undefined;
        const deadline = performance.now() + this.limits.timeoutMs;
        const owner = this.renderer;
        const timeout = setTimeout(() => controller.abort('Timeout'), this.limits.timeoutMs);
        const waitViewport = async () => {
            while (!this.options.canvas.width || !this.options.canvas.height) {
                if (controller.signal.aborted || id !== this.requestId || this.stopped)
                    throw Error('Cancelled');
                if (performance.now() >= deadline) throw Error('Timeout: zero viewport');
                await new Promise<void>((resolve) => setTimeout(resolve, 16));
            }
            if (controller.signal.aborted || id !== this.requestId || this.stopped) throw Error('Cancelled');
        };
        try {
            if (this.stopped) throw Error('Stopped');
            if (this.recovery || this.snapshot.phase === 'Recovering' || this.snapshot.phase === 'Faulted')
                throw Error('DeviceLost: recovery in progress');
            this.publish({
                phase: 'Loading',
                requestId: id,
                error: null,
                progress: { stage: 'Inspecting', done: 0, total: null },
            });
            const scene = await decode(
                source,
                this.limits,
                this.assets,
                this.renderer.pageBytes,
                controller.signal,
                (p) => {
                    if (id === this.requestId) this.publish({ progress: p });
                },
                this.renderer.retainedBytes,
                this.options.assets?.workerUrl,
            );
            if (controller.signal.aborted || id !== this.requestId) throw Error('Cancelled');
            this.publish({ phase: 'Uploading' });
            const previous = this.uploadBarrier;
            let releaseBarrier!: () => void;
            this.uploadBarrier = new Promise<void>((resolve) => {
                releaseBarrier = resolve;
            });
            await previous;
            try {
                if (controller.signal.aborted || id !== this.requestId || this.stopped)
                    throw Error('Cancelled');
                await waitViewport();
                uploaded = await this.renderer.upload(
                    scene,
                    this.active?.bytes ?? 0,
                    controller.signal,
                    (done, total) => {
                        if (id === this.requestId)
                            this.publish({ progress: { stage: 'Uploading', done, total } });
                    },
                );
                if (controller.signal.aborted || id !== this.requestId || this.stopped)
                    throw Error('Cancelled');
                // A replacement must have a validated frame for the current nonzero surface.
                for (let attempt = 0; ; attempt++) {
                    await waitViewport();
                    const width = this.options.canvas.width,
                        height = this.options.canvas.height;
                    const revision = this.revision;
                    const camera = new Camera();
                    camera.fit(scene, width, height);
                    const firstFrame = camera.frame(
                        scene,
                        width,
                        height,
                        scene.count,
                        scene.degree,
                        scene.stride,
                        scene.pageCapacity,
                    );
                    this.renderer.device.pushErrorScope('validation');
                    let validation: GPUError | null = null;
                    try {
                        this.renderer.render(uploaded, firstFrame, -2 - attempt);
                        await this.renderer.device.queue.onSubmittedWorkDone();
                    } finally {
                        validation = await this.renderer.device.popErrorScope();
                    }
                    if (validation) throw Error(validation.message);
                    if (controller.signal.aborted || id !== this.requestId || this.stopped)
                        throw Error('Cancelled');
                    if (
                        revision === this.revision &&
                        width === this.options.canvas.width &&
                        height === this.options.canvas.height
                    )
                        break;
                }
                const old = this.active;
                this.sceneEpoch++;
                this.retainedScene = undefined;
                this.active = uploaded;
                uploaded = undefined;
                this.camera.fit(
                    scene,
                    Math.max(1, this.options.canvas.width),
                    Math.max(1, this.options.canvas.height),
                );
                this.dirty = true;
                if (old) this.renderer.release(old);
                this.publish({
                    phase: this.restingPhase(),
                    sceneCount: scene.count,
                    degree: scene.degree,
                    source: scene.source,
                    progress: null,
                    error: null,
                    stats: null,
                });
                return { ok: true, value: undefined };
            } finally {
                releaseBarrier();
            }
        } catch (reason) {
            if (uploaded) owner.release(uploaded);
            const message = controller.signal.reason === 'Timeout' ? 'Timeout' : String(reason);
            const codes: ErrorCode[] = [
                'UnsupportedCapability',
                'UnsupportedFormat',
                'InvalidInput',
                'OutOfMemory',
                'DeviceLost',
            ];
            const code = message.includes('Cancelled')
                ? 'Cancelled'
                : message.includes('ResourceLimit')
                  ? 'ResourceLimit'
                  : message.includes('Timeout')
                    ? 'Timeout'
                    : message.includes('Stopped')
                      ? 'Stopped'
                      : message.includes('NetworkFailure')
                        ? 'NetworkFailure'
                        : (codes.find((value) => message.includes(value)) ?? 'DecoderFailure');
            const failure = error(code, 'Load', reason);
            if (id === this.requestId && !this.stopped) {
                this.dirty = true;
                this.publish({
                    phase: this.recovery
                        ? 'Recovering'
                        : this.snapshot.phase === 'Faulted'
                          ? 'Faulted'
                          : this.restingPhase(),
                    error: failure,
                    progress: null,
                });
            }
            return { ok: false, error: failure };
        } finally {
            clearTimeout(timeout);
            if (this.pending === controller) this.pending = undefined;
        }
    }
    async closeScene(): Promise<void> {
        if (this.stopped) return;
        const id = ++this.requestId;
        ++this.sceneEpoch;
        this.retainedScene = undefined;
        this.pending?.abort();
        this.pending = undefined;
        const active = this.active;
        this.active = undefined;
        const owner = this.renderer;
        owner.clear();
        const closed = {
            phase: this.recovery
                ? ('Recovering' as const)
                : this.snapshot.phase === 'Faulted'
                  ? ('Faulted' as const)
                  : this.restingPhase(),
            requestId: id,
            sceneCount: 0,
            degree: 0,
            source: null,
            progress: null,
            stats: null,
            error: this.snapshot.phase === 'Faulted' ? this.snapshot.error : null,
        };
        this.publish(closed);
        if (active) {
            await owner.device.queue.onSubmittedWorkDone().catch(() => {});
            owner.release(active);
        }
    }
    requestFrame(): void {
        this.dirty = true;
    }
    private watchDevice(): void {
        const renderer = this.renderer;
        renderer.device.addEventListener('uncapturederror', (event) => {
            if (!this.stopped && this.renderer === renderer)
                this.publish({
                    phase: 'Faulted',
                    error: error(
                        event.error instanceof GPUOutOfMemoryError ? 'OutOfMemory' : 'DeviceLost',
                        'Render',
                        event.error.message,
                    ),
                });
        });
        void renderer.device.lost.then((info) => {
            if (!this.stopped && info.reason !== 'destroyed' && this.renderer === renderer)
                void this.recover();
        });
    }
    recover(): Promise<Result<void>> {
        if (this.recovery) return this.recovery;
        let complete!: (value: Result<void>) => void;
        const result = new Promise<Result<void>>((resolve) => {
            complete = resolve;
        });
        this.recovery = result;
        void this.recoverInternal().then(complete, (reason) =>
            complete({ ok: false, error: error('DeviceLost', 'Recovery', reason) }),
        );
        void result.finally(() => {
            if (this.recovery === result) this.recovery = undefined;
        });
        return result;
    }
    private async recoverInternal(): Promise<Result<void>> {
        if (this.stopped) return { ok: false, error: error('Stopped', 'Recovery', 'Stopped') };
        this.pending?.abort();
        ++this.requestId;
        this.publish({ phase: 'Recovering', requestId: this.requestId, progress: null, stats: null });
        await this.uploadBarrier;
        if (this.stopped) return { ok: false, error: error('Stopped', 'Recovery', 'Stopped') };
        const oldRenderer = this.renderer;
        const scene = this.active?.scene ?? this.retainedScene;
        const epoch = this.sceneEpoch;
        this.retainedScene = scene;
        this.active = undefined;
        for (let attempt = 0; attempt < 2; attempt++) {
            let replacement: Renderer | undefined;
            try {
                await oldRenderer.dispose();
                replacement = await Renderer.create(this.options.canvas, this.limits.gpuBytes);
                if (this.stopped) {
                    await replacement.dispose();
                    throw Error('Stopped');
                }
                const active = scene
                    ? await replacement.upload(scene, 0, new AbortController().signal, () => {})
                    : undefined;
                if (this.stopped) throw Error('Stopped');
                if (epoch !== this.sceneEpoch && active) replacement.release(active);
                this.renderer = replacement;
                this.active = epoch === this.sceneEpoch ? active : undefined;
                this.generation++;
                this.dirty = true;
                this.watchDevice();
                if (this.active && this.options.canvas.width && this.options.canvas.height) {
                    this.draw(this.active);
                    await replacement.device.queue.onSubmittedWorkDone();
                }
                if (this.stopped) throw Error('Stopped');
                this.retainedScene = undefined;
                this.publish({
                    phase: this.restingPhase(),
                    deviceGeneration: this.generation,
                    ...this.sceneMetadata(),
                    error: null,
                });
                return { ok: true, value: undefined };
            } catch (reason) {
                await replacement?.dispose();
                if (this.renderer === replacement) this.active = undefined;
                if (attempt === 1 || this.stopped) {
                    const failure = error('DeviceLost', 'Recovery', reason);
                    if (!this.stopped)
                        this.publish({ phase: 'Faulted', error: failure, ...this.sceneMetadata() });
                    return { ok: false, error: failure };
                }
            }
        }
        return { ok: false, error: error('DeviceLost', 'Recovery', 'Unable to recover') };
    }
    dispose(): Promise<void> {
        if (this.disposal) return this.disposal;
        this.stopped = true;
        ++this.sceneEpoch;
        ++this.requestId;
        this.pending?.abort();
        this.retainedScene = undefined;
        cancelAnimationFrame(this.raf);
        this.disposal = Promise.resolve().then(async () => {
            const active = this.active;
            this.active = undefined;
            await this.uploadBarrier;
            await this.recovery;
            await this.renderer.dispose();
            if (active) this.renderer.release(active);
            this.publish({
                phase: 'Stopped',
                sceneCount: 0,
                degree: 0,
                source: null,
                progress: null,
                stats: null,
            });
            this.observers.clear();
        });
        this.publish({ phase: 'Stopping', requestId: this.requestId });
        return this.disposal;
    }
    async capture(): Promise<Result<{ width: number; height: number; rgba: Uint8Array }>> {
        try {
            if (this.stopped) throw Error('Stopped');
            if (!this.active || this.recovery || this.pending) throw Error('InvalidInput: no settled scene');
            const renderer = this.renderer,
                scene = this.active;
            const width = this.options.canvas.width,
                height = this.options.canvas.height;
            const rgba = await renderer.capture(
                scene,
                this.camera.frame(
                    scene.scene,
                    width,
                    height,
                    scene.scene.count,
                    scene.scene.degree,
                    scene.scene.stride,
                    scene.scene.pageCapacity,
                ),
                this.camera.revision + this.revision,
            );
            if (this.stopped || this.active !== scene || this.renderer !== renderer) throw Error('Cancelled');
            return { ok: true, value: { width, height, rgba } };
        } catch (reason) {
            const message = String(reason);
            return {
                ok: false,
                error: error(
                    this.stopped
                        ? 'Stopped'
                        : message.includes('Cancelled')
                          ? 'Cancelled'
                          : message.includes('ResourceLimit')
                            ? 'ResourceLimit'
                            : 'InvalidInput',
                    'Capture',
                    reason,
                ),
            };
        }
    }
}
