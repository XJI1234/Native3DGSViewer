#version 450
layout(location = 0) in vec2 gaussian;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 out_color;
void main()
{
    float radius2 = dot(gaussian, gaussian);
    if (radius2 > 9.0) discard;
    float alpha = min(0.999, color.a * exp(-0.5 * radius2));
    if (alpha < 0.0039) discard;
    out_color = vec4(color.rgb * alpha, alpha);
}
