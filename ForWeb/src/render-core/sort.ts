const shader = `
const RADIX_BITS:u32=RADIX_BITS_PLACEHOLDER;
const RADIX_BINS:u32=1u<<RADIX_BITS;
const RADIX_MASK:u32=RADIX_BINS-1u;
struct Params { count:u32, groups:u32, shift:u32, totalBlocks:u32 }
struct Pair { key:u32, index:u32 }
@group(0) @binding(0) var<storage,read> input:array<Pair>;
@group(0) @binding(1) var<storage,read_write> output:array<Pair>;
@group(0) @binding(2) var<storage,read_write> histogram:array<u32>;
@group(0) @binding(3) var<storage,read_write> prefix:array<u32>;
@group(0) @binding(4) var<uniform> params:Params;
@group(0) @binding(5) var<storage,read_write> blocks:array<u32>;
var<workgroup> bins:array<atomic<u32>,RADIX_BINS>;
var<workgroup> localKeys:array<u32,256>;
var<workgroup> localIndexes:array<u32,256>;
var<workgroup> scan:array<u32,256>;
@compute @workgroup_size(256) fn count(@builtin(local_invocation_index) lane:u32,@builtin(workgroup_id) group:vec3u) {
  if(lane<RADIX_BINS){atomicStore(&bins[lane],0u);} workgroupBarrier();
  let i=group.x*256u+lane;
  let key=select(0xffffffffu,input[min(i,params.count-1u)].key,i<params.count);
  atomicAdd(&bins[(key>>params.shift)&RADIX_MASK],1u);workgroupBarrier();
  if(lane<RADIX_BINS){histogram[lane*params.groups+group.x]=atomicLoad(&bins[lane]);}
}
@compute @workgroup_size(256) fn offsets(@builtin(local_invocation_index) lane:u32,@builtin(workgroup_id) group:vec3u) {
  let i=group.x*256u+lane;let bin=group.y;
  let value=select(0u,histogram[bin*params.groups+min(i,params.groups-1u)],i<params.groups);
  scan[lane]=value;workgroupBarrier();
  for(var step=1u;step<256u;step*=2u){var add=0u;if(lane>=step){add=scan[lane-step];}workgroupBarrier();scan[lane]+=add;workgroupBarrier();}
  if(i<params.groups){prefix[bin*params.groups+i]=scan[lane]-value;}
  if(lane==255u){blocks[bin*(params.totalBlocks+1u)+group.x]=scan[255u];}
}
@compute @workgroup_size(256) fn blockOffsets(@builtin(local_invocation_index) lane:u32,@builtin(workgroup_id) group:vec3u) {
  let bin=group.x;let base=bin*(params.totalBlocks+1u);
  let value=select(0u,blocks[base+min(lane,params.totalBlocks-1u)],lane<params.totalBlocks);
  scan[lane]=value;workgroupBarrier();
  for(var step=1u;step<256u;step*=2u){var add=0u;if(lane>=step){add=scan[lane-step];}workgroupBarrier();scan[lane]+=add;workgroupBarrier();}
  if(lane<params.totalBlocks){blocks[base+lane]=scan[lane]-value;}
  if(lane==255u){blocks[base+params.totalBlocks]=scan[255u];}
}
@compute @workgroup_size(256) fn addOffsets(@builtin(global_invocation_id) id:vec3u) {
  let i=id.x;let bin=id.y;if(i>=params.groups){return;}
  var base=0u;for(var lower=0u;lower<bin;lower++){base+=blocks[lower*(params.totalBlocks+1u)+params.totalBlocks];}
  prefix[bin*params.groups+i]+=base+blocks[bin*(params.totalBlocks+1u)+i/256u];
}
@compute @workgroup_size(256) fn scatter(@builtin(local_invocation_index) lane:u32,@builtin(workgroup_id) group:vec3u) {
  let i=group.x*256u+lane;let pair=input[min(i,params.count-1u)];
  localKeys[lane]=select(0xffffffffu,pair.key,i<params.count);localIndexes[lane]=select(0xffffffffu,pair.index,i<params.count);
  workgroupBarrier();
  // Deterministic local rank; one shared read phase, no subgroup width assumptions.
  let key=localKeys[lane];let digit=(key>>params.shift)&RADIX_MASK;
  var rank=0u;
  for(var previous=0u;previous<lane;previous++){
    if(((localKeys[previous]>>params.shift)&RADIX_MASK)==digit){rank++;}
  }
  let destination=prefix[digit*params.groups+group.x]+rank;
  if(destination<params.count){output[destination]=Pair(key,localIndexes[lane]);}
}
`;
export class GpuSort {
    private pipelines: GPUComputePipeline[];
    private params: GPUBuffer[] = [];
    readonly histogram: GPUBuffer;
    readonly prefix: GPUBuffer;
    private blocks: GPUBuffer;
    readonly bytes: number;
    private cached: { a: GPUBuffer; b: GPUBuffer; groups: GPUBindGroup[][] } | undefined;
    private bins: number;
    private passes: number;
    constructor(
        private device: GPUDevice,
        readonly count: number,
        bits = 4,
    ) {
        if (bits !== 4 && bits !== 8) throw Error('Invalid radix width');
        this.bytes = GpuSort.byteSize(count, bits);
        this.bins = 1 << bits;
        this.passes = 32 / bits;
        const groups = Math.ceil(count / 256);
        const code = shader.replace('RADIX_BITS_PLACEHOLDER', `${bits}u`);
        const module = device.createShaderModule({ code });
        this.pipelines = ['count', 'offsets', 'blockOffsets', 'addOffsets', 'scatter'].map((entryPoint) =>
            device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint } }),
        );
        this.histogram = device.createBuffer({
            size: Math.max(this.bins * 4, groups * this.bins * 4),
            usage: GPUBufferUsage.STORAGE,
        });
        this.prefix = device.createBuffer({
            size: Math.max(this.bins * 4, groups * this.bins * 4),
            usage: GPUBufferUsage.STORAGE,
        });
        this.blocks = device.createBuffer({
            size: this.bins * (Math.ceil(groups / 256) + 1) * 4,
            usage: GPUBufferUsage.STORAGE,
        });
        for (let shift = 0; shift < 32; shift += bits) {
            const p = device.createBuffer({
                size: 16,
                usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            });
            device.queue.writeBuffer(p, 0, new Uint32Array([count, groups, shift, Math.ceil(groups / 256)]));
            this.params.push(p);
        }
    }
    static byteSize(count: number, bits = 4): number {
        if (
            (bits !== 4 && bits !== 8) ||
            !Number.isSafeInteger(count) ||
            count < 0 ||
            Math.ceil(count / 256) > 65535
        )
            throw Error('ResourceLimit: radix count/profile');
        const groups = Math.ceil(count / 256),
            bins = 1 << bits;
        return (
            Math.max(bins * 4, groups * bins * 4) * 2 +
            (32 / bits) * 16 +
            bins * 4 * (Math.ceil(groups / 256) + 1)
        );
    }
    encode(encoder: GPUCommandEncoder, a: GPUBuffer, b: GPUBuffer, timestamps?: GPUQuerySet): GPUBuffer {
        if (!this.count) return a;
        if (!this.cached || this.cached.a !== a || this.cached.b !== b) {
            const groups = Array.from({ length: this.passes }, (_, digit) =>
                this.pipelines.map((pipeline, stage) => {
                    const bindings = [
                        [0, 2, 4],
                        [2, 3, 4, 5],
                        [4, 5],
                        [3, 4, 5],
                        [0, 1, 3, 4],
                    ][stage]!;
                    const buffers = [
                        digit % 2 === 0 ? a : b,
                        digit % 2 === 0 ? b : a,
                        this.histogram,
                        this.prefix,
                        this.params[digit]!,
                        this.blocks,
                    ];
                    return this.device.createBindGroup({
                        layout: pipeline.getBindGroupLayout(0),
                        entries: bindings.map((binding) => ({
                            binding,
                            resource: { buffer: buffers[binding]! },
                        })),
                    });
                }),
            );
            this.cached = { a, b, groups };
        }
        for (let digit = 0; digit < this.passes; digit++) {
            for (let stage = 0; stage < 5; stage++) {
                const pipeline = this.pipelines[stage]!;
                const bindGroup = this.cached.groups[digit]![stage]!;
                const timing =
                    timestamps && ((digit === 0 && stage === 0) || (digit === this.passes - 1 && stage === 4))
                        ? {
                              timestampWrites: {
                                  querySet: timestamps,
                                  ...(digit === 0
                                      ? { beginningOfPassWriteIndex: 0 }
                                      : { endOfPassWriteIndex: 1 }),
                              },
                          }
                        : {};
                const pass = encoder.beginComputePass(timing);
                pass.setPipeline(pipeline);
                pass.setBindGroup(0, bindGroup);
                if (stage === 1 || stage === 3)
                    pass.dispatchWorkgroups(Math.ceil(Math.ceil(this.count / 256) / 256), this.bins);
                else if (stage === 2) pass.dispatchWorkgroups(this.bins);
                else pass.dispatchWorkgroups(Math.ceil(this.count / 256));
                pass.end();
            }
        }
        return a;
    }
    dispose(): void {
        this.cached = undefined;
        this.histogram.destroy();
        this.prefix.destroy();
        this.blocks.destroy();
        for (const p of this.params) p.destroy();
    }
}
export async function benchmarkSort(
    device: GPUDevice,
    count: number,
    iterations = 20,
    bits = 8,
): Promise<number[]> {
    if (!device.features.has('timestamp-query')) throw Error('timestamp-query unavailable');
    const a = device.createBuffer({
            size: count * 8,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        }),
        b = device.createBuffer({ size: count * 8, usage: GPUBufferUsage.STORAGE });
    const sort = new GpuSort(device, count, bits),
        query = device.createQuerySet({ type: 'timestamp', count: 2 });
    const resolve = device.createBuffer({
            size: 16,
            usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
        }),
        read = device.createBuffer({ size: 16, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    try {
        const pairs = new Uint32Array(count * 2);
        for (let i = 0; i < count; i++) {
            pairs[2 * i] = (Math.imul(i, 1664525) + 1013904223) >>> 0;
            pairs[2 * i + 1] = i;
        }
        const times: number[] = [];
        for (let iteration = 0; iteration < iterations + 5; iteration++) {
            device.queue.writeBuffer(a, 0, pairs);
            const encoder = device.createCommandEncoder();
            sort.encode(encoder, a, b, query);
            encoder.resolveQuerySet(query, 0, 2, resolve, 0);
            encoder.copyBufferToBuffer(resolve, 0, read, 0, 16);
            device.queue.submit([encoder.finish()]);
            await read.mapAsync(GPUMapMode.READ);
            const t = new BigUint64Array(read.getMappedRange());
            const ms = Number(t[1]! - t[0]!) / 1e6;
            read.unmap();
            if (iteration >= 5) times.push(ms);
        }
        return times;
    } finally {
        sort.dispose();
        a.destroy();
        b.destroy();
        query.destroy();
        resolve.destroy();
        read.destroy();
    }
}
export async function testSort(device: GPUDevice, keys: Uint32Array, bits = 8): Promise<Uint32Array> {
    const n = keys.length,
        bytes = Math.max(8, n * 8);
    const a = device.createBuffer({
        size: bytes,
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC,
    });
    const b = device.createBuffer({ size: bytes, usage: GPUBufferUsage.STORAGE });
    const read = device.createBuffer({
        size: bytes,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    const sort = new GpuSort(device, n, bits);
    try {
        if (n) {
            const pairs = new Uint32Array(n * 2);
            for (let i = 0; i < n; i++) {
                pairs[2 * i] = keys[i]!;
                pairs[2 * i + 1] = i;
            }
            device.queue.writeBuffer(a, 0, pairs);
        }
        const encoder = device.createCommandEncoder();
        sort.encode(encoder, a, b);
        encoder.copyBufferToBuffer(a, 0, read, 0, bytes);
        device.queue.submit([encoder.finish()]);
        await read.mapAsync(GPUMapMode.READ);
        const result = new Uint32Array(read.getMappedRange().slice(0, n * 8));
        read.unmap();
        return result;
    } finally {
        a.destroy();
        b.destroy();
        read.destroy();
        sort.dispose();
    }
}
