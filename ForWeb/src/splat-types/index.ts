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
    readonly packing?: 'compact' | 'tiled';
    readonly backing?: SceneBacking;
    readonly source: string;
    readonly decodeMs: number;
}
export interface SceneBacking {
    readonly totalBytes: number;
    readonly residentBytes: number;
    read(offset: number, length: number): Promise<ArrayBuffer>;
    retain(): void;
    release(): Promise<void>;
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
    inputBytes: 8 * 2 ** 30,
    sceneBytes: 8 * 2 ** 30,
    gpuBytes: 8 * 2 ** 30,
    cpuBytes: 512 * 2 ** 20,
    timeoutMs: 600000,
});

export function validateLimits(value: Limits): void {
    if (value.timeoutMs > 2147483647) throw Error('Timeout exceeds browser timer range');
    for (const field of Object.values(value))
        if (!Number.isSafeInteger(field) || field <= 0) throw Error('Limits must be positive safe integers');
}
