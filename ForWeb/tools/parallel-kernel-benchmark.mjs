import { open, readFile, mkdir, writeFile } from 'node:fs/promises';
import { performance } from 'node:perf_hooks';
import { createHash } from 'node:crypto';
import { cpus } from 'node:os';
import { createReadStream } from 'node:fs';
import { join } from 'node:path';
import { resolve, dirname } from 'node:path';
import { pathToFileURL } from 'node:url';
const baselinePath = resolve(process.env.GS_BASELINE_DECODER ?? '.local/parallel-baseline/decoder.mjs');
const baseFactory = (await import(pathToFileURL(baselinePath).href)).default;
import singleFactory from '../public/assets/decoder.mjs';
import threadedFactory from '../public/assets/threaded/decoder.mjs';

const root = process.env.GS_MODEL_ROOT ?? 'C:/Users/21544/Desktop/zhishan';
const model = process.env.GS_DECODE_MODEL ?? 'shengyi_v1.ply';
const output =
    process.env.GS_KERNEL_OUTPUT ?? 'docs/verification/evidence/pthreads-2026-10-06/kernels-first.json';
const manifest = JSON.parse(await readFile('docs/verification/evidence/model-manifest.json', 'utf8'));
const entry = manifest.models.find((item) => item.name === model);
if (!entry) throw Error('Kernel model is absent from manifest');
const modelHash = createHash('sha256');
for await (const chunk of createReadStream(join(root, model))) modelHash.update(chunk);
const modelSha256 = modelHash.digest('hex');
if (modelSha256 !== entry.sha256) throw Error('Kernel model differs from manifest');
const assets = await Promise.all(
    [
        baselinePath,
        join(dirname(baselinePath), 'decoder.wasm'),
        'public/assets/decoder.mjs',
        'public/assets/decoder.wasm',
        'public/assets/threaded/decoder.mjs',
        'public/assets/threaded/decoder.wasm',
    ].map(async (path) => ({
        path,
        sha256: createHash('sha256')
            .update(await readFile(path))
            .digest('hex'),
    })),
);
const file = await open(root + '/' + model);
const prefix = Buffer.alloc(1024 * 1024);
const { bytesRead } = await file.read(prefix, 0, prefix.length, 0);
const total = (await file.stat()).size;
const baselineInitStart = performance.now();
const base = await baseFactory();
const baselineInitializationMs = performance.now() - baselineInitStart;
const rows = [];
const put = (wasm, bytes, fn, observeCopy) => {
    const copyBegin = performance.now();
    const p = wasm._malloc(bytes.length);
    if (!p) throw Error('OOM');
    try {
        wasm.HEAPU8.set(bytes, p);
        observeCopy?.(performance.now() - copyBegin);
        return fn(p);
    } finally {
        wasm._free(p);
    }
};
const check = (wasm, ok) => {
    if (!ok) throw Error(wasm.UTF8ToString(wasm._gs_error()));
};
const probe = (wasm) => {
    check(
        wasm,
        put(wasm, prefix.subarray(0, bytesRead), (p) => wasm._gs_probe(p, bytesRead, total, 8 * 2 ** 30)),
    );
};
probe(base);
check(base, base._gs_begin_batch(1, 1));
const stride = base._gs_meta(11),
    offset = base._gs_meta(10),
    degree = base._gs_degree();
