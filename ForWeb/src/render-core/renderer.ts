import type { Scene } from '../splat-types/index';
import { drawing, projection } from './shaders';
import { GpuSort, testSort } from './sort';
export interface FrameStats {
    readonly cpuMs: number;
    readonly gpuMs: number | null;
    readonly gpuFrameId: number | null;
    readonly frameId: number;
    readonly sorted: boolean;
}
export interface GpuScene {
    scene: Scene;
    pages: GPUBuffer[];
    ellipse: GPUBuffer;
    a: GPUBuffer;
    b: GPUBuffer;
    args: GPUBuffer;
    sort: GpuSort;
    projectGroup: GPUBindGroup;
    drawGroup: GPUBindGroup;
    bytes: number;
    revision: number;
}
export class Renderer {
    readonly pageBytes: number;
    readonly info: GPUAdapterInfo;
    private frame: GPUBuffer;
    private project: GPUComputePipeline;
    private draw: GPURenderPipeline;
    private context: GPUCanvasContext;
    private configured = false;
    private format: GPUTextureFormat;
    private dummy: GPUBuffer;
    private frameId = 0;
    private stopped = false;
    private query: GPUQuerySet | undefined;
    private queryResolve: GPUBuffer | undefined;
    private queryRead: GPUBuffer | undefined;
    private queryBusy = false;
    private lastGpu: number | null = null;
    private lastGpuFrame: number | null = null;
    private timedScene: GpuScene | undefined;
    private captureBytes = 0;
    private residentBytes = 0;
    private cpuResidentBytes = 0;
    private ownedScenes = new Set<GpuScene>();
    get retainedBytes(): number {
        return this.cpuResidentBytes;
    }
    private baseBytes = 384;
    private constructor(
        readonly device: GPUDevice,
        adapter: GPUAdapter,
        readonly canvas: HTMLCanvasElement,
        private budget: number,
    ) {
        this.info = adapter.info;
        this.pageBytes = Math.min(device.limits.maxStorageBufferBindingSize, 128 * 2 ** 20);
        this.context = canvas.getContext('webgpu')!;
        if (!this.context) throw Error('UnsupportedCapability: Canvas context');
        this.format = navigator.gpu.getPreferredCanvasFormat();
        if (canvas.width && canvas.height) this.configureSurface();
        this.frame = device.createBuffer({
            size: 128,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        this.dummy = device.createBuffer({ size: 256, usage: GPUBufferUsage.STORAGE });
        this.project = device.createComputePipeline({
            layout: 'auto',
            compute: { module: device.createShaderModule({ code: projection }), entryPoint: 'project' },
        });
        const module = device.createShaderModule({ code: drawing });
        this.draw = device.createRenderPipeline({
            layout: 'auto',
            vertex: { module, entryPoint: 'vertex' },
            fragment: {
                module,
                entryPoint: 'fragment',
                targets: [
                    {
                        format: this.format,
                        blend: {
                            color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' },
                            alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' },
                        },
                    },
                ],
            },
            primitive: { topology: 'triangle-strip' },
        });
        if (device.features.has('timestamp-query')) {
            this.baseBytes += 32;
            this.query = device.createQuerySet({ type: 'timestamp', count: 2 });
            this.queryResolve = device.createBuffer({
                size: 16,
                usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
            });
            this.queryRead = device.createBuffer({
                size: 16,
                usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
            });
        }
    }
    private configureSurface(): void {
        if (this.configured) return;
        this.context.configure({
            device: this.device,
            format: this.format,
            alphaMode: 'premultiplied',
            colorSpace: 'srgb',
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
        });
        this.configured = true;
    }
    static async create(canvas: HTMLCanvasElement, budget: number): Promise<Renderer> {
        if (!navigator.gpu) throw Error('UnsupportedCapability: WebGPU');
        const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
        if (!adapter) throw Error('UnsupportedCapability: adapter');
        const device = await adapter.requestDevice({
            requiredFeatures: adapter.features.has('timestamp-query') ? ['timestamp-query'] : [],
        });
        device.pushErrorScope('validation');
        try {
            const renderer = new Renderer(device, adapter, canvas, budget);
            const sorted = await testSort(device, new Uint32Array([7, 1, 7, 0, 0xffffffff, 3]));
            const e = await device.popErrorScope();
            if (e) throw Error(e.message);
            const expected = [3, 1, 5, 0, 2, 4];
            if (expected.some((v, i) => sorted[2 * i + 1] !== v))
                throw Error(`GPU stable sort self-test failed: ${Array.from(sorted)}`);
            return renderer;
        } catch (reason) {
            canvas.getContext('webgpu')?.unconfigure();
            device.destroy();
            throw reason;
        }
    }
    async upload(
        scene: Scene,
        oldBytes: number,
        signal: AbortSignal,
        onProgress: (done: number, total: number) => void,
    ): Promise<GpuScene> {
        const n = scene.count,
            groups = Math.ceil(n / 256);
        const bytes =
            scene.pages.reduce((a, b) => a + b.byteLength, 0) +
            Math.max(16, n * 48) +
            2 * Math.max(16, n * 8) +
            16 +
            GpuSort.byteSize(n);
        if (
            bytes + Math.max(oldBytes, this.residentBytes) + this.captureBytes + this.baseBytes >
                this.budget ||
            n * 48 > this.device.limits.maxStorageBufferBindingSize ||
            groups > this.device.limits.maxComputeWorkgroupsPerDimension
        )
            throw Error('ResourceLimit: GPU scene/transaction budget');
        const resources: GPUBuffer[] = [];
        let sort: GpuSort | undefined;
        let scopesOpen = true;
        this.device.pushErrorScope('out-of-memory');
        this.device.pushErrorScope('validation');
        const make = (size: number, usage: number) => {
            const b = this.device.createBuffer({ size: Math.max(16, size), usage });
            resources.push(b);
            return b;
        };
        this.residentBytes += bytes;
        const cpuBytes = scene.pages.reduce((sum, page) => sum + page.byteLength, 0);
        this.cpuResidentBytes += cpuBytes;
        try {
            const pages: GPUBuffer[] = [];
            const total = scene.pages.reduce((a, b) => a + b.byteLength, 0);
            let done = 0;
            for (const page of scene.pages) {
                const b = make(page.byteLength, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
                pages.push(b);
                for (let offset = 0; offset < page.byteLength; offset += 4 * 2 ** 20) {
                    if (signal.aborted) throw Error('Cancelled');
                    const length = Math.min(4 * 2 ** 20, page.byteLength - offset);
                    this.device.queue.writeBuffer(b, offset, page, offset, length);
                    done += length;
                    onProgress(done, total);
                    await new Promise<void>((resolve) => setTimeout(resolve, 0));
                }
            }
            const ellipse = make(n * 48, GPUBufferUsage.STORAGE),
                a = make(n * 8, GPUBufferUsage.STORAGE),
                b = make(n * 8, GPUBufferUsage.STORAGE),
                args = make(16, GPUBufferUsage.STORAGE | GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_DST);
            sort = new GpuSort(this.device, n);
            const projectBuffers = [
                ...Array.from({ length: 4 }, (_, i) => pages[i] ?? this.dummy),
                ellipse,
                a,
                args,
                this.frame,
            ];
            const projectGroup = this.device.createBindGroup({
                layout: this.project.getBindGroupLayout(0),
                entries: projectBuffers.map((buffer, binding) => ({ binding, resource: { buffer } })),
            });
            const drawGroup = this.device.createBindGroup({
                layout: this.draw.getBindGroupLayout(0),
                entries: [ellipse, a, this.frame].map((buffer, binding) => ({
                    binding,
                    resource: { buffer },
                })),
            });
            await this.device.queue.onSubmittedWorkDone();
            scopesOpen = false;
            const [validation, oom] = await Promise.all([
                this.device.popErrorScope(),
                this.device.popErrorScope(),
            ]);
            if (oom) throw Error(`OutOfMemory: ${oom.message}`);
            if (validation) throw Error(`DeviceLost: GPU validation: ${validation.message}`);
            const uploaded = {
                scene,
                pages,
                ellipse,
                a,
                b,
                args,
                sort,
                projectGroup,
                drawGroup,
                bytes,
                revision: -1,
            };
            this.ownedScenes.add(uploaded);
            return uploaded;
        } catch (reason) {
            if (scopesOpen) {
                await Promise.all([this.device.popErrorScope(), this.device.popErrorScope()]).catch(() => {});
            }
            for (const b of resources) b.destroy();
            sort?.dispose();
            this.residentBytes -= bytes;
            this.cpuResidentBytes -= cpuBytes;
            throw reason;
        }
    }
    render(scene: GpuScene, frame: ArrayBuffer, revision: number, capture?: GPUBuffer): FrameStats {
        if (this.stopped) throw Error('Stopped');
        if (!this.canvas.width || !this.canvas.height) throw Error('InvalidInput: zero viewport');
        this.configureSurface();
        const start = performance.now();
        const frameId = ++this.frameId;
        if (this.timedScene !== scene) {
            this.timedScene = scene;
            this.lastGpu = null;
            this.lastGpuFrame = null;
        }
        this.device.queue.writeBuffer(this.frame, 0, frame);
        const encoder = this.device.createCommandEncoder();
        const sorted = scene.revision !== revision;
        const timed = !!this.query && !this.queryBusy;
        if (sorted) {
            this.device.queue.writeBuffer(scene.args, 0, new Uint32Array([4, 0, 0, 0]));
            const pass = encoder.beginComputePass(
                timed ? { timestampWrites: { querySet: this.query!, beginningOfPassWriteIndex: 0 } } : {},
            );
            pass.setPipeline(this.project);
            pass.setBindGroup(0, scene.projectGroup);
            pass.dispatchWorkgroups(Math.ceil(scene.scene.count / 256));
            pass.end();
            scene.sort.encode(encoder, scene.a, scene.b);
            scene.revision = revision;
        }
        const texture = this.context.getCurrentTexture();
        const pass = encoder.beginRenderPass({
            colorAttachments: [
                {
                    view: texture.createView(),
                    clearValue: { r: 0, g: 0, b: 0, a: 1 },
                    loadOp: 'clear',
                    storeOp: 'store',
                },
            ],
            ...(timed
                ? {
                      timestampWrites: {
                          querySet: this.query!,
                          ...(sorted ? {} : { beginningOfPassWriteIndex: 0 }),
                          endOfPassWriteIndex: 1,
                      },
                  }
                : {}),
        });
        pass.setPipeline(this.draw);
        pass.setBindGroup(0, scene.drawGroup);
        pass.drawIndirect(scene.args, 0);
        pass.end();
        if (capture)
            encoder.copyTextureToBuffer(
                { texture },
                { buffer: capture, bytesPerRow: Math.ceil((this.canvas.width * 4) / 256) * 256 },
                { width: this.canvas.width, height: this.canvas.height },
            );
        if (timed) {
            encoder.resolveQuerySet(this.query!, 0, 2, this.queryResolve!, 0);
            encoder.copyBufferToBuffer(this.queryResolve!, 0, this.queryRead!, 0, 16);
            this.queryBusy = true;
        }
        this.device.queue.submit([encoder.finish()]);
        if (timed) {
            void this.queryRead!.mapAsync(GPUMapMode.READ)
                .then(() => {
                    const t = new BigUint64Array(this.queryRead!.getMappedRange());
                    if (this.timedScene === scene) {
                        this.lastGpu = Number(t[1]! - t[0]!) / 1e6;
                        this.lastGpuFrame = frameId;
                    }
                    this.queryRead!.unmap();
                })
                .catch(() => {
                    this.lastGpu = null;
                })
                .finally(() => {
                    this.queryBusy = false;
                });
        }
        return {
            cpuMs: performance.now() - start,
            gpuMs: this.lastGpu,
            gpuFrameId: this.lastGpuFrame,
            frameId,
            sorted,
        };
    }
    release(scene: GpuScene): void {
        if (!this.ownedScenes.delete(scene)) return;
        this.residentBytes -= scene.bytes;
        this.cpuResidentBytes -= scene.scene.pages.reduce((sum, page) => sum + page.byteLength, 0);
        if (this.timedScene === scene) {
            this.timedScene = undefined;
            this.lastGpu = null;
            this.lastGpuFrame = null;
        }
        for (const b of [...scene.pages, scene.ellipse, scene.a, scene.b, scene.args]) b.destroy();
        scene.sort.dispose();
    }
    async capture(scene: GpuScene, frame: ArrayBuffer, revision: number): Promise<Uint8Array> {
        const width = this.canvas.width,
            height = this.canvas.height,
            row = Math.ceil((width * 4) / 256) * 256;
        const bytes = row * height;
        if (
            !width ||
            !height ||
            bytes + Math.max(scene.bytes, this.residentBytes) + this.captureBytes + this.baseBytes >
                this.budget
        )
            throw Error('ResourceLimit: capture');
        this.captureBytes += bytes;
        let buffer: GPUBuffer | undefined;
        try {
            buffer = this.device.createBuffer({
                size: bytes,
                usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
            });
            this.render(scene, frame, revision, buffer);
            await buffer.mapAsync(GPUMapMode.READ);
            const raw = new Uint8Array(buffer.getMappedRange());
            const rgba = new Uint8Array(width * height * 4);
            for (let y = 0; y < height; y++)
                rgba.set(raw.subarray(y * row, y * row + width * 4), y * width * 4);
            if (this.format.startsWith('bgra')) {
                for (let i = 0; i < rgba.length; i += 4) {
                    const red = rgba[i]!;
                    rgba[i] = rgba[i + 2]!;
                    rgba[i + 2] = red;
                }
            }
            buffer.unmap();
            return rgba;
        } finally {
            buffer?.destroy();
            this.captureBytes -= bytes;
        }
    }
    clear(): void {
        if (this.stopped || !this.canvas.width || !this.canvas.height) return;
        this.configureSurface();
        const encoder = this.device.createCommandEncoder();
        const pass = encoder.beginRenderPass({
            colorAttachments: [
                {
                    view: this.context.getCurrentTexture().createView(),
                    clearValue: { r: 0, g: 0, b: 0, a: 1 },
                    loadOp: 'clear',
                    storeOp: 'store',
                },
            ],
        });
        pass.end();
        this.device.queue.submit([encoder.finish()]);
    }
    async dispose(): Promise<void> {
        if (this.stopped) return;
        this.stopped = true;
        this.timedScene = undefined;
        await Promise.race([
            this.device.queue.onSubmittedWorkDone().catch(() => {}),
            new Promise<void>((resolve) => setTimeout(resolve, 2000)),
        ]);
        this.context.unconfigure();
        for (const scene of this.ownedScenes) this.release(scene);
        this.query?.destroy();
        this.queryRead?.destroy();
        this.queryResolve?.destroy();
        this.frame.destroy();
        this.dummy.destroy();
        this.device.destroy();
    }
}
