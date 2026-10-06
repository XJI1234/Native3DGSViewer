import { beforeEach, expect, test, vi } from 'vitest';
import { WebEngine } from '../../src/engine/engine';
import { abortable } from '../../src/splat-types/async';

const fake = vi.hoisted(() => ({ create: vi.fn(), decode: vi.fn() }));
vi.mock('../../src/render-core/renderer', () => ({ Renderer: { create: fake.create } }));
vi.mock('../../src/model-io/loader', () => ({ decode: fake.decode }));
const scene = {
    count: 1,
    degree: 0,
    stride: 64,
    pageCapacity: 1,
    pages: [new ArrayBuffer(64)],
    origin: [0, 0, 0],
    min: [-1, -1, -1],
    max: [1, 1, 1],
    maxScale: 1,
    source: 'fixture',
    decodeMs: 0,
};
function deferred<T>() {
    let resolve!: (value: T) => void;
    const promise = new Promise<T>((r) => {
        resolve = r;
    });
    return { promise, resolve };
}
function renderer() {
    return {
        pageBytes: 128 * 2 ** 20,
        retainedBytes: 0,
        format: 'rgba8unorm',
        device: {
            limits: { maxTextureDimension2D: 8192 },
            lost: new Promise(() => {}),
            addEventListener: vi.fn(),
            pushErrorScope: vi.fn(),
            createTexture: vi.fn(() => ({ destroy: vi.fn() })),
            popErrorScope: async () => null,
            queue: { onSubmittedWorkDone: async () => {} },
        },
        upload: vi.fn(async () => ({ scene, bytes: 100, revision: -1 })),
        render: vi.fn(),
        needsSort: vi.fn(() => false),
        clear: vi.fn(),
        waitForWork(signal: AbortSignal) {
            return abortable(this.device.queue.onSubmittedWorkDone(), signal);
        },
        validationTarget: vi.fn(() => ({ texture: {}, release: vi.fn() })),
        release: vi.fn(),
        dispose: vi.fn(async () => {}),
    };
}
beforeEach(() => {
    vi.resetAllMocks();
    vi.stubGlobal('GPUTextureUsage', { RENDER_ATTACHMENT: 1 });
    vi.stubGlobal('location', { href: 'http://localhost/' });
    vi.stubGlobal('document', { hidden: false });
    vi.stubGlobal('requestAnimationFrame', () => 1);
    vi.stubGlobal('cancelAnimationFrame', () => {});
    fake.decode.mockResolvedValue(scene);
    fake.create.mockImplementation(async () => renderer());
});
async function create(width = 640, height = 480) {
    const result = await WebEngine.create({ canvas: { width, height } as HTMLCanvasElement });
    if (!result.ok) throw Error(result.error.diagnostic);
    return result.value;
}

test('automatic frames overlap two submissions, bound backlog and consume latest camera', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const first = deferred<void>(),
        second = deferred<void>();
    allocated.device.queue.onSubmittedWorkDone = vi
        .fn()
        .mockImplementationOnce(() => first.promise)
        .mockImplementationOnce(() => second.promise)
        .mockImplementation(() => new Promise(() => {}));
    const revisions: number[] = [];
    allocated.render.mockImplementation((_scene, _frame, revision) => {
        revisions.push(revision);
        _scene.revision = revision;
    });
    const tick = () => (engine as unknown as { tick(): void }).tick();
    engine.camera.orbit(1, 0);
    tick();
    engine.camera.orbit(1, 0);
    tick();
    expect(revisions).toHaveLength(2);
    engine.camera.orbit(1, 0);
    tick();
    engine.camera.orbit(1, 0);
    tick();
    expect(revisions).toHaveLength(2);
    first.resolve();
    await new Promise((resolve) => setTimeout(resolve, 0));
    tick();
    expect(revisions).toHaveLength(3);
    expect(revisions[2]! - revisions[1]!).toBe(2);
    second.resolve();
    await engine.dispose();
});

test('invalid frame depth fails before GPU allocation', async () => {
    const result = await WebEngine.create({
        canvas: { width: 640, height: 480 } as HTMLCanvasElement,
        maxFramesInFlight: 0,
    } as unknown as Parameters<typeof WebEngine.create>[0]);
    expect(result.ok).toBe(false);
    expect(fake.create).not.toHaveBeenCalled();
});