const count = Math.min(base._gs_count(), Math.floor((4 * 2 ** 20) / stride / 64) * 64, 65536);
const body = Buffer.alloc(count * stride);
for (let read = 0; read < body.length; ) {
    const { bytesRead } = await file.read(body, read, body.length - read, offset + read);
    if (!bytesRead) throw Error('Unexpected model EOF');
    read += bytesRead;
}
await file.close();
base._gs_release();
const rebaseOffset = [1.25, -2.5, 0.375];
let reference;
try {
    for (const config of ['baseline', 1, 2, 4, 8]) {
        const init = performance.now();
        let wasm;
        if (config === 'baseline') wasm = base;
        else if (config === 1) wasm = await singleFactory();
        else wasm = await threadedFactory({ pthreadPoolSize: config - 1 });
        const initializationMs = config === 'baseline' ? baselineInitializationMs : performance.now() - init;
        if (config !== 'baseline') check(wasm, wasm._gs_set_threads(config));
        const samples = [];
        try {
            for (let run = -2; run < 12; run++) {
                probe(wasm);
                const start = performance.now();
                let p, decodeMs, finishPackMs, inputCopyMs;
                const observeCopy = (ms) => {
                    inputCopyMs = ms;
                };
                if (config === 'baseline') {
                    const t = performance.now();
                    check(wasm, wasm._gs_begin_batch(1, count));
                    check(
                        wasm,
                        put(wasm, body, (p) => wasm._gs_chunk(p, body.length), observeCopy),
                    );
                    decodeMs = performance.now() - t - inputCopyMs;
                    const finish = performance.now();
                    p = wasm._gs_pack_tiled(0, count);
                    check(wasm, wasm._gs_finish_batch());
                    finishPackMs = performance.now() - finish;
                } else {
                    p = put(
                        wasm,
                        body,
                        (p) => wasm._gs_decode_batch(p, body.length, 1, count, 0, 0),
                        observeCopy,
                    );
                    decodeMs = wasm._gs_timing(0);
                    finishPackMs = wasm._gs_timing(1);
                }
                check(wasm, p);
                const wallMs = performance.now() - start;
                const packed = wasm.HEAPU8.slice(
                    p,
                    p + Math.ceil(count / 64) * 64 * (56 + 12 * ((degree + 1) ** 2 - 1)),
                );
                const hash = createHash('sha256').update(packed).digest('hex');
                const bounds = Array.from({ length: 10 }, (_, i) => wasm._gs_meta(i));
                if (!reference) reference = { hash, bounds };
                if (hash !== reference.hash || JSON.stringify(bounds) !== JSON.stringify(reference.bounds))
                    throw Error('Output mismatch');
                const r = wasm._malloc(packed.length);
                if (!r) throw Error('WASM rebase allocation failed');
                let rebaseMs;
                try {
                wasm.HEAPU8.set(packed, r);
                const rebaseStart = performance.now();
                check(
                    wasm,
                    config === 'baseline'
                        ? wasm._gs_rebase_tiled(
                              r,
                              Math.ceil(count / 64) * 64,
                              56 + 12 * ((degree + 1) ** 2 - 1),
                              ...rebaseOffset,
                          )
                        : wasm._gs_rebase_parallel(
                              r,
                              Math.ceil(count / 64) * 64,
                              56 + 12 * ((degree + 1) ** 2 - 1),
                              ...rebaseOffset,
                          ),
                );
                rebaseMs = performance.now() - rebaseStart;
                const rebasedHash = createHash('sha256')
                    .update(wasm.HEAPU8.subarray(r, r + packed.length))
                    .digest('hex');
                if (!reference.rebasedHash) reference.rebasedHash = rebasedHash;
                if (rebasedHash !== reference.rebasedHash) throw Error('Rebased output mismatch');
                } finally { wasm._free(r); }
                wasm._free(p);
                wasm._gs_release();
                if (run >= 0) samples.push({ wallMs, inputCopyMs, decodeMs, finishPackMs, rebaseMs });
            }
            const summary = Object.fromEntries(
                ['wallMs', 'inputCopyMs', 'decodeMs', 'finishPackMs', 'rebaseMs'].map((key) => {
                    const v = samples.map((s) => s[key]).sort((a, b) => a - b);
                    return [key, { p50: v[Math.floor(v.length / 2)], min: v[0], max: v.at(-1) }];
                }),
            );
            const scopes =
                config === 'baseline'
                    ? {
                          decodeMs:
                              'JS wall: begin_batch/chunk excluding separately measured malloc/input copy; not a native task clock',
                          finishPackMs: 'JS wall: pack and finish_batch',
                      }
                    : {
                          decodeMs:
                              'Native task maximum: begin_batch/chunk, excludes input copy and dispatch',
                          finishPackMs: 'Native task maximum: finish_batch and direct pack',
                      };
            rows.push({ config, initializationMs, scopes, samples, summary });
            console.log(JSON.stringify({ config, initializationMs, summary }));
        } finally {
            wasm._gs_release();
            wasm.PThread?.terminateAllThreads();
        }
    }
} finally {
    base._gs_release();
}
await mkdir(dirname(output), { recursive: true });
await writeFile(
    output,
    JSON.stringify(
        {
            date: new Date().toISOString(),
            baselinePath,
            baselineSha256: createHash('sha256')
                .update(await readFile(baselinePath))
                .digest('hex'),
            cpu: cpus()[0].model,
            node: process.version,
            model,
            modelSha256,
            assets,
            inputSha256: createHash('sha256').update(body).digest('hex'),
            count,
            degree,
            inputBytes: body.length,
            scope: 'Node WASM preloaded 4MiB source batch; wall includes memcpy/dispatch and is comparable; baseline JS phase scopes differ from native task maxima and MUST NOT be compared as decode-only speedups; per-task maxima are not additive; separate initialization and rebase; 2 warmups/12 repeats',
            rebaseOffset,
            reference,
            rows,
        },
        null,
        2,
    ),
);
