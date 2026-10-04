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