test('decoder options are validated before allocation and fixed before asynchronous initialization', async () => {
    for (const decoder of [{ threads: 9 }, { mode: 'invalid' }]) {
        const result = await WebEngine.create({
            canvas: {} as HTMLCanvasElement,
            decoder,
        } as unknown as Parameters<typeof WebEngine.create>[0]);
        expect(result.ok).toBe(false);
    }
    expect(fake.create).not.toHaveBeenCalled();
    const wait = deferred<ReturnType<typeof renderer>>();
    fake.create.mockReturnValueOnce(wait.promise);
    const decoder = { mode: 'single' as 'single' | 'auto', threads: 2 };
    const pending = WebEngine.create({ canvas: { width: 640, height: 480 } as HTMLCanvasElement, decoder });
    decoder.mode = 'auto';
    decoder.threads = 8;
    wait.resolve(renderer());
    const created = await pending;
    if (!created.ok) throw Error('create');
    await created.value.open({ kind: 'blob', blob: new Blob() }).result;
    expect(fake.decode.mock.calls[0]![8]).toEqual({ mode: 'single', threads: 2 });
    await created.value.dispose();
});

test('decoder diagnostics follow accepted scene ownership through recovery and close', async () => {
    const info = { backend: 'pthreads' as const, threads: 4, fallbackReason: null };
    fake.decode.mockResolvedValueOnce({ ...scene, decoder: info });
    const allocated = renderer();
    allocated.upload.mockImplementation(async (input) => ({ scene: input, bytes: 100, revision: -1 }));
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    expect((await engine.open({ kind: 'blob', blob: new Blob() }).result).ok).toBe(true);
    expect(engine.getSnapshot().decoder).toEqual(info);
    expect(Object.isFrozen(engine.getSnapshot().decoder)).toBe(true);
    expect((await engine.recover()).ok).toBe(true);
    expect(engine.getSnapshot().decoder).toEqual(info);
    await engine.closeScene();
    expect(engine.getSnapshot().decoder).toBeNull();
    await engine.dispose();
    expect(engine.getSnapshot().decoder).toBeNull();
});

test('caller mutation cannot increase the validated submission bound', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const options = {
        canvas: { width: 640, height: 480 } as HTMLCanvasElement,
        maxFramesInFlight: 1 as 1 | 2 | 3,
    };
    const result = await WebEngine.create(options);
    if (!result.ok) throw Error('create');
    const engine = result.value;
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    allocated.device.queue.onSubmittedWorkDone = () => new Promise(() => {});
    let submissions = 0;
    allocated.render.mockImplementation((scene, _frame, revision) => {
        scene.revision = revision;
        submissions++;
    });
    const tick = () => (engine as unknown as { tick(): void }).tick();
    tick();
    options.maxFramesInFlight = 3;
    engine.camera.orbit(1, 0);
    tick();
    expect(submissions).toBe(1);
    allocated.device.queue.onSubmittedWorkDone = async () => {};
    await engine.dispose();
});

test('single-frame fallback suppresses paused and hidden draws without losing latest input', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const created = await WebEngine.create({
        canvas: { width: 640, height: 480 } as HTMLCanvasElement,
        maxFramesInFlight: 1,
    });
    if (!created.ok) throw Error('create');
    const engine = created.value;
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const wait = deferred<void>();
    allocated.device.queue.onSubmittedWorkDone = () => wait.promise;
    const revisions: number[] = [];
    allocated.render.mockImplementation((scene, _frame, revision) => {
        scene.revision = revision;
        revisions.push(revision);
    });
    const tick = () => (engine as unknown as { tick(): void }).tick();
    engine.camera.orbit(1, 0);
    tick();
    engine.camera.orbit(1, 0);
    tick();
    expect(revisions).toHaveLength(1);
    wait.resolve();
    await new Promise((resolve) => setTimeout(resolve, 0));
    engine.pause();
    tick();
    expect(revisions).toHaveLength(1);
    engine.resume();
    vi.stubGlobal('document', { hidden: true });
    tick();
    expect(revisions).toHaveLength(1);
    vi.stubGlobal('document', { hidden: false });
    tick();
    expect(revisions).toHaveLength(2);
    expect(revisions[1]! - revisions[0]!).toBe(1);
    await engine.dispose();
});

