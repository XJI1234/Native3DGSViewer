import type { SceneBacking } from '../splat-types/index';

export async function removeJob(job: string): Promise<void> {
    let timer: ReturnType<typeof setTimeout> | undefined;
    try {
        await Promise.race([
            removeJobInternal(job),
            new Promise<never>((_resolve, reject) => {
                timer = setTimeout(
                    () => reject(Error('StorageCleanup: timeout removing model backing')),
                    5000,
                );
            }),
        ]);
    } finally {
        clearTimeout(timer);
    }
}
async function removeJobInternal(job: string): Promise<void> {
    if (!/^gs-[a-f0-9-]+$/.test(job)) throw Error('InvalidInput: storage job');
    const root = await navigator.storage.getDirectory();
    for (let attempt = 0; ; attempt++) {
        try {
            await root.removeEntry(job, { recursive: true });
            return;
        } catch (reason) {
            if (reason instanceof DOMException && reason.name === 'NotFoundError') return;
            if (attempt === 19) throw reason;
            await new Promise<void>((resolve) => setTimeout(resolve, 25));
        }
    }
}

export function diskBacking(job: string, file: File): SceneBacking {
    let references = 1;
    let cleanup: Promise<void> | undefined;
    return {
        totalBytes: file.size,
        residentBytes: 0,
        read(offset, length) {
            if (!references) return Promise.reject(Error('Stopped: released backing'));
            if (
                !Number.isSafeInteger(offset) ||
                !Number.isSafeInteger(length) ||
                offset < 0 ||
                length < 0 ||
                offset + length > file.size
            )
                return Promise.reject(Error('InvalidInput: backing range'));
            return file.slice(offset, offset + length).arrayBuffer();
        },
        retain() {
            if (!references) throw Error('Stopped: released backing');
            references++;
        },
        release() {
            if (references) references--;
            if (!references) cleanup ??= removeJob(job);
            return cleanup ?? Promise.resolve();
        },
    };
}
