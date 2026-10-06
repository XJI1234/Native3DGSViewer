import { chromium } from '@playwright/test';
import { mkdir, readFile, writeFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { createHash } from 'node:crypto';
import { verifyServedModel } from './benchmark-provenance.mjs';
import { evaluateBenchmark } from './browser-benchmark-guards.mjs';

const url = process.env.GS_TEST_URL ?? 'http://127.0.0.1:5187';
const output =
    process.env.GS_PARALLEL_OUTPUT ?? 'docs/verification/evidence/pthreads-2026-10-06/browser-smoke.json';
const names = (process.env.GS_PARALLEL_MODELS ?? 'changjin_v1.ply,shengyi_v1.ply,spz/shengyi_v1.spz').split(
    ',',
);
const configs = (process.env.GS_PARALLEL_CONFIGS ?? 'single,2,4,8').split(',');
const runs = Number(process.env.GS_PARALLEL_RUNS ?? 1);
if (!Number.isSafeInteger(runs) || runs < 1) throw Error('Runs must be a positive safe integer');
if (
    !configs.includes('single') ||
    new Set(configs).size !== configs.length ||
    configs.some((c) => !['single', 'auto', '2', '4', '8'].includes(c))
)
    throw Error('Configurations must include single and contain unique supported values');
const manifest = JSON.parse(await readFile('docs/verification/evidence/model-manifest.json', 'utf8'));
const models =
    names[0] === 'all'
        ? manifest.models
        : names.map((name) => {
              const m = manifest.models.find((m) => m.name === name);
              if (!m) throw Error('Unknown model ' + name);
              return m;
          });
const profile = await mkdtemp(join(tmpdir(), 'gs-pthread-tests-'));
const browser = await chromium.launchPersistentContext(profile, {
    channel: 'msedge',
    headless: true,
    viewport: { width: 1920, height: 1080 },
    deviceScaleFactor: 1,
});
const servedModels = [];
const page = await browser.newPage(),
    errors = [],
    rows = [],
    failures = [];
page.on('pageerror', (e) => errors.push(e.message));
const save = async () => {
    await mkdir(dirname(output), { recursive: true });
    await writeFile(
        output,
        JSON.stringify(
            {
                date: new Date().toISOString(),
                browser: browser.browser().version(),
                url,
                runs,
                configs,
                requested: models.map((m) => m.name),
                scope: 'End-to-end URL load through first validated GPU frame; stage boundaries include I/O; capture outside timings; fresh ordinary profile; complete count/SH',
                complete:
                    rows.length === models.length * configs.length * runs &&
                    !failures.length &&
                    !errors.length,
                servedModels,
                rows,
                failures,
                errors,
            },
            null,
            2,
        ),
    );
};
try {
    await page.goto(url);
    await page.waitForFunction(() => window.gs);
    await page.evaluate(() => window.gs.engine.dispose());
    for (const model of models) {
        servedModels.push(await verifyServedModel(url, model));
        let reference;
        for (let run = 0; run < runs; run++)
            for (const config of run % 2 ? [...configs].reverse() : configs) {
                const row = await evaluateBenchmark(
                    page,
                    async ({ model, config, pose }) => {
                        const canvas = document.createElement('canvas');
                        canvas.width = 1920;
                        canvas.height = 1080;
                        document.body.append(canvas);
                        let mode = 'parallel';
                        if (config === 'single') mode = 'single';
                        else if (config === 'auto') mode = 'auto';
                        const created = await window.gs.createEngine({
                            canvas,
                            assets: { baseUrl: new URL('/assets/', location.href) },
                            decoder: { mode, threads: mode === 'parallel' ? Number(config) : 4 },
                        });
                        if (!created.ok) throw Error(JSON.stringify(created));
                        const engine = created.value;
                        engine.pause();
                        const stages = {};
                        let stage = 'Inspecting',
                            last = performance.now();
                        const unsubscribe = engine.subscribe(() => {
                            const next = engine.getSnapshot().progress?.stage ?? engine.getSnapshot().phase;
                            if (next !== stage) {
                                stages[stage] = (stages[stage] ?? 0) + performance.now() - last;
                                last = performance.now();
                                stage = next;
                            }
                        });
                        const start = performance.now();
                        try {
                            const result = await engine.open({
                                kind: 'url',
                                url: new URL(
                                    '/models/' + model.name.split('/').map(encodeURIComponent).join('/'),
                                    location.href,
                                ).href,
                            }).result;
                            const ms = performance.now() - start;
                            unsubscribe();
                            stages[stage] = (stages[stage] ?? 0) + performance.now() - last;
                            if (!result.ok) throw Error(JSON.stringify(result));
                            const snapshot = engine.getSnapshot(),
                                scene = engine.active.scene;
                            if (snapshot.sceneCount !== model.count || snapshot.degree !== model.degree)
                                throw Error('Count/SH mismatch');
                            if (pose) engine.camera.setPose(pose);
                            const capture = await engine.capture();
                            if (!capture.ok) throw Error(JSON.stringify(capture));
                            const digest = Array.from(
                                new Uint8Array(await crypto.subtle.digest('SHA-256', capture.value.rgba)),
                            )
                                .map((x) => x.toString(16).padStart(2, '0'))
                                .join('');
                            return {
                                model: model.name,
                                sha256: model.sha256,
                                config,
                                count: snapshot.sceneCount,
                                degree: snapshot.degree,
                                ms,
                                stages,
                                decoder: snapshot.decoder,
                                timings: scene.decodeTimings,
                                bounds: {
                                    origin: scene.origin,
                                    min: scene.min,
                                    max: scene.max,
                                    maxScale: scene.maxScale,
                                },
                                pose: engine.camera.getPose(),
                                imageSha256: digest,
                                gpuBytes: engine.active.bytes,
                            };
                        } finally {
                            unsubscribe();
                            await engine.dispose();
                            canvas.remove();
                            if (
                                engine.getSnapshot().phase !== 'Stopped' ||
                                engine.getSnapshot().decoder !== null
                            )
                                throw Error('Shutdown diagnostic state');
                            if (engine.getSnapshot().error?.stage === 'StorageCleanup')
                                throw Error(engine.getSnapshot().error.diagnostic);
                            const root = await navigator.storage.getDirectory();
                            for await (const name of root.keys())
                                if (name.startsWith('gs-')) throw Error('Storage leak ' + name);
                        }
                    },
                    { model, config, pose: reference?.pose },
                    'parallel model load',
                );
                if (!reference) reference = row;
                if (
                    JSON.stringify(row.bounds) !== JSON.stringify(reference.bounds) ||
                    row.imageSha256 !== reference.imageSha256
                )
                    throw Error('Single/parallel bounds or image mismatch ' + model.name);
                if (config === 'single' && row.decoder.backend !== 'single') throw Error('Explicit single selection did not use single');
                if (config !== 'single' && config !== 'auto' && row.decoder.backend !== 'pthreads')
                    throw Error('Parallel benchmark unexpectedly fell back');
                rows.push({ ...row, run });
                console.log(
                    JSON.stringify({
                        model: model.name,
                        run,
                        config,
                        ms: row.ms,
                        decoder: row.decoder,
                        timings: row.timings,
                    }),
                );
                await save();
            }
    }
    if (errors.length) throw Error(errors.join('\n'));
} catch (e) {
    failures.push(String(e));
    console.error(e);
    process.exitCode = 1;
} finally {
    try {
        await save();
    } finally {
        try {
            await browser.close();
        } finally {
            await rm(profile, { recursive: true, force: true });
        }
    }
}