test('old-device completion cannot free a new renderer submission slot', async () => {
    const old = renderer(),
        replacement = renderer();
    fake.create.mockResolvedValueOnce(old).mockResolvedValueOnce(replacement);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const oldWait = deferred<void>();
    old.device.queue.onSubmittedWorkDone = () => oldWait.promise;
    const tick = () => (engine as unknown as { tick(): void }).tick();
    tick();
    expect((await engine.recover()).ok).toBe(true);
    const newWait = deferred<void>();
    replacement.device.queue.onSubmittedWorkDone = () => newWait.promise;
    let submissions = 0;
    replacement.render.mockImplementation((scene, _frame, revision) => {
        scene.revision = revision;
        submissions++;
    });
    tick();
    engine.camera.orbit(1, 0);
    tick();
    expect(submissions).toBe(2);
    oldWait.resolve();
    await new Promise((resolve) => setTimeout(resolve, 0));
    engine.camera.orbit(1, 0);
    tick();
    expect(submissions).toBe(2);
    newWait.resolve();
    await engine.dispose();
});
test('zero viewport waits and resize does not overwrite upload transaction', async () => {
    const engine = await create(0, 0),
        operation = engine.open({ kind: 'blob', blob: new Blob() });
    await new Promise((r) => setTimeout(r, 25));
    expect(engine.getSnapshot().phase).toBe('Uploading');
    expect(engine.getSnapshot().sceneCount).toBe(0);
    engine.resize(640, 480);
    expect(engine.getSnapshot().phase).toBe('Uploading');
    expect((await operation.result).ok).toBe(true);
    expect(engine.getSnapshot().phase).toBe('Ready');
    await engine.dispose();
});
test('old-scene asynchronous cleanup cannot overwrite a later close snapshot', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const cleanup = deferred<void>();
    allocated.release.mockImplementationOnce(() => cleanup.promise);
    const replacement = engine.open({ kind: 'blob', blob: new Blob() });
    await vi.waitFor(() => expect(allocated.release).toHaveBeenCalled());
    const close = engine.closeScene();
    expect(engine.getSnapshot().sceneCount).toBe(0);
    cleanup.resolve();
    await replacement.result;
    await close;
    expect(engine.getSnapshot().sceneCount).toBe(0);
    expect(engine.getSnapshot().phase).toBe('Idle');
    await engine.dispose();
});
test('dispose waits for aborted decoder completion and its backing cleanup', async () => {
    const decode = deferred<typeof scene>();
    const cleanup = deferred<void>();
    const backing = {
        totalBytes: 64,
        residentBytes: 0,
        read: vi.fn(),
        retain: vi.fn(),
        release: vi.fn(() => cleanup.promise),
    };
    fake.decode.mockImplementationOnce(() => decode.promise);
    const engine = await create();
    const operation = engine.open({ kind: 'blob', blob: new Blob() });
    let disposed = false;
    const disposal = engine.dispose().then(() => {
        disposed = true;
    });
    await new Promise((resolve) => setTimeout(resolve, 10));
    expect(disposed).toBe(false);
    decode.resolve({ ...scene, backing } as typeof scene);
    await vi.waitFor(() => expect(backing.release).toHaveBeenCalledOnce());
    expect(disposed).toBe(false);
    cleanup.resolve();
    expect((await operation.result).ok).toBe(false);
    await disposal;
    expect(engine.getSnapshot().phase).toBe('Stopped');
});
test('backing cleanup failure returns a structured error and clears the pending operation', async () => {
    const allocated = renderer();
    allocated.upload.mockRejectedValueOnce(Error('ResourceLimit: injected admission'));
    fake.create.mockResolvedValue(allocated);
    const backing = {
        totalBytes: 64,
        residentBytes: 0,
        read: vi.fn(),
        retain: vi.fn(),
        release: vi.fn(async () => {
            throw Error('injected storage deletion');
        }),
    };
    fake.decode.mockResolvedValueOnce({ ...scene, backing });
    const engine = await create();
    const result = await engine.open({ kind: 'blob', blob: new Blob() }).result;
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error.stage).toBe('StorageCleanup');
    expect(engine.getSnapshot().sceneCount).toBe(0);
    await engine.dispose();
});
test('Loading observer disposal owns the load before synchronous publication and reports cleanup failures', async () => {
    const decode = deferred<typeof scene>();
    fake.decode.mockImplementationOnce(() => decode.promise);
    const backing = {
        totalBytes: 64,
        residentBytes: 0,
        read: vi.fn(),
        retain: vi.fn(),
        release: vi.fn(async () => {
            throw Error('injected interrupted cleanup');
        }),
    };
    const engine = await create();
    let disposal: Promise<void> | undefined,
        stopped = false;
    engine.subscribe(() => {
        if (engine.getSnapshot().phase === 'Loading')
            disposal = engine.dispose().then(() => {
                stopped = true;
            });
    });
    const operation = engine.open({ kind: 'blob', blob: new Blob() });
    await new Promise((resolve) => setTimeout(resolve, 10));
    expect(disposal).toBeDefined();
    expect(stopped).toBe(false);
    decode.resolve({ ...scene, backing } as typeof scene);
    expect((await operation.result).ok).toBe(false);
    await disposal;
    expect(engine.getSnapshot().phase).toBe('Stopped');
    expect(engine.getSnapshot().error?.stage).toBe('StorageCleanup');
    expect(engine.getSnapshot().error?.diagnostic).toContain('interrupted cleanup');
});
test('close during recovery upload never resurrects retained scene', async () => {
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const replacement = renderer(),
        wait = deferred<Awaited<ReturnType<typeof replacement.upload>>>();
    replacement.upload.mockImplementation(() => wait.promise);
    fake.create.mockResolvedValue(replacement);
    const recovery = engine.recover();
    await vi.waitFor(() => expect(replacement.upload).toHaveBeenCalled());
    await engine.closeScene();
    engine.resize(800, 600);
    expect(engine.getSnapshot().phase).toBe('Recovering');
    wait.resolve({ scene, bytes: 100, revision: -1 });
    expect((await recovery).ok).toBe(true);
    expect(engine.getSnapshot().sceneCount).toBe(0);
    expect(engine.getSnapshot().phase).toBe('Idle');
    expect(replacement.render).not.toHaveBeenCalled();
    await engine.dispose();
});

