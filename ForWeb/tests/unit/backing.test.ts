import { expect, test, vi } from 'vitest';
import { diskBacking } from '../../src/model-io/backing';
import { Renderer } from '../../src/render-core/renderer';

test('disk backing survives another owner release and deletes only after its last owner', async () => {
    const removeEntry = vi.fn(async () => {});
    vi.stubGlobal('navigator', { storage: { getDirectory: async () => ({ removeEntry }) } });
    const backing = diskBacking('gs-1234', new File(['0123456789'], 'fixture'));
    backing.retain();
    await backing.release();
    expect(removeEntry).not.toHaveBeenCalled();
    expect(new TextDecoder().decode(await backing.read(2, 3))).toBe('234');
    await expect(backing.read(8, 4)).rejects.toThrow('InvalidInput');
    await backing.release();
    await backing.release();
    expect(removeEntry).toHaveBeenCalledOnce();
    await expect(backing.read(0, 1)).rejects.toThrow('released');
    expect(() => backing.retain()).toThrow('released');
    vi.unstubAllGlobals();
});
test('GPU disposal destroys remaining resources even when a scene backing cleanup rejects', async () => {
    const destroy = vi.fn(),
        release = vi.fn(async (scene: { bad: boolean }) => {
            if (scene.bad) throw Error('storage deletion failed');
        });
    const renderer = Object.assign(Object.create(Renderer.prototype) as Renderer, {
        stopped: false,
        ownedScenes: new Set([{ bad: true }, { bad: false }]),
        release,
        device: { queue: { onSubmittedWorkDone: async () => {} }, destroy },
        context: { unconfigure: vi.fn() },
        query: { destroy },
        queryRead: { destroy },
        queryResolve: { destroy },
        frame: { destroy },
        dummy: { destroy },
    });
    await expect(renderer.dispose()).rejects.toThrow('storage deletion failed');
    expect(release).toHaveBeenCalledTimes(2);
    expect(destroy).toHaveBeenCalledTimes(6);
});

test('GPU disposal remains bounded when timestamp mapping never completes', async () => {
    vi.useFakeTimers();
    try {
        const destroy = vi.fn();
        const renderer = Object.assign(Object.create(Renderer.prototype) as Renderer, {
            stopped: false,
            ownedScenes: new Set(),
            timingReady: new Promise<void>(() => {}),
            device: { queue: { onSubmittedWorkDone: async () => {} }, destroy },
            context: { unconfigure: vi.fn() },
            queryRead: { destroy },
            frame: { destroy },
            dummy: { destroy },
        });
        let finished = false;
        const disposal = renderer.dispose().then(() => {
            finished = true;
        });
        await vi.advanceTimersByTimeAsync(1999);
        expect(finished).toBe(false);
        await vi.advanceTimersByTimeAsync(1);
        await disposal;
        expect(finished).toBe(true);
        expect(destroy).toHaveBeenCalledTimes(4);
    } finally {
        vi.useRealTimers();
    }
});

test('final backing cleanup can retry a transient directory failure', async () => {
    const removeEntry = vi.fn(async () => {});
    const getDirectory = vi
        .fn()
        .mockRejectedValueOnce(Error('storage unavailable'))
        .mockResolvedValue({ removeEntry });
    vi.stubGlobal('navigator', { storage: { getDirectory } });
    try {
        const backing = diskBacking('gs-9876', new File(['x'], 'fixture'));
        await expect(backing.release()).rejects.toThrow('storage unavailable');
        await backing.release();
        await backing.release();
        expect(getDirectory).toHaveBeenCalledTimes(2);
        expect(removeEntry).toHaveBeenCalledOnce();
    } finally {
        vi.unstubAllGlobals();
    }
});

test('concurrent renderer disposal shares the actual cleanup completion', async () => {
    let release!: () => void;
    const gate = new Promise<void>((resolve) => {
        release = resolve;
    });
    const destroy = vi.fn();
    const renderer = Object.assign(Object.create(Renderer.prototype) as Renderer, {
        ownedScenes: new Set(),
        timingReady: gate,
        device: { queue: { onSubmittedWorkDone: () => gate }, destroy },
        context: { unconfigure: vi.fn() },
        frame: { destroy },
        dummy: { destroy },
    });
    const first = renderer.dispose(),
        second = renderer.dispose();
    expect(first).toBe(second);
    expect(destroy).not.toHaveBeenCalled();
    release();
    await second;
    expect(destroy).toHaveBeenCalledTimes(3);
});
