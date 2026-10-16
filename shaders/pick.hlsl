cbuffer Frame : register(b0)
{
    float4 view_rows[3];
    float4 camera;
    float4 viewport;
    float4 quality;
    float4 clip_planes;
    uint4 meta;
    uint4 offsets0;
    uint4 offsets1;
};
cbuffer Pick : register(b1) { float2 pixel_position; };
struct Ellipse { float2 center; float2 axis0; float2 axis1; float4 color; };
ByteAddressBuffer scene : register(t0);
StructuredBuffer<Ellipse> projected : register(t1);
StructuredBuffer<uint> sorted : register(t2);
RWByteAddressBuffer output : register(u0);

[numthreads(1,1,1)]
void pick_reset(uint i : SV_DispatchThreadID) { output.Store4(0, uint4(0,0,0,0)); }

[numthreads(256,1,1)]
void pick_collect(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint rank = (group.y * 65535 + group.x) * 256 + lane;
    if (rank >= meta.x) return;
    uint index = sorted[rank];
    if (index >= meta.x) return;
    Ellipse e = projected[index];
    float2 delta = pixel_position - e.center;
    float major2 = dot(e.axis0,e.axis0), minor2 = dot(e.axis1,e.axis1);
    if (major2 <= 0 || minor2 <= 0 || e.color.a <= 0) return;
    float2 uv = float2(dot(delta,e.axis0)/major2, dot(delta,e.axis1)/minor2);
    float radius2 = dot(uv,uv) * quality.x * quality.x;
    if (!isfinite(radius2) || any(abs(uv)>1) || radius2 > quality.x*quality.x) return;
    float alpha = min(0.999,e.color.a*exp(-0.5*radius2));
    if (alpha < quality.y || alpha <= 0) return;
    float3 center = asfloat(scene.Load3(offsets0.x+index*12)) - camera.xyz;
    float depth = -dot(view_rows[2].xyz,center);
    // Clipped centers can occlude visible layers, but must not fabricate depth.
    if (!isfinite(depth) || depth < clip_planes.x || depth > clip_planes.y) depth = 0;
    uint slot;
    output.InterlockedAdd(0,1,slot);
    if (slot < 16384) output.Store4((slot+1)*16,uint4(asuint(depth),asuint(alpha),rank,index));
}
