import { beforeEach, expect, test, vi } from 'vitest';
import { WebEngine } from '../../src/engine/engine';

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
        device: {
            limits: { maxTextureDimension2D: 8192 },
            lost: new Promise(() => {}),
            addEventListener: vi.fn(),
            pushErrorScope: vi.fn(),
            popErrorScope: async () => null,
            queue: { onSubmittedWorkDone: async () => {} },
        },
        upload: vi.fn(async () => ({ scene, bytes: 100, revision: -1 })),
        render: vi.fn(),
        clear: vi.fn(),
        release: vi.fn(),
        dispose: vi.fn(async () => {}),
    };
}
beforeEach(() => {
    vi.resetAllMocks();
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
    expect(allocated.render).toHaveBeenCalledTimes(2);
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
