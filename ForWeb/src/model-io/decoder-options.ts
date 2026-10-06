import type { DecoderInfo, DecoderOptions } from '../splat-types/index';

export type { DecoderInfo, DecoderOptions } from '../splat-types/index';
export function validateDecoderOptions(options: DecoderOptions): void {
    if (options.mode !== undefined && !['auto', 'single', 'parallel'].includes(options.mode))
        throw Error('InvalidInput: decoder mode');
    if (
        options.threads !== undefined &&
        (!Number.isInteger(options.threads) || options.threads < 1 || options.threads > 8)
    )
        throw Error('InvalidInput: decoder threads must be an integer from 1 to 8');
}
export function decoderPolicy(
    options: DecoderOptions,
    isolated: boolean,
    hardwareThreads: number,
    count: number,
    streaming: boolean,
    cpuAvailable: number,
): DecoderInfo {
    validateDecoderOptions(options);
    const mode = options.mode ?? 'auto';
    const single = (reason: string | null): DecoderInfo => ({
        backend: 'single',
        threads: 1,
        fallbackReason: reason,
    });
    if (mode === 'single' || options.threads === 1) return single(null);
    if (!isolated) {
        if (mode === 'parallel')
            throw Error('UnsupportedCapability: cross-origin isolation/SharedArrayBuffer');
        return single('Cross-origin isolation unavailable');
    }
    if (!streaming) return single('Small PLY or SPZ v4 uses the bounded single-thread path');
    if (mode === 'auto' && count < 262144) return single('Model below parallel threshold');
    // Reserve headroom for base module, pthread stacks and initialization scratch.
    if (cpuAvailable < 128 * 2 ** 20) {
        if (mode === 'parallel') throw Error('ResourceLimit: parallel runtime CPU budget');
        return single('CPU budget selects single-thread decoder');
    }
    const hardware = Number.isFinite(hardwareThreads) ? Math.max(1, Math.floor(hardwareThreads)) : 1;
    const threads = Math.min(options.threads ?? 4, hardware);
    if (threads < 2) return single('Only one hardware thread available');
    return { backend: 'pthreads', threads, fallbackReason: null };
}
