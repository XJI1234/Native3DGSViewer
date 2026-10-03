export type Vec3 = readonly [number, number, number];
export interface Bounds {
    readonly origin: Vec3;
    readonly min: Vec3;
    readonly max: Vec3;
    readonly maxScale: number;
}
export interface Scene extends Bounds {
    readonly count: number;
    readonly degree: number;
    readonly stride: number;
    readonly pageCapacity: number;
    readonly pages: readonly ArrayBuffer[];
    readonly source: string;
    readonly decodeMs: number;
}
export type ErrorCode =
    | 'UnsupportedCapability'
    | 'InvalidInput'
    | 'UnsupportedFormat'
    | 'ResourceLimit'
    | 'OutOfMemory'
    | 'DecoderFailure'
    | 'Cancelled'
    | 'Timeout'
    | 'NetworkFailure'
    | 'DeviceLost'
    | 'Stopped';
export interface EngineError {
    readonly code: ErrorCode;
    readonly stage: string;
    readonly diagnostic: string;
}
export type Result<T> =
    | { readonly ok: true; readonly value: T }
    | { readonly ok: false; readonly error: EngineError };
export function error(code: ErrorCode, stage: string, reason: unknown): EngineError {
    return Object.freeze({
        code,
        stage,
        diagnostic: (reason instanceof Error ? reason.message : String(reason)).slice(0, 512),
    });
}
export interface Progress {
    readonly stage: string;
    readonly done: number;
    readonly total: number | null;
}
export type Source =
    | { kind: 'blob'; blob: Blob; name?: string; plyCoordinates?: 'rdf' | 'rub' }
    | { kind: 'url'; url: string; plyCoordinates?: 'rdf' | 'rub' };
export interface Limits {
    readonly inputBytes: number;
    readonly sceneBytes: number;
    readonly gpuBytes: number;
    readonly cpuBytes: number;
    readonly timeoutMs: number;
}
export const defaultLimits: Limits = Object.freeze({
    inputBytes: 256 * 2 ** 20,
    sceneBytes: 768 * 2 ** 20,
    gpuBytes: 512 * 2 ** 20,
    cpuBytes: 1536 * 2 ** 20,
    timeoutMs: 120000,
});

export function validateLimits(value: Limits): void {
    for (const field of Object.values(value))
        if (!Number.isSafeInteger(field) || field <= 0) throw Error('Limits must be positive safe integers');
    if (value.sceneBytes > 768 * 2 ** 20 || value.inputBytes > 768 * 2 ** 20)
        throw Error('wasm32 profile exceeds validated policy');
}
