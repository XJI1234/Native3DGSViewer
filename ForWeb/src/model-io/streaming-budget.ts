const chunkBytes = 4 * 2 ** 20;

// Estimates engine allocations, not browser/OS file caches or externally owned Blobs.
export function streamingBudget(
    count: number,
    degree: number,
    sourceStride: number,
    ply: boolean,
    threads: number,
    available: number,
    windowBytes = 0,
): { batch: number; cpuPeak: number } {
    const stride = 56 + 12 * ((degree + 1) ** 2 - 1);
    const capacity = Math.min(65536, Math.floor(chunkBytes / sourceStride));
    let batch = capacity >= 64 ? Math.floor(capacity / 64) * 64 : capacity;
    if (!batch || (batch < 64 && count > batch))
        throw Error('ResourceLimit: source record exceeds batch capacity');
    const runtime = 32 * 2 ** 20 + (threads > 1 ? 32 * 2 ** 20 + threads * 2 * 2 ** 20 : 0);
    // Inflate and normalize run sequentially; allocator high-water must fit either phase.
    const inflate = Math.ceil((windowBytes + 2 * chunkBytes + 2 * 2 ** 20) * 1.5) + chunkBytes + 2 ** 20;
    while (true) {
        const n = Math.min(count, batch),
            input = n * sourceStride;
        const normalized = n * (stride + 8) + 256;
        const scratch = ply ? 2 * normalized + input : 3 * normalized + 2 * input;
        const decode =
            Math.max(16 * 2 ** 20, Math.ceil(scratch * 1.5)) +
            (threads > 1 ? 4 : 2) * input +
            Math.ceil(n / 64) * 64 * stride +
            chunkBytes +
            2 ** 20;
        const cpuPeak = runtime + Math.max(decode, ply ? 0 : inflate);
        if (cpuPeak <= available) return { batch, cpuPeak };
        if (batch <= 64 || count <= 64) throw Error('ResourceLimit: streaming CPU peak');
        batch = Math.max(64, Math.floor(batch / 128) * 64);
    }
}
