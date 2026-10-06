import { test } from 'node:test';
import assert from 'node:assert/strict';
import { gzipSync } from 'node:zlib';
import singleFactory from '../../public/assets/decoder.mjs';
import parallelFactory from '../../public/assets/threaded/decoder.mjs';

const single = await singleFactory();
const parallel = await parallelFactory({ pthreadPoolSize: 7 });
function put(wasm, bytes, fn) {
    const p = wasm._malloc(bytes.length);
    assert.ok(p);
    try {
        wasm.HEAPU8.set(bytes, p);
        return fn(p);
    } finally {
        wasm._free(p);
    }
}
function syntheticPly(count, degree) {
    const rest = 3 * ((degree + 1) ** 2 - 1);
    const names = [
        'x',
        'y',
        'z',
        'scale_0',
        'scale_1',
        'scale_2',
        'rot_1',
        'rot_2',
        'rot_3',
        'rot_0',
        'f_dc_0',
        'f_dc_1',
        'f_dc_2',
        'opacity',
        ...Array.from({ length: rest }, (_, i) => 'f_rest_' + i),
    ];
    const header = Buffer.from(
        'ply\nformat binary_little_endian 1.0\nelement vertex ' +
            count +
            '\n' +
            names.map((n) => 'property float ' + n + '\n').join('') +
            'end_header\n',
    );
    const body = new Float32Array(count * names.length);
    for (let i = 0; i < count; i++) {
        const at = i * names.length;
        body.set([i / 256 - 3, Math.sin(i), i % 17, -2, -3, -4, 0.1, 0.2, 0.3, 1, 0.4, 0.5, 0.6, 0.2], at);
        for (let k = 0; k < rest; k++) body[at + 14 + k] = (k + 1) * 0.01;
    }
    return { input: Buffer.concat([header, Buffer.from(body.buffer)]), body: new Uint8Array(body.buffer) };
}
function decode(wasm, input, body, count, degree, threads, version = 0, rdf = 1, fractional = 12) {
    assert.equal(wasm._gs_set_threads(threads), 1);
    assert.equal(
        put(wasm, input, (p) => wasm._gs_probe(p, input.length, input.length, 1e9)),
        1,
    );
    let p;
    if (wasm === single) {
        assert.equal(wasm._gs_begin_batch(rdf, count), 1);
        assert.equal(
            put(wasm, body, (p) =>
                version
                    ? wasm._gs_raw_spz(p, body.length, version, fractional)
                    : wasm._gs_chunk(p, body.length),
            ),
            1,
        );
        p = wasm._gs_pack_tiled(0, count);
        assert.equal(wasm._gs_finish_batch(), 1);
    } else {
        p = put(wasm, body, (p) => wasm._gs_decode_batch(p, body.length, rdf, count, version, fractional));
    }
    assert.ok(p, wasm.UTF8ToString(wasm._gs_error()));
    const result = {
        bytes: Buffer.from(
            wasm.HEAPU8.slice(p, p + Math.ceil(count / 64) * 64 * (56 + 12 * ((degree + 1) ** 2 - 1))),
        ),
        bounds: Array.from({ length: 10 }, (_, i) => wasm._gs_meta(i)),
    };
    wasm._free(p);
    wasm._gs_release();
    return result;
}
test('pthreads preserves all PLY attributes, global bounds and tile padding at every SH degree', () => {
    for (const count of [1, 63, 64, 65, 127, 129, 65536])
        for (const degree of [0, 1, 2, 3])
            for (const rdf of [0, 1]) {
                const { input, body } = syntheticPly(count, degree);
                const expected = decode(single, input, body, count, degree, 1, 0, rdf);
                for (const threads of [1, 2, 4, 8])
                    assert.deepEqual(
                        decode(parallel, input, body, count, degree, threads, 0, rdf),
                        expected,
                        `n=${count} SH${degree} t${threads} rdf${rdf}`,
                    );
            }
});
test('pthreads preserves SPZ v1–3 and SH0–3 batch unpack mathematics', () => {
    const count = 129;
    for (const version of [1, 2, 3])
        for (const degree of [0, 1, 2, 3])
            for (const fractional of [12, 25, 30]) {
                const header = Buffer.alloc(16);
                header.writeUInt32LE(0x5053474e);
                header.writeUInt32LE(version, 4);
                header.writeUInt32LE(count, 8);
                header[12] = degree;
                header[13] = fractional;
                const widths = [
                    version === 1 ? 6 : 9,
                    1,
                    3,
                    3,
                    version >= 3 ? 4 : 3,
                    3 * ((degree + 1) ** 2 - 1),
                ];
                const attrs = widths.map((width) => Buffer.alloc(count * width));
                for (let i = 0; i < count; i++) {
                    for (let k = 0; k < 3; k++) {
                        if (version === 1)
                            attrs[0].writeUInt16LE(
                                (i % 2 ? 0x8000 : 0) + 0x3c00 + (i % 32) * 8 + k,
                                6 * i + 2 * k,
                            );
                        else attrs[0].writeIntLE((i - 64) * (k + 1) * 1000, 9 * i + 3 * k, 3);
                        attrs[2][3 * i + k] = 80 + ((i * 3 + k * 7) % 120);
                        attrs[3][3 * i + k] = 80 + ((i + k * 3) % 50);
                        if (version < 3) attrs[4][3 * i + k] = 120 + ((i + k) % 15);
                    }
                    attrs[1][i] = 32 + (i % 128);
                    if (version >= 3)
                        attrs[4].writeUInt32LE(
                            (3 * 2 ** 30 +
                                (20 + (i % 90)) * 2 ** 20 +
                                (20 + ((i * 3) % 90)) * 2 ** 10 +
                                20 +
                                ((i * 7) % 90)) >>>
                                0,
                            4 * i,
                        );
                    for (let k = 0; k < widths[5]; k++)
                        attrs[5][i * widths[5] + k] = 64 + ((i * 7 + k * 11) % 128);
                }
                const body = Buffer.concat(attrs);
                const input = gzipSync(Buffer.concat([header, body]));
                const expected = decode(single, input, body, count, degree, 1, version, 0, fractional);
                for (const threads of [2, 4, 8])
                    assert.deepEqual(
                        decode(parallel, input, body, count, degree, threads, version, 0, fractional),
                        expected,
                    );
            }
});
test('parallel failures reject malformed attributes/lengths and permit a subsequent valid batch', () => {
    const { input, body } = syntheticPly(129, 3);
    assert.equal(parallel._gs_set_threads(4), 1);
    assert.equal(
        put(parallel, input, (p) => parallel._gs_probe(p, input.length, input.length, 1e9)),
        1,
    );
    assert.equal(
        put(parallel, body, (p) => parallel._gs_decode_batch(p, body.length - 1, 1, 129, 0, 0)),
        0,
    );
    const bad = body.slice();
    new DataView(bad.buffer).setFloat32(128 * (14 + 45) * 4, NaN, true);
    assert.equal(
        put(parallel, bad, (p) => parallel._gs_decode_batch(p, bad.length, 1, 129, 0, 0)),
        0,
    );
    assert.deepEqual(decode(parallel, input, body, 129, 3, 4), decode(single, input, body, 129, 3, 1));
});
test('parallel rebase is byte-identical to scalar across tile partitions', () => {
    for (const count of [0, 64, 128, 65536])
        for (const threads of [1, 2, 4, 8]) {
            assert.equal(parallel._gs_set_threads(threads), 1);
            const bytes = Buffer.from(Float32Array.from({ length: count * 59 }, (_, i) => i % 103).buffer);
            const expected = put(single, bytes, (p) => {
                assert.equal(single._gs_rebase_tiled(p, count, 236, 0.25, -123.3, 0.5), 1);
                return Buffer.from(single.HEAPU8.slice(p, p + bytes.length));
            });
            const actual = put(parallel, bytes, (p) => {
                assert.equal(parallel._gs_rebase_parallel(p, count, 236, 0.25, -123.3, 0.5), 1);
                return Buffer.from(parallel.HEAPU8.slice(p, p + bytes.length));
            });
            assert.deepEqual(actual, expected);
        }
});
test.after(() => {
    single._gs_release();
    parallel._gs_release();
    parallel.PThread.terminateAllThreads();
});
