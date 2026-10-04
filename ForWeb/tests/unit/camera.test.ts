import { describe, expect, it } from 'vitest';
import { Camera } from '../../src/engine/camera';

describe('Camera', () => {
    it('fits bounds including support and preserves pose on resize', () => {
        const camera = new Camera();
        camera.fit(
            { origin: [1000000, 0, 0], min: [999999, -1, -1], max: [1000001, 1, 1], maxScale: 0.01 },
            800,
            600,
        );
        const before = camera.getPose();
        expect(before.position[0]).toBe(1000000);
        expect(before.position[2]).toBeGreaterThan(3);
        camera.resize(1600, 900);
        expect(camera.getPose()).toEqual(before);
        camera.orbit(30, 10);
        camera.reset();
        expect(camera.getPose()).toEqual(before);
    });
    it('rejects non-finite inputs without changing valid pose', () => {
        const camera = new Camera();
        const before = camera.getPose();
        expect(() => camera.orbit(NaN, 0)).toThrow();
        expect(camera.getPose()).toEqual(before);
        expect(() => camera.resize(-1, 100)).toThrow();
    });
    it('roundtrips explicit rolled poses and keeps the eye fixed for fly look', () => {
        const camera = new Camera();
        camera.setPose({ position: [4, 3, 2], target: [0, 0, 0], up: [0, 0, 1] });
        const before = camera.getPose();
        expect(before.position[0]).toBeCloseTo(4);
        expect(before.position[1]).toBeCloseTo(3);
        camera.look(10, 20);
        const after = camera.getPose();
        after.position.forEach((v, i) => {
            expect(v).toBeCloseTo(before.position[i]!);
        });
        expect(() => camera.setPose({ position: [0, 0, 0], target: [0, 0, 0], up: [0, 1, 0] })).toThrow();
    });
});
