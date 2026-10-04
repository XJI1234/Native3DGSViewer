/** Stop waiting without allowing late work to publish or write GPU resources. */
export function abortable<T>(work: Promise<T>, signal: AbortSignal): Promise<T> {
    return new Promise<T>((resolve, reject) => {
        const abort = () => {
            signal.removeEventListener('abort', abort);
            reject(Error(signal.reason === 'Timeout' ? 'Timeout' : 'Cancelled'));
        };
        signal.addEventListener('abort', abort, { once: true });
        if (signal.aborted) abort();
        void work.then(resolve, reject).finally(() => signal.removeEventListener('abort', abort));
    });
}
