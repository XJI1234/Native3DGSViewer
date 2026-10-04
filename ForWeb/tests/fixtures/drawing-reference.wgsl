
struct Frame { r0:vec4f,r1:vec4f,r2:vec4f,camera:vec4f,viewport:vec4f,quality:vec4f,clip:vec4f,config:vec4u }
struct Ellipse { center:vec2f,axis0:vec2f,axis1:vec2f,colorRG:vec2f,colorBA:vec2f }
struct Pair { key:u32,index:u32 }
@group(0) @binding(0) var<storage,read> ellipses:array<Ellipse>;
@group(0) @binding(1) var<storage,read> pairs:array<Pair>;
@group(0) @binding(2) var<uniform> frame:Frame;
struct Vertex { @builtin(position) position:vec4f,@location(0) gaussian:vec2f,@location(1) color:vec4f }
@vertex fn vertex(@builtin(vertex_index) v:u32,@builtin(instance_index) instance:u32)->Vertex {
  let corners=array<vec2f,4>(vec2f(-1,-1),vec2f(1,-1),vec2f(-1,1),vec2f(1,1));
  let e=ellipses[pairs[instance].index];let uv=corners[v];let pixel=e.center+uv.x*e.axis0+uv.y*e.axis1;
  return Vertex(vec4f(pixel.x*2/frame.viewport.z-1,1-pixel.y*2/frame.viewport.w,0,1),uv*frame.quality.x,vec4f(e.colorRG,e.colorBA));
}
@fragment fn fragment(input:Vertex)->@location(0) vec4f {
  let radius2=dot(input.gaussian,input.gaussian);if(radius2>frame.quality.x*frame.quality.x){discard;}
  let alpha=min(0.999,input.color.a*exp(-0.5*radius2));if(alpha<frame.quality.y){discard;}
  return vec4f(input.color.rgb*alpha,alpha);
}
