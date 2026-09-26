cbuffer Frame : register(b0)
{
    float4 view_rows[3];
    float4 camera;
    float4 viewport; // fx, fy, width, height
    float4 quality; // stddev, alpha cutoff, blur variance, max radius
    float4 clip_planes;
    uint4 meta; // count, SH degree, SH floats per point, sort mode
    uint4 offsets0; // centers, scales, rotations, opacity
    uint4 offsets1; // RGB, SH, reserved
};
struct Ellipse { float2 center; float2 axis0; float2 axis1; float4 color; float2 reserved; };
ByteAddressBuffer scene : register(t0);
RWStructuredBuffer<Ellipse> ellipses : register(u0);
RWStructuredBuffer<uint> keys : register(u1);
RWStructuredBuffer<uint> indexes : register(u2);
RWStructuredBuffer<uint> arguments : register(u3);

float3 rotate(float4 q,float3 p) { return p+2*cross(q.xyz,cross(q.xyz,p)+q.w*p); }
float3 to_view(float3 p) { return float3(dot(view_rows[0].xyz,p),dot(view_rows[1].xyz,p),dot(view_rows[2].xyz,p)); }
float3 load3(uint offset,uint i) { return asfloat(scene.Load3(offset+i*12)); }
float3 sh_color(float3 rgb,float3 dir,uint i)
{
    float x=dir.x,y=dir.y,z=dir.z;
    float b[15];
    b[0]=-0.4886025119029199*y; b[1]=0.4886025119029199*z; b[2]=-0.4886025119029199*x;
    b[3]=1.0925484305920792*x*y; b[4]=-1.0925484305920792*y*z;
    b[5]=0.31539156525252005*(2*z*z-x*x-y*y); b[6]=-1.0925484305920792*x*z;
    b[7]=0.5462742152960396*(x*x-y*y);
    b[8]=-0.5900435899266435*y*(3*x*x-y*y); b[9]=2.890611442640554*x*y*z;
    b[10]=-0.4570457994644658*y*(4*z*z-x*x-y*y);
    b[11]=0.3731763325901154*z*(2*z*z-3*x*x-3*y*y);
    b[12]=-0.4570457994644658*x*(4*z*z-x*x-y*y);
    b[13]=1.445305721320277*z*(x*x-y*y); b[14]=-0.5900435899266435*x*(x*x-3*y*y);
    uint n=(meta.y+1)*(meta.y+1)-1;
    for(uint j=0;j<n;++j) rgb+=asfloat(scene.Load3(offsets1.y+(i*meta.z+j*3)*4))*b[j];
    return saturate(rgb);
}
[numthreads(1,1,1)]
void reset_args(uint i:SV_DispatchThreadID)
{ arguments[0]=6; arguments[1]=0; arguments[2]=0; arguments[3]=0; arguments[4]=0; }
void reject_projection(uint i)
{
    ellipses[i]=(Ellipse)0;
    uint ignored; InterlockedAdd(arguments[4],1,ignored);
}
[numthreads(256,1,1)]
void project(uint i:SV_DispatchThreadID)
{
    if(i>=meta.x) return;
    keys[i]=0xffffffff; indexes[i]=i;
    Ellipse e=(Ellipse)0;
    float3 delta=load3(offsets0.x,i)-camera.xyz;
    float3 p=to_view(delta),s=load3(offsets0.y,i);
    float4 q=asfloat(scene.Load4(offsets0.z+i*16));
    float alpha=asfloat(scene.Load(offsets0.w+i*4));
    float support=max(s.x,max(s.y,s.z))*quality.x;
    float depth=-p.z;
    if(!all(isfinite(p)) || !all(isfinite(s)) || any(s<=0) || !all(isfinite(q)) ||
       abs(dot(q,q)-1)>0.001 || !isfinite(alpha) || alpha<0 || alpha>1)
    { reject_projection(i); return; }
    if(alpha<=quality.y ||
       depth+support<clip_planes.x || depth-support>clip_planes.y) { ellipses[i]=e; return; }
    float d=max(depth,clip_planes.x);
    float3 jx=float3(viewport.x/d,0,viewport.x*p.x/(d*d));
    float3 jy=float3(0,-viewport.y/d,-viewport.y*p.y/(d*d));
    float3 a0=to_view(rotate(q,float3(s.x,0,0)));
    float3 a1=to_view(rotate(q,float3(0,s.y,0)));
    float3 a2=to_view(rotate(q,float3(0,0,s.z)));
    float3 tx=float3(dot(jx,a0),dot(jx,a1),dot(jx,a2));
    float3 ty=float3(dot(jy,a0),dot(jy,a1),dot(jy,a2));
    float xx=dot(tx,tx)+quality.z,yy=dot(ty,ty)+quality.z,xy=dot(tx,ty);
    float covariance_scale=max(xx,yy);
    if(!isfinite(covariance_scale) || covariance_scale<=0) { reject_projection(i); return; }
    float xxn=xx/covariance_scale,yyn=yy/covariance_scale,xyn=xy/covariance_scale;
    float middle=(xxn+yyn)*0.5,disc=length(float2((xxn-yyn)*0.5,xyn));
    float eigen_major=middle+disc;
    // Gram determinant avoids subtracting nearly equal eigenvalues for thin splats.
    float3 gram_cross=cross(tx/sqrt(covariance_scale),ty/sqrt(covariance_scale));
    float blur=quality.z/covariance_scale;
    float determinant=dot(gram_cross,gram_cross)+blur*(xxn+yyn-blur);
    float major=covariance_scale*eigen_major,minor=covariance_scale*(determinant/eigen_major);
    if(!isfinite(major) || !isfinite(minor) || minor<=0) { reject_projection(i); return; }
    float2 axis=abs(xyn)>1e-10 ? normalize(float2(xyn,eigen_major-xxn)) : (xx>=yy ? float2(1,0) : float2(0,1));
    e.axis0=axis*min(quality.x*sqrt(major),quality.w);
    e.axis1=float2(-axis.y,axis.x)*min(quality.x*sqrt(minor),quality.w);
    e.center=float2(viewport.z*0.5+viewport.x*p.x/d,viewport.w*0.5-viewport.y*p.y/d);
    float2 extent=abs(e.axis0)+abs(e.axis1);
    if(any(e.center+extent<0) || any(e.center-extent>viewport.zw)) { ellipses[i]=e; return; }
    float largest=max(abs(delta.x),max(abs(delta.y),abs(delta.z)));
    float metric;
    if(meta.w==0)
    {
        if(largest>1.0e19) { reject_projection(i); return; }
        metric=dot(delta,delta);
    }
    else metric=max(depth,0);
    if(!isfinite(metric)) { reject_projection(i); return; }
    float3 direction=largest>0 ? normalize(delta/largest) : float3(0,0,1);
    e.color=float4(sh_color(load3(offsets1.x,i),direction,i),alpha);
    ellipses[i]=e;
    keys[i]=~asuint(metric);
    // UINT_MAX is reserved for culled splats; zero distance stays a valid key.
    if(keys[i]==0xffffffff) keys[i]=0xfffffffe;
    uint ignored; InterlockedAdd(arguments[1],1,ignored);
}

StructuredBuffer<Ellipse> projected : register(t1);
StructuredBuffer<uint> sorted : register(t2);
struct Vertex { float4 position:SV_Position; float2 gaussian:TEXCOORD0; float4 color:COLOR0; };
Vertex vertex(uint v:SV_VertexID,uint instance:SV_InstanceID)
{
    const float2 corners[6]={float2(-1,-1),float2(1,-1),float2(-1,1),float2(-1,1),float2(1,-1),float2(1,1)};
    Ellipse e=projected[sorted[instance]];
    float2 uv=corners[v]; float2 pixel=e.center+uv.x*e.axis0+uv.y*e.axis1;
    Vertex result; result.position=float4(pixel.x*2/viewport.z-1,1-pixel.y*2/viewport.w,0,1);
    result.gaussian=uv*quality.x; result.color=e.color; return result;
}
float4 pixel(Vertex input):SV_Target
{
    float radius2=dot(input.gaussian,input.gaussian);
    if(radius2>quality.x*quality.x) discard;
    float alpha=min(0.999,input.color.a*exp(-0.5*radius2));
    if(alpha<quality.y) discard;
    return float4(input.color.rgb*alpha,alpha);
}