test('close immediately after recover clears snapshot as well as owned scene', async () => {
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const recovery = engine.recover();
    await engine.closeScene();
    expect((await recovery).ok).toBe(true);
    expect(engine.getSnapshot().sceneCount).toBe(0);
    expect(engine.getSnapshot().source).toBe(null);
    expect(engine.getSnapshot().phase).toBe('Idle');
    await engine.dispose();
});

test('failed recovery after close and rejected open cannot retain closed metadata', async () => {
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    fake.create.mockRejectedValue(Error('injected unavailable adapter'));
    const recovery = engine.recover(),
        closed = engine.closeScene();
    const rejected = engine.open({ kind: 'blob', blob: new Blob() });
    expect((await rejected.result).ok).toBe(false);
    await closed;
    expect((await recovery).ok).toBe(false);
    expect(engine.getSnapshot().phase).toBe('Faulted');
    expect(engine.getSnapshot().sceneCount).toBe(0);
    expect(engine.getSnapshot().source).toBe(null);
    await engine.closeScene();
    expect(engine.getSnapshot().phase).toBe('Faulted');
    await engine.dispose();
});

test('fault arriving during close retains its latest diagnostic after GPU cleanup', async () => {
    vi.stubGlobal('GPUOutOfMemoryError', class extends Error {});
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const submitted = deferred<void>();
    allocated.device.queue.onSubmittedWorkDone = () => submitted.promise;
    const closed = engine.closeScene();
    const listener = allocated.device.addEventListener.mock.calls[0]![1] as (event: { error: Error }) => void;
    listener({ error: new Error('fault while closing') });
    submitted.resolve();
    await closed;
    expect(engine.getSnapshot().phase).toBe('Faulted');
    expect(engine.getSnapshot().error?.diagnostic).toBe('fault while closing');
    await engine.dispose();
});

