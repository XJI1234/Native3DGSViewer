export interface SortingOptions {
    readonly mode?: 'strict' | 'adaptive';
    readonly maxSortAgeMs?: number;
    readonly targetFrameMs?: number;
}
export type SortReason =
    | 'strict'
    | 'initial'
    | 'forced'
    | 'frame-change'
    | 'camera-jump'
    | 'age'
    | 'budget'
    | 'reuse';
export function resolveSortingOptions(options: SortingOptions = {}): Readonly<Required<SortingOptions>> {
    const result = {
        mode: options.mode ?? 'adaptive',
        maxSortAgeMs: options.maxSortAgeMs ?? 100,
        targetFrameMs: options.targetFrameMs ?? 1000 / 60,
    };
    if (!['strict', 'adaptive'].includes(result.mode)) throw Error('InvalidInput: sorting mode');
    for (const [name, value, min, max] of [
        ['maxSortAgeMs', result.maxSortAgeMs, 16, 1000],
        ['targetFrameMs', result.targetFrameMs, 4, 100],
    ] as const) {
        if (!Number.isFinite(value) || value < min || value > max)
            throw Error(`InvalidInput: sorting ${name} must be ${min}–${max}`);
    }
    return Object.freeze(result);
}

// Per-scene state; only a submitted sort advances the reference pose.
export class SortingPolicy {
    private reference: Float32Array | undefined;
    private latest: Float32Array | undefined;
    private submittedAt = 0;
    private costMs = 0;
    constructor(
        readonly options: Readonly<Required<SortingOptions>>,
        private readonly scale: number,
    ) {}
    observeCost(ms: number): void {
        if (Number.isFinite(ms) && ms >= 0) this.costMs = this.costMs ? this.costMs * 0.8 + ms * 0.2 : ms;
    }
    private distance(frame: Float32Array): number {
        if (!this.reference) return 0;
        return (
            Math.hypot(...[12, 13, 14].map((i) => frame[i]! - this.reference![i]!)) /
            Math.max(this.scale, 1e-6)
        );
    }
    private interval(): number {
        // Amortize sorting to approximately one eighth of the requested frame budget.
        return Math.min(this.options.maxSortAgeMs, Math.max(this.options.targetFrameMs, this.costMs * 8));
    }
    needsRefresh(now: number): boolean {
        return (
            this.options.mode === 'adaptive' &&
            !!this.latest &&
            !!this.reference &&
            this.distance(this.latest) > 1e-7 &&
            now - this.submittedAt >= this.interval()
        );
    }
    decide(frame: Float32Array, now: number, changed: boolean, forced = false): SortReason {
        this.latest = frame.slice();
        if (this.options.mode === 'strict') return changed ? 'strict' : 'reuse';
        if (!this.reference) return 'initial';
        if (forced) return 'forced';
        // Dynamic near/far planes only affect current visibility, not radial ranks.
        if (frame.slice(16, 24).some((v, i) => v !== this.reference![16 + i])) return 'frame-change';
        const determinant = (f: Float32Array) =>
            f[0]! * (f[5]! * f[10]! - f[6]! * f[9]!) -
            f[1]! * (f[4]! * f[10]! - f[6]! * f[8]!) +
            f[2]! * (f[4]! * f[9]! - f[5]! * f[8]!);
        if (determinant(frame) * determinant(this.reference) < 0) return 'frame-change';
        const directionDot = [8, 9, 10].reduce((sum, i) => sum + frame[i]! * this.reference![i]!, 0);
        const distance = this.distance(frame);
        if (distance > 0.15 || directionDot < Math.SQRT1_2) return 'camera-jump';
        if (distance <= 1e-7) return 'reuse';
        if (now - this.submittedAt >= this.options.maxSortAgeMs) return 'age';
        return now - this.submittedAt >= this.interval() ? 'budget' : 'reuse';
    }
    submitted(frame: Float32Array, now: number): void {
        this.reference = frame.slice();
        this.submittedAt = now;
    }
    diagnostics(frame: Float32Array, now: number): { sortAgeMs: number; sortPositionErrorRatio: number } {
        return {
            sortAgeMs: this.reference ? Math.max(0, now - this.submittedAt) : 0,
            sortPositionErrorRatio: this.distance(frame),
        };
    }
}
