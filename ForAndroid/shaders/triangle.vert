#version 450
layout(location = 0) out vec3 color;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-0.7, 0.6), vec2(0.7, 0.6), vec2(0.0, -0.6));
    vec3 colors[3] = vec3[3](vec3(0.9, 0.1, 0.1), vec3(0.1, 0.8, 0.2), vec3(0.1, 0.3, 0.9));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    color = colors[gl_VertexIndex];
}
