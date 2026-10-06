import { describe, expect, it } from 'vitest';
import { resolveSortingOptions, SortingPolicy } from '../../src/render-core/sorting-policy';

const frame = () => {
    const f = new Float32Array(32);
    f[0] = f[5] = f[10] = 1;
    f[16] = f[17] = 100;
    f[18] = f[19] = 64;
    return f;
};
describe('adaptive sorting policy', () => {
    it('uses adaptive by default and permits explicit strict and rejects invalid options before allocation', () => {
        expect(resolveSortingOptions().mode).toBe('adaptive');
        expect(resolveSortingOptions({ mode: 'strict' }).mode).toBe('strict');
        for (const value of [NaN, Infinity, 0, 1001])
            expect(() => resolveSortingOptions({ maxSortAgeMs: value })).toThrow('InvalidInput');
        expect(() => resolveSortingOptions({ mode: 'bad' as 'strict' })).toThrow('InvalidInput');
        expect(Object.isFrozen(resolveSortingOptions())).toBe(true);
    });
    it('bounds stale translation and refreshes after input stops', () => {
        const p = new SortingPolicy(resolveSortingOptions({ mode: 'adaptive' }), 10),
            f = frame();
        expect(p.decide(f, 0, true)).toBe('initial');
        p.submitted(f, 0);
        p.observeCost(10);
        f[12] = 0.1;
        expect(p.decide(f, 20, true)).toBe('reuse');
        expect(p.needsRefresh(79)).toBe(false);
        expect(p.needsRefresh(80)).toBe(true);
        expect(p.decide(f, 80, false)).toBe('budget');
        expect(p.decide(f, 100, false)).toBe('age');
        p.submitted(f, 100);
        expect(p.needsRefresh(300)).toBe(false);
    });
    it('forces capture, viewport, quality, reflection and camera jumps', () => {
        for (const change of [
            (f: Float32Array) => {
                f[18] = 128;
            },
            (f: Float32Array) => {
                f[20] = 4;
            },
            (f: Float32Array) => {
                f[5] = -1;
            },
            (f: Float32Array) => {
                f[12] = 2;
            },
            (f: Float32Array) => {
                f[10] = 0;
                f[8] = 1;
            },
        ]) {
            const p = new SortingPolicy(resolveSortingOptions({ mode: 'adaptive' }), 10),
                f = frame();
            p.submitted(f, 0);
            change(f);
            expect(p.decide(f, 1, true)).not.toBe('reuse');
        }
        const p = new SortingPolicy(resolveSortingOptions({ mode: 'adaptive' }), 10),
            f = frame();
        p.submitted(f, 0);
        expect(p.decide(f, 1, false, true)).toBe('forced');
        f[10] = Math.cos(0.2);
        f[8] = Math.sin(0.2);
        expect(p.decide(f, 1000, true)).toBe('reuse');
        expect(p.needsRefresh(2000)).toBe(false);
    });
});
