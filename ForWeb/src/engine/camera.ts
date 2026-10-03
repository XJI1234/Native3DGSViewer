import type { Bounds, Vec3 } from '../splat-types/index';
export interface Pose {
    readonly position: Vec3;
    readonly target: Vec3;
    readonly up: Vec3;
}
function finite(...values: number[]): void {
    if (!values.every(Number.isFinite)) throw Error('Non-finite camera input');
}
const cross = (a: Vec3, b: Vec3): Vec3 => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
];
const normalize = (a: Vec3): Vec3 => {
    const n = Math.hypot(...a);
    if (n < 1e-20) throw Error('Degenerate camera basis');
    return [a[0] / n, a[1] / n, a[2] / n];
};
export class Camera {
    private target: Vec3 = [0, 0, 0];
    private yaw = 0;
    private pitch = 0;
    private distance = 5;
    private upHint: Vec3 = [0, 1, 0];
    private initial: { target: Vec3; yaw: number; pitch: number; distance: number } | undefined;
    private aspect = 4 / 3;
    private currentRevision = 0;
    get revision(): number {
        return this.currentRevision;
    }
    readonly fov = Math.PI / 3;
    setPose(pose: Pose): void {
        finite(...pose.position, ...pose.target, ...pose.up);
        const delta = pose.position.map((v, i) => v - pose.target[i]!) as unknown as Vec3;
        const back = normalize(delta);
        normalize(cross(pose.up, back));
        this.target = [...pose.target];
        this.distance = Math.hypot(...delta);
        this.yaw = Math.atan2(back[0], back[2]);
        this.pitch = Math.asin(Math.max(-1, Math.min(1, back[1])));
        this.upHint = normalize(pose.up);
        this.currentRevision++;
    }
    resize(width: number, height: number): void {
        finite(width, height);
        if (width < 0 || height < 0) throw Error('Invalid viewport');
        if (width > 0 && height > 0) this.aspect = width / height;
        this.currentRevision++;
    }
    fit(bounds: Bounds, width: number, height: number): void {
        finite(...bounds.min, ...bounds.max, ...bounds.origin, bounds.maxScale);
        if (
            width <= 0 ||
            height <= 0 ||
            bounds.maxScale <= 0 ||
            bounds.max.some((v, i) => v < bounds.min[i]!)
        )
            throw Error('Invalid bounds/viewport');
        this.resize(width, height);
        const radius =
            Math.hypot(...bounds.max.map((v, i) => (v - bounds.min[i]!) / 2)) + 3 * bounds.maxScale;
        const half = Math.min(this.fov / 2, Math.atan(Math.tan(this.fov / 2) * this.aspect));
        this.target = [...bounds.origin];
        this.yaw = 0;
        this.pitch = 0;
        this.upHint = [0, 1, 0];
        this.distance = Math.max(1e-5, (radius / Math.sin(half)) * 1.1);
        this.initial = { target: this.target, yaw: 0, pitch: 0, distance: this.distance };
        this.currentRevision++;
    }
    orbit(x: number, y: number): void {
        finite(x, y);
        this.yaw -= x * 0.005;
        this.pitch = Math.max(-Math.PI / 2 + 0.001, Math.min(Math.PI / 2 - 0.001, this.pitch + y * 0.005));
        this.upHint = [0, 1, 0];
        this.currentRevision++;
    }
    look(x: number, y: number): void {
        const position = this.getPose().position;
        this.orbit(x, y);
        const { back } = this.basis();
        this.target = [
            position[0] - back[0] * this.distance,
            position[1] - back[1] * this.distance,
            position[2] - back[2] * this.distance,
        ];
    }
    dolly(steps: number): void {
        finite(steps);
        this.distance = Math.max(
            1e-6,
            Math.min(1e15, this.distance * Math.exp(Math.max(-20, Math.min(20, steps)) * 0.1)),
        );
        this.currentRevision++;
    }
    pan(x: number, y: number): void {
        finite(x, y);
        const { right, up } = this.basis();
        const factor = this.distance * 0.001;
        this.target = [
            this.target[0] + factor * (-x * right[0] + y * up[0]),
            this.target[1] + factor * (-x * right[1] + y * up[1]),
            this.target[2] + factor * (-x * right[2] + y * up[2]),
        ];
        this.currentRevision++;
    }
    fly(right: number, up: number, forward: number, seconds: number): void {
        finite(right, up, forward, seconds);
        const b = this.basis();
        const scale = Math.min(0.1, Math.max(0, seconds)) * this.distance;
        this.target = this.target.map(
            (v, i) => v + scale * (right * b.right[i]! + up * b.up[i]! - forward * b.back[i]!),
        ) as unknown as Vec3;
        this.currentRevision++;
    }
    reset(): void {
        if (this.initial) {
            Object.assign(this, this.initial);
            this.upHint = [0, 1, 0];
            this.currentRevision++;
        }
    }
    private basis(): { right: Vec3; up: Vec3; back: Vec3 } {
        const back: Vec3 = [
            Math.sin(this.yaw) * Math.cos(this.pitch),
            Math.sin(this.pitch),
            Math.cos(this.yaw) * Math.cos(this.pitch),
        ];
        const right = normalize(cross(this.upHint, back));
        return { right, up: cross(back, right), back };
    }
    getPose(): Pose {
        const { back, up } = this.basis();
        return {
            position: [
                this.target[0] + back[0] * this.distance,
                this.target[1] + back[1] * this.distance,
                this.target[2] + back[2] * this.distance,
            ],
            target: [...this.target],
            up,
        };
    }
    frame(
        bounds: Bounds,
        width: number,
        height: number,
        count: number,
        degree: number,
        stride: number,
        pageCapacity: number,
    ): ArrayBuffer {
        const values = new Float32Array(32),
            integers = new Uint32Array(values.buffer);
        const b = this.basis();
        values.set(b.right, 0);
        values.set(b.up, 4);
        values.set(b.back, 8);
        const pose = this.getPose();
        values.set(
            pose.position.map((v, i) => v - bounds.origin[i]!),
            12,
        );
        const fy = height / (2 * Math.tan(this.fov / 2));
        values.set([fy, fy, width, height], 16);
        values.set([3, 1 / 255, 0.3, 4096], 20);
        values.set([Math.max(1e-5, this.distance * 1e-5), Math.max(this.distance * 100, 1000), 0, 0], 24);
        integers.set([count, degree, stride / 4, pageCapacity], 28);
        if (!values.subarray(0, 28).every(Number.isFinite))
            throw Error('InvalidInput: camera exceeds float32 frame');
        return values.buffer;
    }
}
