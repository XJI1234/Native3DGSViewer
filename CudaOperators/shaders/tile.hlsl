#ifndef TILE_SIZE
#define TILE_SIZE 16
#endif
cbuffer TileFrame : register(b0)
{
    uint width, height, tile_size, chunk_offset;
    float stddev, cutoff;
    uint quantize, source_count;
};
struct Ellipse { float2 center; float2 axis0; float2 axis1; float4 color; };
StructuredBuffer<Ellipse> ellipses : register(t0);
StructuredBuffer<uint> order : register(t1);
StructuredBuffer<uint2> tile_keys : register(t2);
StructuredBuffer<uint2> ranges : register(t3);
RWByteAddressBuffer status : register(u5);
RWStructuredBuffer<float4> pixels : register(u0);
RWTexture2D<float4> output_texture : register(u1);
RWByteAddressBuffer indirect_commands : register(u2);
RWByteAddressBuffer indirect_counts : register(u3);
[numthreads(256,1,1)]
void tile_init(uint i : SV_DispatchThreadID)
{
    if (i < width * height) pixels[i] = 0;
    const uint chunks = source_count/512 + (source_count%512 != 0);
    if (i < chunks)
    {
        const uint tiles = ((width + tile_size - 1) / tile_size) * ((height + tile_size - 1) / tile_size);
        indirect_commands.Store3(i * 12, uint3(tiles, 1, 1));
        indirect_counts.Store(i * 4, status.Load(8) == 0 && i * 512 < status.Load(12) ? 1 : 0);
    }
}
groupshared Ellipse batch[512];
groupshared uint invalid;
[numthreads(TILE_SIZE*TILE_SIZE,1,1)]
void tile_composite(uint tile : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (lane == 0) invalid = status.Load(8);
    GroupMemoryBarrierWithGroupSync();
    if (invalid != 0) return;
    const uint2 range = ranges[tile];
    if (chunk_offset >= range.y - range.x) return;
    const uint base = range.x + chunk_offset;
    const uint count = min(512, range.y - base);
    for (uint i = lane; i < count; i += TILE_SIZE*TILE_SIZE)
        batch[i] = ellipses[order[tile_keys[base+i].x]];
    GroupMemoryBarrierWithGroupSync();
    const uint columns = (width + tile_size - 1) / tile_size;
    const uint x = tile % columns * tile_size + lane % tile_size;
    const uint y = tile / columns * tile_size + lane / tile_size;
    const bool active = x < width && y < height;
    float4 result = active ? pixels[y*width+x] : 0;
    for (uint i = 0; i < count; ++i)
    {
        const Ellipse e = batch[i];
        const float determinant = e.axis0.x * e.axis1.y - e.axis0.y * e.axis1.x;
        if (!isfinite(determinant) || determinant == 0)
        { if (lane == 0) { uint ignored; status.InterlockedOr(8, 4, ignored); } continue; }
        if (!active) continue;
        const float2 delta = float2(x+.5,y+.5) - e.center;
        const float u = (delta.x * e.axis1.y - delta.y * e.axis1.x) / determinant;
        const float v = (delta.y * e.axis0.x - delta.x * e.axis0.y) / determinant;
        const float2 gaussian = float2(u,v) * stddev;
        const float radius2 = dot(gaussian, gaussian);
        if (abs(u) > 1 || abs(v) > 1 || radius2 > stddev*stddev) continue;
        const float alpha = min(.999, e.color.a * exp(-.5*radius2));
        if (alpha < cutoff) continue;
        result = float4(e.color.rgb*alpha,alpha)+(1-alpha)*result;
        if (quantize) result = round(saturate(result)*255)/255;
    }
    if (active) pixels[y*width+x] = result;
}
[numthreads(256,1,1)]
void tile_store(uint i : SV_DispatchThreadID)
{
    if (i < width * height) output_texture[uint2(i%width,i/width)] = status.Load(8) ? 0 : pixels[i];
}
