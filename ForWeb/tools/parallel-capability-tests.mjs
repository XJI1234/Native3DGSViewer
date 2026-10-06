import { chromium } from '@playwright/test';
import { mkdir, writeFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { evaluateBenchmark } from './browser-benchmark-guards.mjs';
const profile = await mkdtemp(join(tmpdir(), 'gs-pthread-lifecycle-'));
const context = await chromium.launchPersistentContext(profile, { channel: 'msedge', headless: true });
let page, cdp;
const rows = [];
const output =
    process.env.GS_CAPABILITY_OUTPUT ?? 'docs/verification/evidence/pthreads-2026-10-06/capabilities.json';
async function cleanWorkers() {
    for (let i = 0; i < 100; i++) {
        const { targetInfos } = await cdp.send('Target.getTargets');
        const workers = targetInfos.filter((t) => t.type.includes('worker') && t.url.includes('127.0.0.1'));
        if (!workers.length) return;
        await new Promise((r) => setTimeout(r, 50));
    }
    throw Error('Owned decoder/pthread targets remain after shutdown');
}
async function setup(isolated) {
    await page.goto(`http://127.0.0.1:${isolated ? 5187 : 5173}`);
    await page.waitForFunction(() => window.gs);
    await page.evaluate(() => window.gs.engine.dispose());
}
async function loadCase(label, options, expectation, stage, model = 'shengyi_v1.ply') {
    const result = await evaluateBenchmark(
        page,
        async ({ options, stage, model }) => {
            const canvas = document.createElement('canvas');
            canvas.width = 640;
            canvas.height = 480;
            document.body.append(canvas);
            const created = await window.gs.createEngine({
                canvas,
                assets: { baseUrl: new URL('/assets/', location.href) },
                ...options,
            });
            if (!created.ok) {
                canvas.remove();
                return { result: created };
            }
            const engine = created.value;
            let operation,
                cancelledAt = null;
            const unsubscribe = engine.subscribe(() => {
                const current = engine.getSnapshot().progress?.stage;
                if (stage && current === stage && operation) {
                    cancelledAt = current;
                    operation.cancel();
                }
            });
            try {
                operation = engine.open({
                    kind: 'url',
                    url: new URL(
                        '/models/' + (stage === 'Inflating' ? 'spz/jiulonghu_v1.spz' : model),
                        location.href,
                    ).href,
                });
                const result = await operation.result;
                return { result, decoder: engine.getSnapshot().decoder, cancelledAt };
            } finally {
                unsubscribe();
                await engine.dispose();
                canvas.remove();
                if (engine.getSnapshot().error?.stage === 'StorageCleanup')
                    throw Error(engine.getSnapshot().error.diagnostic);
                const root = await navigator.storage.getDirectory();
                for await (const name of root.keys())
                    if (name.startsWith('gs-')) throw Error('OPFS leaked ' + name);
            }
        },
        { options, stage, model },
        label,
    );
    expectation(result);
    await cleanWorkers();
    rows.push({ label, ...result, workersAfterShutdown: 0 });
    console.log('PASS', label);
}
const success = (backend) => (r) => {
    if (!r.result.ok || r.decoder.backend !== backend) throw Error(JSON.stringify(r));
};
const code = (expected) => (r) => {
    if (r.result.ok || r.result.error.code !== expected) throw Error(JSON.stringify(r));
};
try {
    page = await context.newPage();
    cdp = await context.newCDPSession(page);
    await setup(false);
    await loadCase('nonisolated legacy auto falls back', {}, success('single'));
    await loadCase(
        'nonisolated explicit parallel rejects',
        { decoder: { mode: 'parallel' } },
        code('UnsupportedCapability'),
    );
    await setup(true);
    await loadCase('isolated auto enables pthreads', {}, success('pthreads'));
    await loadCase('isolated explicit single retained', { decoder: { mode: 'single' } }, success('single'));
    await loadCase(
        'low CPU admission returns to base',
        { limits: { cpuBytes: 90 * 2 ** 20 } },
        success('single'),
    );
    await loadCase(
        'SPZ exact CPU admission retries base',
        { limits: { cpuBytes: 160 * 2 ** 20 } },
        (r) => {
            success('single')(r);
            if (!r.decoder.fallbackReason.includes('Streaming CPU admission'))
                throw Error('Missing admission fallback');
        },
        undefined,
        'spz/shengyi_v1.spz',
    );
    let abortedModules=0;
    await context.route('**/assets/threaded/decoder.mjs', (route) => {abortedModules++;return route.abort();});
    await loadCase('missing optional assets auto falls back', {}, (r) => {
        success('single')(r);
        if (!r.decoder.fallbackReason.includes('initialization failed'))
            throw Error('Missing fallback diagnostic');
    });
    await loadCase(
        'missing assets explicit parallel fails',
        { decoder: { mode: 'parallel' } },
        code('DecoderFailure'),
    );
    if(abortedModules<2)throw Error('Missing module route injection');
    await context.unroute('**/assets/threaded/decoder.mjs');
    const heldModules = [];
    await context.route('**/assets/threaded/decoder.mjs', (route) => {
        heldModules.push(route);
    });
    await loadCase('stalled pthread module import is bounded', {}, (r) => {
        success('single')(r);
        if (!r.decoder.fallbackReason.includes('Timeout')) throw Error('Missing module import timeout');
    });
    await context.unroute('**/assets/threaded/decoder.mjs');
    if(!heldModules.length)throw Error('Stalled module injection not reached');
    await Promise.allSettled(heldModules.map((route) => route.abort()));
    const held = [];
    await context.route('**/assets/threaded/decoder.wasm', (route) => {
        held.push(route);
    });
    await loadCase('stalled pthread initialization is bounded', {}, (r) => {
        success('single')(r);
        if (!r.decoder.fallbackReason.includes('Timeout')) throw Error('Missing initialization timeout');
    });
    await context.unroute('**/assets/threaded/decoder.wasm');
    if(!held.length)throw Error('Stalled WASM injection not reached');
    await Promise.allSettled(held.map((route) => route.abort()));
    for (const stage of ['Downloading', 'Decoding', 'Rebasing', 'Uploading', 'Inflating'])
        await loadCase(
            'cancel ' + stage,
            { decoder: { mode: 'parallel', threads: 4 } },
            (r) => {
                code('Cancelled')(r);
                if (r.cancelledAt !== stage) throw Error('Did not reach requested stage');
            },
            stage,
        );
    await loadCase(
        'bounded load timeout',
        { decoder: { mode: 'parallel' }, limits: { timeoutMs: 10 } },
        code('Timeout'),
    );
} finally {
    try {
        await mkdir(dirname(output), { recursive: true });
        await writeFile(
            output,
            JSON.stringify({ date: new Date().toISOString(), rows, complete: rows.length === 16 }, null, 2),
        );
    } finally {
        try {
            await context.close();
        } finally {
            await rm(profile, { recursive: true, force: true });
        }
    }
}