test('decode admission includes renderer-held CPU pages after close detaches active scene', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    allocated.retainedBytes = 64;
    const submitted = deferred<void>();
    allocated.device.queue.onSubmittedWorkDone = () => submitted.promise;
    const closed = engine.closeScene();
    fake.decode.mockRejectedValueOnce(Error('ResourceLimit: retained CPU pages'));
    const loaded = await engine.open({ kind: 'blob', blob: new Blob() }).result;
    expect(fake.decode.mock.lastCall?.[6]).toBe(64);
    expect(loaded.ok).toBe(false);
    submitted.resolve();
    await closed;
    await engine.dispose();
});
test('disposal waits for recovery and suppresses activation', async () => {
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const replacement = renderer(),
        wait = deferred<Awaited<ReturnType<typeof replacement.upload>>>();
    replacement.upload.mockImplementation(() => wait.promise);
    fake.create.mockResolvedValue(replacement);
    const recovery = engine.recover();
    await vi.waitFor(() => expect(replacement.upload).toHaveBeenCalled());
    const disposal = engine.dispose();
    wait.resolve({ scene, bytes: 100, revision: -1 });
    await recovery;
    await disposal;
    expect(engine.getSnapshot().phase).toBe('Stopped');
    expect(engine.getSnapshot().sceneCount).toBe(0);
    expect(replacement.dispose).toHaveBeenCalled();
});
test('latest-wins ignores stale decoder completion and pause preserves Loading', async () => {
    const engine = await create(),
        first = deferred<typeof scene>();
    fake.decode.mockImplementationOnce(() => first.promise);
    const stale = engine.open({ kind: 'blob', blob: new Blob() });
    engine.pause();
    engine.resize(800, 600);
    expect(engine.getSnapshot().phase).toBe('Loading');
    const latest = engine.open({ kind: 'blob', blob: new Blob() });
    expect((await latest.result).ok).toBe(true);
    first.resolve(scene);
    expect((await stale.result).ok).toBe(false);
    expect(engine.getSnapshot().phase).toBe('Suspended');
    engine.resume();
    expect(engine.getSnapshot().phase).toBe('Ready');
    await engine.dispose();
});

test('failed initialization releases the already-created renderer', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const result = await WebEngine.create({
        canvas: { width: 640, height: 480 } as HTMLCanvasElement,
        assets: { baseUrl: 'http://[' },
    });
    expect(result.ok).toBe(false);
    expect(allocated.dispose).toHaveBeenCalledOnce();
});

test('uncaptured validation fault rejects open without clearing Faulted', async () => {
    vi.stubGlobal('GPUOutOfMemoryError', class extends Error {});
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    const listener = allocated.device.addEventListener.mock.calls[0]![1] as (event: { error: Error }) => void;
    listener({ error: new Error('injected validation') });
    expect(engine.getSnapshot().phase).toBe('Faulted');
    const result = await engine.open({ kind: 'blob', blob: new Blob() }).result;
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error.code).toBe('DeviceLost');
    expect(engine.getSnapshot().phase).toBe('Faulted');
    expect(fake.decode).not.toHaveBeenCalled();
    await engine.dispose();
});

test('viewport collapse during upload waits for a new nonzero first frame', async () => {
    const allocated = renderer(),
        wait = deferred<Awaited<ReturnType<typeof allocated.upload>>>();
    allocated.upload.mockImplementation(() => wait.promise);
    fake.create.mockResolvedValue(allocated);
    const engine = await create(),
        operation = engine.open({ kind: 'blob', blob: new Blob() });
    await vi.waitFor(() => expect(allocated.upload).toHaveBeenCalled());
    engine.resize(0, 0);
    wait.resolve({ scene, bytes: 100, revision: -1 });
    await new Promise((r) => setTimeout(r, 25));
    expect(allocated.render).not.toHaveBeenCalled();
    expect(engine.getSnapshot().sceneCount).toBe(0);
    engine.resize(640, 480);
    expect((await operation.result).ok).toBe(true);
    await engine.dispose();
});

