import { chromium } from '@playwright/test';
import { mkdir, readFile, writeFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { createHash } from 'node:crypto';
import { verifyServedModel, verifyServedAssets } from './benchmark-provenance.mjs';
import { evaluateBenchmark, disposeBenchmark, validateTimingSamples } from './browser-benchmark-guards.mjs';

const base = process.env.GS_TEST_URL ?? 'http://127.0.0.1:5187';
const names = (
    process.env.GS_PAIR_MODELS ??
    'changjin_v1.ply,shengyi_v1.ply,spz/shengyi_v1.spz,tumu_v1.ply,spz/tumu_v1.spz,zhihuizhimen.ply,jiulonghu_v1.ply,spz/jiulonghu_v1.spz'
).split(',');
const runs = Number(process.env.GS_PAIR_RUNS ?? 3);
if (!Number.isSafeInteger(runs) || runs < 1) throw Error('Invalid benchmark run count');
const output =
    process.env.GS_PAIR_OUTPUT ?? 'docs/verification/evidence/pthreads-2026-10-06/spark-comparison.json';
const manifest = JSON.parse(await readFile('docs/verification/evidence/model-manifest.json', 'utf8'));
const assets = await Promise.all(
    [
        'public/assets/decoder.mjs',
        'public/assets/decoder.wasm',
        'public/assets/threaded/decoder.mjs',
        'public/assets/threaded/decoder.wasm',
        'apps/benchmark.ts',
        'src/model-io/streaming.ts',
        'node_modules/@sparkjsdev/spark/dist/spark.module.js',
    ].map(async (path) => ({
        path,
        sha256: createHash('sha256')
            .update(await readFile(path))
            .digest('hex'),
    })),
);
const profile = await mkdtemp(join(tmpdir(), 'gs-pthread-paired-'));
const context = await chromium
    .launchPersistentContext(profile, {
        channel: 'msedge',
        headless: true,
        viewport: { width: 1920, height: 1080 },
        deviceScaleFactor: 1,
    })
    .catch(async (e) => {
        await rm(profile, { recursive: true, force: true });
        throw e;
    });
const servedModels = [],
    servedAssets = [];
const rows = [],
    failures = [],
    configs = ['single', 'parallel', 'spark'];

const quantile = (xs, p) => {
    const s = xs.toSorted((a, b) => a - b);
    return s[Math.floor((s.length - 1) * p)];
};
const save = async () => {
    await mkdir(dirname(output), { recursive: true });
    await writeFile(
        output,
        JSON.stringify(
            {
                date: new Date().toISOString(),
                browser: context.browser().version(),
                runs,
                names,
                configs,
                resolution: [1920, 1080],
                spark: '2.3.1',
                localAssets: assets,
                servedModels,
                servedAssets,
                scope: 'Complete points/SH3, LoD disabled, identical world poses; 3 interleaved load repetitions; first run also 30 warmup + 120 completed frames x 3. Native stage GPU timestamps; Spark update includes Worker sort/readPause and its draw GPU timer has a different scope. Headless throughput protocol, not display FPS. Spark initializedMs combines network/decode/packing; no fabricated pure WASM stage split.',
                complete: rows.length === names.length * configs.length * runs && !failures.length,
                rows,
                failures,
            },
            null,
            2,
        ),
    );
};
try {
    servedAssets.push(...(await verifyServedAssets(base, assets)));
    for (const name of names) {
        const model = manifest.models.find((m) => m.name === name);
        if (!model) throw Error('Unknown model ' + name);
        servedModels.push(await verifyServedModel(base, model));
        let pose;
        for (let run = 0; run < runs; run++)
            for (const config of run % 2 ? [...configs].reverse() : configs) {
                const page = await context.newPage(),
                    errors = [];
                page.on('pageerror', (e) => errors.push(e.message));
                try {
                    await page.goto(
                        `${base}/benchmark.html?engine=${config === 'spark' ? 'spark' : 'native'}&complete=1&decoder=${config === 'parallel' ? 'parallel' : 'single'}&threads=4`,
                    );
                    await page.waitForFunction(() => window.bench, {}, { timeout: 60000 });
                    const loaded = await evaluateBenchmark(
                        page,
                        ({ name, pose }) => window.bench.load(name, pose),
                        { name, pose },
                    );
                    if (!pose) pose = loaded.pose;
                    if (loaded.count !== model.count || loaded.degree !== model.degree)
                        throw Error('Point/SH mismatch');
                    if (config === 'parallel' && loaded.decoder.backend !== 'pthreads')
                        throw Error('Unexpected fallback');
                    let stages = null;
                    if (run === 0) {
                        const samples = await evaluateBenchmark(page, () =>
                            window.bench.completed(30, 120, 3),
                        );
                        const metrics =
                            config === 'spark'
                                ? ['completedMs', 'cpuMs', 'gpuMs', 'updateMs']
                                : ['completedMs', 'cpuMs', 'gpuMs', 'projectionMs', 'sortMs', 'drawMs'];
                        validateTimingSamples(samples, 360, metrics);
                        stages = Array.from({ length: 3 }, (_, run) => ({
                            run,
                            metrics: Object.fromEntries(
                                metrics.map((key) => {
                                    const values = samples.filter((s) => s.run === run).map((s) => s[key]);
                                    return [key, { p50: quantile(values, 0.5), p95: quantile(values, 0.95) }];
                                }),
                            ),
                        }));
                    }
                    if (errors.length) throw Error(errors.join('\n'));
                    rows.push({ model: name, sha256: model.sha256, run, config, loaded, stages });
                    console.log(
                        JSON.stringify({
                            model: name,
                            run,
                            config,
                            firstFrameMs: loaded.firstFrameMs,
                            initializedMs: loaded.initializedMs,
                            stages,
                        }),
                    );
                } finally {
                    try {
                        await disposeBenchmark(page);
                    } finally {
                        await page.close();
                    }
                }
                await save();
            }
    }
} catch (e) {
    failures.push(String(e));
    console.error(e);
    process.exitCode = 1;
} finally {
    try {
        await save();
    } finally {
        try {
            await context.close();
        } finally {
            await rm(profile, { recursive: true, force: true });
        }
    }
}
