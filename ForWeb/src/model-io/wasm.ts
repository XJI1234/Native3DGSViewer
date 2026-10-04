export interface Decoder {
    HEAPU8: Uint8Array;
    _malloc(bytes: number): number;
    _free(ptr: number): void;
    _gs_probe(ptr: number, size: number, input: number, limit: number): number;
    _gs_begin(rdf: number): number;
    _gs_begin_batch(rdf: number, count: number): number;
    _gs_raw_spz(ptr: number, bytes: number, version: number, fractionalBits: number): number;
    _gs_pack_compact(start: number, count: number): number;
    _gs_rebase(ptr: number, count: number, stride: number, x: number, y: number, z: number): number;
    _gs_inflate_begin(): number;
    _gs_inflate_step(input: number, length: number, output: number, capacity: number): number;
    _gs_inflate_consumed(): number;
    _gs_inflate_produced(): number;
    _gs_inflate_end(): void;
    _gs_chunk(ptr: number, size: number): number;
    _gs_spz(ptr: number, size: number): number;
    _gs_finish(spz: number): number;
    _gs_count(): number;
    _gs_degree(): number;
    _gs_meta(index: number): number;
    _gs_stride(): number;
    _gs_pack(start: number, count: number): number;
    _gs_error(): number;
    _gs_release(): void;
    UTF8ToString(ptr: number): string;
}
export async function createDecoder(baseUrl: string): Promise<Decoder> {
    const moduleUrl = new URL('decoder.mjs', baseUrl).href;
    const factory = (await import(/* @vite-ignore */ moduleUrl)).default as (
        options: unknown,
    ) => Promise<Decoder>;
    return factory({ locateFile: (path: string) => new URL(path, baseUrl).href });
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