test('observer reentry shares recovery and disposal promises', async () => {
    const engine = await create();
    let nestedRecovery: ReturnType<typeof engine.recover> | undefined;
    let nestedDisposal: ReturnType<typeof engine.dispose> | undefined;
    engine.subscribe(() => {
        if (engine.getSnapshot().phase === 'Recovering' && !nestedRecovery) nestedRecovery = engine.recover();
        if (engine.getSnapshot().phase === 'Stopping' && !nestedDisposal) nestedDisposal = engine.dispose();
    });
    const recovery = engine.recover();
    await recovery;
    expect(nestedRecovery).toBe(recovery);
    expect(fake.create).toHaveBeenCalledTimes(2);
    const disposal = engine.dispose();
    await disposal;
    expect(nestedDisposal).toBe(disposal);
});

test('first-frame completion after resize must validate a new frame before activation', async () => {
    const allocated = renderer(),
        submitted = deferred<void>();
    allocated.device.queue.onSubmittedWorkDone = vi
        .fn()
        .mockImplementationOnce(() => submitted.promise)
        .mockResolvedValue(undefined);
    fake.create.mockResolvedValue(allocated);
    const engine = await create(),
        operation = engine.open({ kind: 'blob', blob: new Blob() });
    await vi.waitFor(() => expect(allocated.render).toHaveBeenCalledOnce());
    engine.resize(0, 0);
    submitted.resolve();
    await new Promise((r) => setTimeout(r, 25));
    expect(engine.getSnapshot().sceneCount).toBe(0);
    engine.resize(800, 600);
    expect((await operation.result).ok).toBe(true);
    expect(allocated.render).toHaveBeenCalledTimes(3);
    expect(allocated.render.mock.calls[0]?.[2]).not.toBe(allocated.render.mock.calls[1]?.[2]);
    await engine.dispose();
});

test('published nested progress is immutable and detached from provider values', async () => {
    const engine = await create(),
        pending = deferred<typeof scene>();
    fake.decode.mockImplementation(() => pending.promise);
    const operation = engine.open({ kind: 'blob', blob: new Blob() });
    const snapshot = engine.getSnapshot();
    expect(Object.isFrozen(snapshot.progress)).toBe(true);
    expect(() => Object.assign(snapshot.progress!, { done: 999 })).toThrow();
    pending.resolve(scene);
    await operation.result;
    expect(snapshot.progress?.done).toBe(0);
    await engine.dispose();
});

test('cancel during stalled candidate validation never presents the candidate and closes promptly', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    engine.pause();
    const before = allocated.render.mock.calls.length;
    allocated.device.queue.onSubmittedWorkDone = () => new Promise(() => {});
    const operation = engine.open({ kind: 'blob', blob: new Blob() });
    await vi.waitFor(() => expect(allocated.render.mock.calls.length).toBe(before + 1));
    operation.cancel();
    const result = await operation.result;
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error.code).toBe('Cancelled');
    expect(allocated.render.mock.calls.at(-1)?.[4]).toBeDefined();
    expect(engine.getSnapshot().sceneCount).toBe(1);
    await engine.dispose();
});

test('dispose aborts stalled recovery creation and disposes a late renderer', async () => {
    const engine = await create();
    const creating = deferred<ReturnType<typeof renderer>>();
    fake.create.mockImplementationOnce(() => creating.promise);
    const recovery = engine.recover();
    await vi.waitFor(() => expect(fake.create).toHaveBeenCalledTimes(2));
    await engine.dispose();
    expect((await recovery).ok).toBe(false);
    const late = renderer();
    creating.resolve(late);
    await vi.waitFor(() => expect(late.dispose).toHaveBeenCalledOnce());
    expect(engine.getSnapshot().phase).toBe('Stopped');
});

test('captured recovery validation errors cannot publish Ready', async () => {
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    fake.create.mockImplementation(async () => {
        const next = renderer();
        next.device.popErrorScope = async () => ({ message: 'injected GPU validation' }) as never;
        return next;
    });
    const result = await engine.recover();
    expect(result.ok).toBe(false);
    expect(engine.getSnapshot().phase).toBe('Faulted');
    await engine.dispose();
});

test('canvas presentation failure preserves the old scene and releases only the candidate', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    engine.pause();
    const before = engine.camera.getPose();
    allocated.render
        .mockImplementationOnce(() => undefined)
        .mockImplementationOnce(() => {
            throw Error('canvas acquisition failed');
        });
    const result = await engine.open({ kind: 'blob', blob: new Blob() }).result;
    expect(result.ok).toBe(false);
    expect(engine.getSnapshot().sceneCount).toBe(1);
    expect(engine.camera.getPose()).toEqual(before);
    expect(allocated.release).toHaveBeenCalledOnce();
    await engine.dispose();
});

