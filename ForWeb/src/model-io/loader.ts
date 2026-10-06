import type { Limits, Progress, Scene, Source } from '../splat-types/index';
import { diskBacking, removeJob } from './backing';
import type { DecoderOptions } from './decoder-options';
export function decode(
    source: Source,
    limits: Limits,
    assets: string,
    pageBytes: number,
    signal: AbortSignal,
    onProgress: (p: Progress) => void,
    retainedBytes = 0,
    workerUrl?: URL | string,
    decoder?: DecoderOptions,
): Promise<Scene> {
    return new Promise((resolve, reject) => {
        const job = `gs-${crypto.randomUUID()}`;
        const worker = workerUrl
            ? new Worker(workerUrl, { type: 'module' })
            : new Worker(new URL('./decoder.worker.ts', import.meta.url), { type: 'module' });
        let settled = false;
        const finish = (value?: Scene, reason?: unknown) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            signal.removeEventListener('abort', abort);
            worker.terminate();
            if (value?.backing) resolve(value);
            else if (value) void removeJob(job).then(() => resolve(value), reject);
            else
                void removeJob(job).then(
                    () => reject(reason),
                    (cleanup) => reject(Error(`${reason}; storage cleanup: ${cleanup}`)),
                );
        };
        const abort = () => finish(undefined, Error('Cancelled'));
        const timer = setTimeout(() => finish(undefined, Error('Timeout')), limits.timeoutMs);
        worker.onmessage = (
            event: MessageEvent<{ kind: string; scene: Scene; file?: File; value: Progress; reason: string }>,
        ) => {
            if (event.data.kind === 'ready')
                finish(
                    event.data.file
                        ? { ...event.data.scene, backing: diskBacking(job, event.data.file) }
                        : event.data.scene,
                );
            else if (event.data.kind === 'error') finish(undefined, Error(event.data.reason));
            else {
                try {
                    onProgress(event.data.value);
                } catch (reason) {
                    finish(undefined, reason);
                }
            }
        };
        worker.onerror = (event) => finish(undefined, Error(event.message));
        signal.addEventListener('abort', abort, { once: true });
        if (signal.aborted) {
            abort();
            return;
        }
        try {
            worker.postMessage({ source, limits, assets, pageBytes, retainedBytes, job, decoder });
        } catch (reason) {
            finish(undefined, reason);
        }
    });
}
