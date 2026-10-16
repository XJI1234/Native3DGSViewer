Texture2D<float4> source_texture : register(t0);
float4 blit_vertex(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 blit_pixel(float4 position : SV_Position) : SV_Target
{
    return source_texture.Load(int3(int2(position.xy), 0));
}
