/** Host-side guards for development benchmarks. These are not shipped in the SDK. */
export async function withBenchmarkDeadline(page, work, label, timeoutMs = Number(process.env.GS_BENCH_TIMEOUT_MS ?? 600000)) {
    if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0) throw Error('Invalid benchmark timeout');
    let timer;
    try {
        return await Promise.race([work, new Promise((_, reject) => {
            timer = setTimeout(() => {
                reject(Error(`BenchmarkTimeout: ${label} after ${timeoutMs}ms`));
                void page.close().catch(() => {});
            }, timeoutMs);
        })]);
    } finally { clearTimeout(timer); }
}
export function evaluateBenchmark(page, fn, argument, label = 'browser evaluation') {
    return withBenchmarkDeadline(page, page.evaluate(fn, argument), label);
}
export async function disposeBenchmark(page) {
    if (page.isClosed()) return;
    const result = await withBenchmarkDeadline(page, page.evaluate(() => window.bench?.dispose()), 'engine cleanup', 5000);
    if (result?.error) throw Error(String(result.error));
}
export function validateTimingSamples(samples, expected, metrics) {
    if (samples.length !== expected) throw Error(`Expected ${expected} completed timing samples, got ${samples.length}`);
    let previousFrame=-1;
    for (const sample of samples) {
        if (!Number.isSafeInteger(sample.frameId) || sample.gpuFrameId !== sample.frameId) throw Error('Missing/stale timing frame attribution');
        if(sample.frameId<=previousFrame)throw Error('Duplicate/out-of-order completed frame');
        previousFrame=sample.frameId;
        for (const metric of metrics) if (!Number.isFinite(sample[metric]) || sample[metric] < 0) throw Error(`Unavailable/invalid ${metric}`);
    }
}
