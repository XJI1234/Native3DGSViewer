import { chromium } from '@playwright/test';
import { writeFile } from 'node:fs/promises';
const browser = await chromium.launch({ channel: 'msedge', headless: true });
try {
    const page = await browser.newPage();
    await page.goto('http://127.0.0.1:5173');
    await page.waitForFunction(() => window.gs);
    const result = await page.evaluate(async () => {
        const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
        const device = await adapter.requestDevice({ requiredLimits: {
            maxBufferSize: adapter.limits.maxBufferSize,
            maxStorageBufferBindingSize: adapter.limits.maxStorageBufferBindingSize,
        } });
        const rows = [];
        try {
            for (const n of [16777217, 22480361]) {
                const bins = 4093;
                const keys = Uint32Array.from({ length: n }, (_, i) => i % bins);
                const start = performance.now();
                device.pushErrorScope('validation');
                const pairs = await window.gs.testSort(device, keys, 4);
                const error = await device.popErrorScope();
                if (error) throw Error(error.message);
                let cursor = 0;
                for (let key = 0; key < bins; key++) for (let index = key; index < n; index += bins) {
                    if (pairs[cursor * 2] !== key || pairs[cursor * 2 + 1] !== index)
                        throw Error(`Stable reference mismatch at ${n}/${cursor}`);
                    cursor++;
                }
                rows.push({ n, checked: cursor, stable: true, ms: performance.now() - start });
            }
            return { rows, adapter: { vendor: adapter.info.vendor, architecture: adapter.info.architecture } };
        } finally { device.destroy(); }
    });
    await writeFile('docs/verification/evidence/large-sort-tests.json', JSON.stringify(result, null, 2));
    console.log(result);
} finally { await browser.close(); }
