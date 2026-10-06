import { expect, test } from 'vitest';
import { decoderPolicy, validateDecoderOptions } from '../../src/model-io/decoder-options';

test('decoder options reject invalid modes and thread counts before initialization', () => {
    for (const threads of [0, 9, -1, 2.5, NaN, Infinity])
        expect(() => validateDecoderOptions({ threads })).toThrow('InvalidInput');
    expect(() => validateDecoderOptions({ mode: 'invalid' as 'auto' })).toThrow('InvalidInput');
});
test('auto preserves single-thread use without isolation, small models or CPU headroom', () => {
    for (const [isolated, count, streaming, cpu] of [
        [false, 1e6, true, 512 * 2 ** 20],
        [true, 100, true, 512 * 2 ** 20],
        [true, 1e6, false, 512 * 2 ** 20],
        [true, 1e6, true, 64 * 2 ** 20],
    ] as const) {
        const policy = decoderPolicy({}, isolated, 16, count, streaming, cpu);
        expect(policy.backend).toBe('single');
        expect(policy.threads).toBe(1);
        expect(policy.fallbackReason).toBeTruthy();
    }
});
test('explicit parallel reports missing isolation and budget; thread limits include coordinator', () => {
    expect(() => decoderPolicy({ mode: 'parallel' }, false, 16, 1e6, true, 512 * 2 ** 20)).toThrow(
        'UnsupportedCapability',
    );
    expect(() => decoderPolicy({ mode: 'parallel' }, true, 16, 1e6, true, 64 * 2 ** 20)).toThrow(
        'ResourceLimit',
    );
    expect(decoderPolicy({}, true, 16, 1e6, true, 512 * 2 ** 20).threads).toBe(4);
    expect(decoderPolicy({ threads: 8 }, true, 2, 1e6, true, 512 * 2 ** 20).threads).toBe(2);
    expect(decoderPolicy({ mode: 'single' }, true, 16, 1e6, true, 512 * 2 ** 20).backend).toBe('single');
});