test('recovery returns Timeout even while old renderer cleanup remains pending', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const created = await WebEngine.create({
        canvas: { width: 640, height: 480 } as HTMLCanvasElement,
        limits: { timeoutMs: 20 },
    });
    if (!created.ok) throw Error('create');
    const engine = created.value,
        cleanup = deferred<void>();
    allocated.dispose.mockImplementationOnce(() => cleanup.promise);
    const result = await engine.recover();
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.error.code).toBe('Timeout');
    expect(engine.getSnapshot().phase).toBe('Faulted');
    cleanup.resolve();
    await engine.dispose();
    expect(engine.getSnapshot().phase).toBe('Stopped');
});

test('dirty RAF cannot overwrite candidate presentation during completion', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    await engine.open({ kind: 'blob', blob: new Blob() }).result;
    const wait = deferred<void>();
    allocated.device.queue.onSubmittedWorkDone = vi
        .fn()
        .mockResolvedValueOnce(undefined)
        .mockImplementationOnce(() => wait.promise);
    const before = allocated.render.mock.calls.length,
        operation = engine.open({ kind: 'blob', blob: new Blob() });
    await vi.waitFor(() => expect(allocated.render.mock.calls.length).toBe(before + 2));
    engine.camera.orbit(15, 5);
    engine.requestFrame();
    (engine as unknown as { tick(): void }).tick();
    expect(allocated.render.mock.calls.length).toBe(before + 2);
    wait.resolve();
    expect((await operation.result).ok).toBe(true);
    await engine.dispose();
});

test('old recovery cleanup cannot overwrite a later successful recovery', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const created = await WebEngine.create({
        canvas: { width: 640, height: 480 } as HTMLCanvasElement,
        limits: { timeoutMs: 20 },
    });
    if (!created.ok) throw Error('create');
    const engine = created.value,
        cleanup = deferred<void>();
    allocated.dispose.mockImplementationOnce(() => cleanup.promise);
    expect((await engine.recover()).ok).toBe(false);
    expect((await engine.recover()).ok).toBe(true);
    expect(engine.getSnapshot().phase).toBe('Idle');
    cleanup.resolve();
    await new Promise((resolve) => setTimeout(resolve, 10));
    expect(engine.getSnapshot().phase).toBe('Idle');
    await engine.dispose();
});

test('sorting options are validated before device creation and frozen through recovery', async () => {
    const canvas = { width: 640, height: 480 } as HTMLCanvasElement;
    const invalid = await WebEngine.create({ canvas, sorting: { maxSortAgeMs: NaN } });
    expect(invalid.ok).toBe(false);
    expect(fake.create).not.toHaveBeenCalled();
    const sorting = { mode: 'adaptive' as const, maxSortAgeMs: 80 };
    const created = await WebEngine.create({ canvas, sorting });
    if (!created.ok) throw Error(created.error.diagnostic);
    sorting.maxSortAgeMs = 900;
    const copied = fake.create.mock.calls[0]![2];
    expect(copied.maxSortAgeMs).toBe(80);
    expect(Object.isFrozen(copied)).toBe(true);
    expect((await created.value.recover()).ok).toBe(true);
    expect(fake.create.mock.calls[1]![2]).toBe(copied);
    await created.value.dispose();
});

test('idle RAF refreshes pending ordering while retaining frame backlog bounds', async () => {
    const allocated = renderer();
    fake.create.mockResolvedValue(allocated);
    const engine = await create();
    expect((await engine.open({ kind: 'blob', blob: new Blob() }).result).ok).toBe(true);
    const internal = engine as unknown as {
        dirty: boolean;
        revision: number;
        active: { revision: number };
        tick(): void;
    };
    internal.dirty = false;
    internal.active.revision = engine.camera.revision + internal.revision;
    const before = allocated.render.mock.calls.length;
    allocated.needsSort.mockReturnValue(true);
    internal.tick();
    expect(allocated.render.mock.calls.length).toBe(before + 1);
    await engine.dispose();
});
