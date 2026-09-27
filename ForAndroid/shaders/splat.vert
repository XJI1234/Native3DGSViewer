#version 450
struct Ellipse { vec2 center; vec2 axis0; vec2 axis1; vec2 pad; vec4 color; };
layout(set = 0, binding = 0, std430) readonly buffer Projected { Ellipse items[]; } projected;
struct KeyValue { uint key; uint value; };
layout(set = 0, binding = 1, std430) readonly buffer Pairs { KeyValue items[]; } pairs;
layout(set = 0, binding = 2, std140) uniform Frame {
    vec4 row[3]; vec4 camera; vec4 viewport; vec4 quality; vec4 clip_planes;
    uvec4 meta; uvec4 offsets0; uvec4 offsets1;
} frame;
layout(location = 0) out vec2 gaussian;
layout(location = 1) out vec4 color;
void main()
{
    vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(-1, 1),
                                vec2(-1, 1), vec2(1, -1), vec2(1, 1));
    KeyValue pair = pairs.items[gl_InstanceIndex];
    Ellipse e = projected.items[pair.value];
    vec2 uv = corners[gl_VertexIndex];
    vec2 pixel = e.center + uv.x * e.axis0 + uv.y * e.axis1;
    float horizontal = pixel.x * 2.0 / frame.viewport.z - 1.0;
    if (frame.offsets1.y != 0u) horizontal = -horizontal;
    gl_Position = vec4(horizontal,
                       pixel.y * 2.0 / frame.viewport.w - 1.0, 0, 1);
    gaussian = uv * frame.quality.x;
    color = pair.key == 0xffffffffu ? vec4(0) : e.color;
}
