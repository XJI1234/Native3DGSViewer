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

describe('Camera display reflection', () => {
    const bounds = {
        origin: [10, 20, 30] as const,
        min: [9, 19, 29] as const,
        max: [11, 21, 31] as const,
        maxScale: 0.01,
    };
    it('reflects all view rows and relative eye without changing canonical pose', () => {
        const camera = new Camera();
        camera.fit(bounds, 800, 600);
        camera.orbit(30, 20);
        const pose = camera.getPose();
        const before = new Float32Array(camera.frame(bounds, 800, 600, 1, 3, 224, 1));
        camera.setFlipY(true);
        const after = new Float32Array(camera.frame(bounds, 800, 600, 1, 3, 224, 1));
        expect(camera.getPose()).toEqual(pose);
        for (const offset of [0, 4, 8, 12]) {
            expect(after[offset]).toBe(before[offset]);
            expect(after[offset + 1]).toBe(-before[offset + 1]!);
            expect(after[offset + 2]).toBe(before[offset + 2]);
        }
        camera.setFlipY(false);
        expect(new Float32Array(camera.frame(bounds, 800, 600, 1, 3, 224, 1))).toEqual(before);
    });
    it('preserves identical canonical drag direction and persistent flags through fit/reset', () => {
        const normal = new Camera(),
            flipped = new Camera();
        normal.fit(bounds, 800, 600);
        flipped.fit(bounds, 800, 600);
        flipped.setFlipY(true);
        flipped.setMode('fly');
        for (const c of [normal, flipped]) {
            c.orbit(50, 20);
            c.pan(12, -8);
            c.look(-10, 4);
            c.fly(1, 0, 1, 0.02);
        }
        expect(flipped.getPose()).toEqual(normal.getPose());
        flipped.reset();
        flipped.fit(bounds, 600, 800);
        expect(flipped.flipY).toBe(true);
        expect(flipped.mode).toBe('fly');
        expect(() => flipped.setMode('invalid' as 'orbit')).toThrow();
        expect(() => flipped.setFlipY(1 as unknown as boolean)).toThrow();
        expect(flipped.mode).toBe('fly');
        expect(flipped.flipY).toBe(true);
    });
});

it('positive fly-look deltas turn right and down with a fixed eye', () => {
    const camera = new Camera();
    const eye = camera.getPose().position;
    camera.look(10, 10);
    const pose = camera.getPose();
    expect(pose.position).toEqual(eye);
    expect(pose.target[0]).toBeGreaterThan(eye[0]);
    expect(pose.target[1]).toBeLessThan(eye[1]);
});
