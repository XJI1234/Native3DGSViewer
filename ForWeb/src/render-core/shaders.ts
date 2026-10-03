export const projection = `
struct Frame { r0:vec4f,r1:vec4f,r2:vec4f,camera:vec4f,viewport:vec4f,quality:vec4f,clip:vec4f,config:vec4u }
struct Ellipse { center:vec2f,axis0:vec2f,axis1:vec2f,pad:vec2f,color:vec4f }
struct Pair { key:u32,index:u32 }
@group(0) @binding(0) var<storage,read> page0:array<u32>;
@group(0) @binding(1) var<storage,read> page1:array<u32>;
@group(0) @binding(2) var<storage,read> page2:array<u32>;
@group(0) @binding(3) var<storage,read> page3:array<u32>;
@group(0) @binding(4) var<storage,read_write> ellipses:array<Ellipse>;
@group(0) @binding(5) var<storage,read_write> pairs:array<Pair>;
@group(0) @binding(6) var<storage,read_write> args:array<atomic<u32>>;
@group(0) @binding(7) var<uniform> frame:Frame;
fn value(i:u32,offset:u32)->f32 {
  let page=i/frame.config.w;let address=(i%frame.config.w)*frame.config.z+offset;
  switch(page){case 0u:{return bitcast<f32>(page0[address]);}case 1u:{return bitcast<f32>(page1[address]);}case 2u:{return bitcast<f32>(page2[address]);}default:{return bitcast<f32>(page3[address]);}}
}
fn v3(i:u32,o:u32)->vec3f {return vec3f(value(i,o),value(i,o+1u),value(i,o+2u));}
fn rotate(q:vec4f,p:vec3f)->vec3f {return p+2.0*cross(q.xyz,cross(q.xyz,p)+q.w*p);}
fn view(p:vec3f)->vec3f {return vec3f(dot(frame.r0.xyz,p),dot(frame.r1.xyz,p),dot(frame.r2.xyz,p));}
fn sh(rgb:vec3f,dir:vec3f,i:u32)->vec3f {
  let x=dir.x;let y=dir.y;let z=dir.z;
  let basis=array<f32,15>(-0.4886025119*y,0.4886025119*z,-0.4886025119*x,
    1.0925484306*x*y,-1.0925484306*y*z,0.3153915653*(2*z*z-x*x-y*y),-1.0925484306*x*z,0.5462742153*(x*x-y*y),
    -0.5900435899*y*(3*x*x-y*y),2.8906114426*x*y*z,-0.4570457995*y*(4*z*z-x*x-y*y),
    0.3731763326*z*(2*z*z-3*x*x-3*y*y),-0.4570457995*x*(4*z*z-x*x-y*y),1.4453057213*z*(x*x-y*y),-0.5900435899*x*(x*x-3*y*y));
  var color=rgb;let n=(frame.config.y+1u)*(frame.config.y+1u)-1u;
  for(var j=0u;j<n;j++){color+=v3(i,16u+3u*j)*basis[j];}return clamp(color,vec3f(0),vec3f(1));
}
@compute @workgroup_size(256) fn project(@builtin(global_invocation_id) id:vec3u){
  let i=id.x;if(i>=frame.config.x){return;}
  pairs[i]=Pair(0xffffffffu,i);ellipses[i]=Ellipse(vec2f(0),vec2f(0),vec2f(0),vec2f(0),vec4f(0));
  let delta=v3(i,0)-frame.camera.xyz;let p=view(delta);let s=v3(i,4);let q=vec4f(v3(i,8),value(i,11));let alpha=value(i,15);
  let support=max(s.x,max(s.y,s.z))*frame.quality.x;let depth=-p.z;
  if(alpha<=frame.quality.y || depth+support<frame.clip.x || depth-support>frame.clip.y){return;}
  let d=max(depth,frame.clip.x);let fx=frame.viewport.x;let fy=frame.viewport.y;
  let jx=vec3f(fx/d,0,fx*p.x/(d*d));let jy=vec3f(0,-fy/d,-fy*p.y/(d*d));
  let a0=view(rotate(q,vec3f(s.x,0,0)));let a1=view(rotate(q,vec3f(0,s.y,0)));let a2=view(rotate(q,vec3f(0,0,s.z)));
  let tx=vec3f(dot(jx,a0),dot(jx,a1),dot(jx,a2));let ty=vec3f(dot(jy,a0),dot(jy,a1),dot(jy,a2));
  let xx=dot(tx,tx)+frame.quality.z;let yy=dot(ty,ty)+frame.quality.z;let xy=dot(tx,ty);let scale=max(xx,yy);
  if(!(scale>0.0) || scale>3.4e38){return;}
  let xxn=xx/scale;let yyn=yy/scale;let xyn=xy/scale;let majorN=(xxn+yyn)*0.5+length(vec2f((xxn-yyn)*0.5,xyn));
  let gram=cross(tx/sqrt(scale),ty/sqrt(scale));let blur=frame.quality.z/scale;
  let determinant=dot(gram,gram)+blur*(xxn+yyn-blur);let major=scale*majorN;let minor=scale*determinant/majorN;
  if(!(minor>0.0) || major>3.4e38){return;}
  var axis=select(vec2f(0,1),vec2f(1,0),xx>=yy);if(abs(xyn)>1e-10){axis=normalize(vec2f(xyn,majorN-xxn));}
  let axis0=axis*min(frame.quality.x*sqrt(major),frame.quality.w);let axis1=vec2f(-axis.y,axis.x)*min(frame.quality.x*sqrt(minor),frame.quality.w);
  let center=vec2f(frame.viewport.z*0.5+fx*p.x/d,frame.viewport.w*0.5-fy*p.y/d);let extent=abs(axis0)+abs(axis1);
  if(any(center+extent<vec2f(0)) || any(center-extent>frame.viewport.zw)){return;}
  let largest=max(abs(delta.x),max(abs(delta.y),abs(delta.z)));if(largest>1.0e19){return;}
  let metric=dot(delta,delta);var direction=vec3f(0,0,1);if(largest>0){direction=normalize(delta/largest);}
  ellipses[i]=Ellipse(center,axis0,axis1,vec2f(0),vec4f(sh(v3(i,12),direction,i),alpha));
  pairs[i].key=min(~bitcast<u32>(metric),0xfffffffeu);atomicAdd(&args[1],1u);
}
`;
export const drawing = `
struct Frame { r0:vec4f,r1:vec4f,r2:vec4f,camera:vec4f,viewport:vec4f,quality:vec4f,clip:vec4f,config:vec4u }
struct Ellipse { center:vec2f,axis0:vec2f,axis1:vec2f,pad:vec2f,color:vec4f }
struct Pair { key:u32,index:u32 }
@group(0) @binding(0) var<storage,read> ellipses:array<Ellipse>;
@group(0) @binding(1) var<storage,read> pairs:array<Pair>;
@group(0) @binding(2) var<uniform> frame:Frame;
struct Vertex { @builtin(position) position:vec4f,@location(0) gaussian:vec2f,@location(1) color:vec4f }
@vertex fn vertex(@builtin(vertex_index) v:u32,@builtin(instance_index) instance:u32)->Vertex {
  let corners=array<vec2f,4>(vec2f(-1,-1),vec2f(1,-1),vec2f(-1,1),vec2f(1,1));
  let e=ellipses[pairs[instance].index];let uv=corners[v];let pixel=e.center+uv.x*e.axis0+uv.y*e.axis1;
  return Vertex(vec4f(pixel.x*2/frame.viewport.z-1,1-pixel.y*2/frame.viewport.w,0,1),uv*frame.quality.x,e.color);
}
@fragment fn fragment(input:Vertex)->@location(0) vec4f {
  let radius2=dot(input.gaussian,input.gaussian);if(radius2>frame.quality.x*frame.quality.x){discard;}
  let alpha=min(0.999,input.color.a*exp(-0.5*radius2));if(alpha<frame.quality.y){discard;}
  return vec4f(input.color.rgb*alpha,alpha);
}
`;
