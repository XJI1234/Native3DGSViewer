import { expect, test } from 'vitest';
import { parseSpzV4 } from '../../src/model-io/spz-v4';
import { streamingBudget } from '../../src/model-io/streaming-budget';

function header(degree = 3) {
    const widths = [9, 1, 3, 3, 4, ...(degree ? [3 * ((degree + 1) ** 2 - 1)] : [])];
    const bytes = new Uint8Array(32 + widths.length * 16),
        view = new DataView(bytes.buffer);
    view.setUint32(0, 0x5053474e, true);
    view.setUint32(4, 4, true);
    view.setUint32(8, 65, true);
    bytes[12] = degree;
    bytes[13] = 12;
    bytes[15] = widths.length;
    view.setUint32(16, 32, true);
    widths.forEach((w, i) => {
        view.setBigUint64(32 + i * 16, 20n, true);
        view.setBigUint64(40 + i * 16, BigInt(w * 65), true);
    });
    return { bytes, view, size: bytes.length + widths.length * 20 };
}

test('v4 TOC maps full SH0–3 attributes without whole-file allocation', () => {
    for (let degree = 0; degree <= 3; degree++) {
        const { bytes, size } = header(degree),
            layout = parseSpzV4(bytes, size, 65, degree);
        expect(layout.streams).toHaveLength(degree ? 6 : 5);
        expect(layout.streams[0]?.compressedOffset).toBe(bytes.length);
        expect(layout.streams[0]?.rawBytes).toBe(65 * 9);
        expect(layout.rawBytes).toBe(65 * (20 + 3 * ((degree + 1) ** 2 - 1)));
    }
});

test('v4 rejects changed header, unsafe uint64 sizes, truncation and trailing data', () => {
    const { bytes, view, size } = header();
    expect(() => parseSpzV4(bytes, size + 1, 65, 3)).toThrow('DecoderFailure');
    expect(() => parseSpzV4(bytes, size - 1, 65, 3)).toThrow('DecoderFailure');
    expect(() => parseSpzV4(bytes, size, 64, 3)).toThrow('DecoderFailure');
    expect(() => parseSpzV4(bytes.slice(0, -1), size, 65, 3)).toThrow('DecoderFailure');
    view.setBigUint64(32, 2n ** 63n, true);
    expect(() => parseSpzV4(bytes, size, 65, 3)).toThrow('DecoderFailure');
});

test('64 MiB CPU budget shrinks batches while preserving every point and SH3', () => {
    const full = streamingBudget(3904214, 3, 65, false, 1, 512 * 2 ** 20, 4 * 2 ** 20);
    const low = streamingBudget(3904214, 3, 65, false, 1, 64 * 2 ** 20, 4 * 2 ** 20);
    expect(full.batch).toBeGreaterThan(low.batch);
    expect(low.batch % 64).toBe(0);
    expect(low.cpuPeak).toBeLessThanOrEqual(64 * 2 ** 20);
    expect(() => streamingBudget(3904214, 3, 65, false, 1, 40 * 2 ** 20, 4 * 2 ** 20)).toThrow(
        'ResourceLimit',
    );
    expect(() => streamingBudget(3904214, 3, 65, false, 4, 64 * 2 ** 20, 4 * 2 ** 20)).toThrow(
        'ResourceLimit',
    );
    expect(() => streamingBudget(3904214, 3, 65, false, 1, 64 * 2 ** 20, 128 * 2 ** 20)).toThrow(
        'ResourceLimit',
    );
});
