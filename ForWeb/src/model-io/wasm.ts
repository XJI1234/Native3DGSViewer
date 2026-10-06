export interface Decoder {
    PThread?: { terminateAllThreads(): void };
    HEAPU8: Uint8Array;
    _malloc(bytes: number): number;
    _free(ptr: number): void;
    _gs_probe(ptr: number, size: number, input: number, limit: number): number;
    _gs_begin(rdf: number): number;
    _gs_begin_batch(rdf: number, count: number): number;
    _gs_raw_spz(ptr: number, bytes: number, version: number, fractionalBits: number): number;
    _gs_pack_compact(start: number, count: number): number;
    _gs_pack_tiled(start: number, count: number): number;
    _gs_rebase_tiled(ptr: number, count: number, stride: number, x: number, y: number, z: number): number;
    _gs_rebase(ptr: number, count: number, stride: number, x: number, y: number, z: number): number;
    _gs_inflate_begin(): number;
    _gs_inflate_step(input: number, length: number, output: number, capacity: number): number;
    _gs_inflate_consumed(): number;
    _gs_inflate_produced(): number;
    _gs_inflate_end(): void;
    _gs_chunk(ptr: number, size: number): number;
    _gs_spz(ptr: number, size: number): number;
    _gs_finish(spz: number): number;
    _gs_finish_batch(): number;
    _gs_count(): number;
    _gs_degree(): number;
    _gs_meta(index: number): number;
    _gs_stride(): number;
    _gs_pack(start: number, count: number): number;
    _gs_error(): number;
    _gs_release(): void;
    _gs_set_threads(threads: number): number;
    _gs_decode_batch(
        ptr: number,
        bytes: number,
        rdf: number,
        count: number,
        version: number,
        fractional: number,
    ): number;
    _gs_rebase_parallel(ptr: number, count: number, stride: number, x: number, y: number, z: number): number;
    _gs_timing(stage: number): number;
    UTF8ToString(ptr: number): string;
}
export async function createDecoder(baseUrl: string, threads = 1): Promise<Decoder> {
    const directory = threads > 1 ? new URL('threaded/', baseUrl).href : baseUrl;
    const moduleUrl = new URL('decoder.mjs', directory).href;
    const options: {
        locateFile: (path: string) => string;
        pthreadPoolSize: number;
        PThread?: Decoder['PThread'];
    } = {
        locateFile: (path) => new URL(path, directory).href,
        pthreadPoolSize: threads - 1,
    };
    let failed = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let initialized: Decoder | undefined;
    try {
        // The optional module download and its factory share one initialization deadline.
        const pending = (async () => {
            const factory = (await import(/* @vite-ignore */ moduleUrl)).default as (
                options: unknown,
            ) => Promise<Decoder>;
            // A late import cannot start another pool after auto has selected the base runtime.
            if (failed) throw Error('Timeout: pthread initialization');
            return factory(options);
        })();
        void pending.then(
            (module) => {
                if (failed) disposeDecoder(module);
            },
            () => {},
        );
        initialized =
            threads === 1
                ? await pending
                : await Promise.race([
                      pending,
                      new Promise<never>((_, reject) => {
                          timer = setTimeout(() => reject(Error('Timeout: pthread initialization')), 10000);
                      }),
                  ]);
        check(initialized, initialized._gs_set_threads(threads));
        return initialized;
    } catch (reason) {
        failed = true;
        if (initialized) disposeDecoder(initialized);
        else options.PThread?.terminateAllThreads();
        throw reason;
    } finally {
        if (timer !== undefined) clearTimeout(timer);
    }
}
export function disposeDecoder(module: Decoder): void {
    module._gs_release();
    module.PThread?.terminateAllThreads();
}
export function check(module: Decoder, value: number): void {
    if (!value) throw Error(module.UTF8ToString(module._gs_error()));
}
export function withBytes<T>(module: Decoder, bytes: Uint8Array, fn: (pointer: number) => T): T {
    const ptr = module._malloc(bytes.length);
    if (!ptr) throw Error('OutOfMemory');
    try {
        module.HEAPU8.set(bytes, ptr);
        return fn(ptr);
    } finally {
        module._free(ptr);
    }
}
