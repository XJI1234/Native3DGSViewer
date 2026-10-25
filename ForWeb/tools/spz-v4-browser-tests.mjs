import assert from 'node:assert/strict';
import { chromium } from '@playwright/test';
import { constants, zstdCompressSync } from 'node:zlib';
import { mkdtemp, mkdir, rm, writeFile, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import { hashServed } from './benchmark-provenance.mjs';
import { evaluateBenchmark } from './browser-benchmark-guards.mjs';

const base = process.env.GS_TEST_URL ?? 'http://127.0.0.1:5173';
const output = process.env.GS_V4_OUTPUT ?? 'docs/verification/evidence/spz-v4-2026-10-25';
await mkdir(output, { recursive: true });
const profile = await mkdtemp(join(tmpdir(), 'gs-v4-tests-'));
const browser = await chromium.launchPersistentContext(profile, {
    channel: 'msedge', headless: true, viewport: { width: 960, height: 720 }, deviceScaleFactor: 1,
});
const page = await browser.newPage(), errors = [], rows = [], provenance = [];
page.on('pageerror', e => errors.push(e.message));
page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
const assets = [];
for (const path of ['decoder.wasm', 'decoder.mjs', 'threaded/decoder.wasm', 'threaded/decoder.mjs']) {
    const bytes = await readFile('public/assets/' + path);
    assets.push({ path, bytes: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') });
}
let synthetic, failure;
try {
    await page.goto(base); await page.waitForFunction(() => window.gs);
    await page.evaluate(async () => { await window.gs.engine.dispose(); });
    const gpu = await page.evaluate(async () => {
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter || adapter.info.isFallbackAdapter) throw Error('Hardware adapter required');
        return { vendor: adapter.info.vendor, architecture: adapter.info.architecture, description: adapter.info.description, isolated: crossOriginIsolated, hardwareThreads: navigator.hardwareConcurrency };
    });
    const models = [
        { name: 'spz/danganguan_spzv4.spz', count: 1859737, degree: 3 },
        { name: 'spz/zhihuizhimen_spzv4.spz', count: 3904214, degree: 3 },
        { name: 'spz/danganguan_v1.spz', count: 1888950, degree: 3 },
    ];
    for (const model of models) {
        provenance.push({ model: model.name, ...await hashServed(new URL('/models/' + model.name, base)) });
        let reference;
        const configs = model.name.includes('spzv4')
            ? [{ mode: 'single', cpuMiB: 512 }, { mode: 'auto', cpuMiB: 512 }, { mode: 'auto', cpuMiB: 64 }]
            : [{ mode: 'single', cpuMiB: 64 }];
        if (model.name.includes('spzv4') && gpu.isolated) configs.push({ mode: 'parallel', cpuMiB: 512 });
        for (const config of configs) {
            const row = await evaluateBenchmark(page, async ({ model, config, pose }) => {
                const canvas = document.createElement('canvas'); canvas.width = 640; canvas.height = 360;
                document.body.append(canvas);
                const result = await window.gs.createEngine({ canvas, assets: { baseUrl: new URL('/assets/', location.href) },
                    decoder: { mode: config.mode, threads: 4 }, limits: { cpuBytes: config.cpuMiB * 2 ** 20 } });
                if (!result.ok) throw Error(JSON.stringify(result));
                const engine = result.value; engine.pause();
                let stage = 'Inspecting', last = performance.now(); const stages = {};
                const unsubscribe = engine.subscribe(() => {
                    const next = engine.getSnapshot().progress?.stage ?? engine.getSnapshot().phase;
                    if (next !== stage) { stages[stage] = (stages[stage] ?? 0) + performance.now() - last; last = performance.now(); stage = next; }
                });
                const begin = performance.now();
                try {
                    const loaded = await engine.open({ kind: 'url', url: new URL('/models/' + model.name, location.href).href }).result;
                    if (!loaded.ok) throw Error(JSON.stringify(loaded));
                    const ms = performance.now() - begin;
                    const snapshot = engine.getSnapshot(), scene = engine.active.scene;
                    if (snapshot.sceneCount !== model.count || snapshot.degree !== model.degree) throw Error('Count/SH mismatch');
                    if (!snapshot.decoder?.batchPoints || !snapshot.decoder.wasmHeapBytes ||
                        snapshot.decoder.estimatedCpuPeakBytes > config.cpuMiB * 2 ** 20)
                        throw Error('Public allocation diagnostics missing or exceed budget');
                    if (pose) engine.camera.setPose(pose);
                    const captured = await engine.capture();
                    if (!captured.ok) throw Error(JSON.stringify(captured));
                    const { width, height, rgba } = captured.value;
                    const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', rgba))).map(v => v.toString(16).padStart(2, '0')).join('');
                    const image = document.createElement('canvas'); image.width = width; image.height = height;
                    image.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(rgba), width, height), 0, 0);
                    return { model: model.name, ...config, count: snapshot.sceneCount, degree: snapshot.degree, ms, stages,
                        decoder: snapshot.decoder, diagnostics: scene.decodeTimings, backingBytes: scene.backing.totalBytes,
                        gpuBytes: engine.active.bytes, pose: engine.camera.getPose(), imageSha256: digest,
                        bounds: { origin: scene.origin, min: scene.min, max: scene.max, maxScale: scene.maxScale },
                        png: image.toDataURL().split(',')[1] };
                } finally {
                    unsubscribe(); await engine.dispose(); canvas.remove();
                    if (engine.getSnapshot().error?.stage === 'StorageCleanup') throw Error(engine.getSnapshot().error.diagnostic);
                    const root = await navigator.storage.getDirectory();
                    for await (const name of root.keys()) if (name.startsWith('gs-')) throw Error('OPFS leak: ' + name);
                }
            }, { model, config, pose: reference?.pose });
            const png = row.png; delete row.png;
            const expectedParallel = config.mode === 'parallel' ||
                (config.mode === 'auto' && config.cpuMiB >= 128 && gpu.isolated && gpu.hardwareThreads >= 2);
            assert.equal(row.decoder.backend, expectedParallel ? 'pthreads' : 'single');
            assert.equal(row.decoder.threads, expectedParallel ? Math.min(4, gpu.hardwareThreads) : 1);
            if (reference) { assert.deepEqual(row.bounds, reference.bounds); assert.equal(row.imageSha256, reference.imageSha256); }
            else reference = row;
            const name = `${model.name.replaceAll('/', '_')}-${config.mode}-${config.cpuMiB}.png`;
            await writeFile(join(output, name), Buffer.from(png, 'base64'));
            rows.push(row); console.log(JSON.stringify(row));
        }
    }
    function fixture(degree, corrupt = '') {
        const count = 65, widths = [9, 1, 3, 3, 4, ...(degree ? [3 * ((degree + 1) ** 2 - 1)] : [])];
        const header = Buffer.alloc(32); header.writeUInt32LE(0x5053474e); header.writeUInt32LE(4, 4); header.writeUInt32LE(count, 8);
        header[12] = degree; header[13] = 12; header[15] = widths.length; header.writeUInt32LE(32, 16);
        const streams = widths.map((width, i) => zstdCompressSync(Buffer.alloc(width * count, i === 0 || i === 4 ? 0 : 128),
            { params: { [constants.ZSTD_c_checksumFlag]: 1 } }));
        if (corrupt === 'checksum') streams[0][streams[0].length - 1] ^= 1;
        if (corrupt === 'truncated') streams[0] = streams[0].subarray(0, -1);
        if (corrupt === 'trailing') streams[0] = Buffer.concat([streams[0], streams[0]]);
        if (corrupt === 'length') streams[0] = zstdCompressSync(Buffer.alloc(widths[0] * count + 1));
        const toc = Buffer.alloc(widths.length * 16);
        streams.forEach((s, i) => { toc.writeBigUInt64LE(BigInt(s.length), i * 16); toc.writeBigUInt64LE(BigInt(widths[i] * count), i * 16 + 8); });
        return Buffer.concat([header, toc, ...streams]).toString('base64');
    }
    synthetic = await evaluateBenchmark(page, async ({ valid, invalid }) => {
        const canvas = document.createElement('canvas'); canvas.width = canvas.height = 64; document.body.append(canvas);
        const made = await window.gs.createEngine({ canvas, assets: { baseUrl: new URL('/assets/', location.href) }, limits: { cpuBytes: 64 * 2 ** 20 } });
        if (!made.ok) throw Error(JSON.stringify(made));
        const engine = made.value; engine.pause();
        const source = base64 => ({ kind: 'blob', blob: new Blob([Uint8Array.from(atob(base64), c => c.charCodeAt(0))]) });
        const failures = [];
        try {
            for (let degree = 0; degree < valid.length; degree++) {
                const r = await engine.open(source(valid[degree])).result;
                if (!r.ok || engine.getSnapshot().degree !== degree || engine.getSnapshot().sceneCount !== 65) throw Error('Synthetic v4 ' + JSON.stringify(r));
            }
            for (const base64 of invalid) {
                const r = await engine.open(source(base64)).result;
                if (r.ok || r.error.code !== 'DecoderFailure' || engine.getSnapshot().sceneCount !== 65) throw Error('Corrupt replaced scene: ' + JSON.stringify(r));
                failures.push(r.error);
            }
            const operation = engine.open({ kind: 'url', url: new URL('/models/spz/zhihuizhimen_spzv4.spz', location.href).href });
            let cancelled = false;
            const unsubscribe = engine.subscribe(() => { if (engine.getSnapshot().progress?.stage === 'Inflating') { cancelled = true; operation.cancel(); } });
            const result = await operation.result; unsubscribe();
            if (!cancelled || result.ok || result.error.code !== 'Cancelled' || engine.getSnapshot().sceneCount !== 65) throw Error('Inflate cancel did not preserve scene');
            return { degrees: [0, 1, 2, 3], failures, cancel: result.error, oldScenePreserved: true };
        } finally {
            await engine.dispose(); canvas.remove();
            const root = await navigator.storage.getDirectory();
            for await (const name of root.keys()) if (name.startsWith('gs-')) throw Error('OPFS leak: ' + name);
        }
    }, { valid: [0, 1, 2, 3].map(d => fixture(d)), invalid: ['checksum', 'truncated', 'trailing', 'length'].map(c => fixture(3, c)) });
    assert.deepEqual(errors, []);
    await writeFile(join(output, 'results.json'), JSON.stringify({ date: new Date().toISOString(), browser: browser.browser().version(), gpu,
        scope: 'Full points/SH, fixed 640x360 RGBA, ordinary disk-backed profile; CPU admission is allocation estimate, not RSS or physical RAM limit.',
        provenance, assets, rows, synthetic, errors }, null, 2));
} catch (e) {
    failure = String(e); process.exitCode = 1; console.error(e);
    await writeFile(join(output, 'failed.json'), JSON.stringify({ failure, rows, provenance, assets, synthetic, errors }, null, 2));
} finally {
    await browser.close(); await rm(profile, { recursive: true, force: true });
}
