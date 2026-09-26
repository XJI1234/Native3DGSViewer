#define FFX_HLSL
#define kRS_ValueCopy
#ifdef GS_WAVE_AGNOSTIC
#include "FFX_ParallelSort_WaveAgnostic.h"
#else
#include "FFX_ParallelSort.h"
#endif
ConstantBuffer<FFX_ParallelSortCB> params : register(b0);
cbuffer Shift : register(b1) { uint shift_bit; };
RWStructuredBuffer<uint> keys_in : register(u0);
RWStructuredBuffer<uint> values_in : register(u1);
RWStructuredBuffer<uint> sums : register(u2);
RWStructuredBuffer<uint> reduced : register(u3);
RWStructuredBuffer<uint> keys_out : register(u4);
RWStructuredBuffer<uint> values_out : register(u5);
RWStructuredBuffer<uint> scan_in : register(u6);
RWStructuredBuffer<uint> scan_out : register(u7);
RWStructuredBuffer<uint> scan_scratch : register(u8);
#ifdef GS_FORCE_WAVE32
[WaveSize(32)]
#endif
[numthreads(128,1,1)]
void count(uint local : SV_GroupThreadID, uint group : SV_GroupID)
{ FFX_ParallelSort_Count_uint(local,group,params,shift_bit,keys_in,sums); }
#ifdef GS_FORCE_WAVE32
[WaveSize(32)]
#endif
[numthreads(128,1,1)]
void reduce(uint local : SV_GroupThreadID, uint group : SV_GroupID)
{ FFX_ParallelSort_ReduceCount(local,group,params,sums,reduced); }
#ifdef GS_FORCE_WAVE32
[WaveSize(32)]
#endif
[numthreads(128,1,1)]
void scan(uint local : SV_GroupThreadID, uint group : SV_GroupID)
{ FFX_ParallelSort_ScanPrefix(params.NumScanValues,local,group,0,512*group,false,params,scan_in,scan_out,scan_scratch); }
#ifdef GS_FORCE_WAVE32
[WaveSize(32)]
#endif
[numthreads(128,1,1)]
void scan_add(uint local : SV_GroupThreadID, uint group : SV_GroupID)
{
    uint bin = group/params.NumReduceThreadgroupPerBin;
    uint base = (group%params.NumReduceThreadgroupPerBin)*512;
    FFX_ParallelSort_ScanPrefix(params.NumThreadGroups,local,group,bin*params.NumThreadGroups,base,true,params,scan_in,scan_out,scan_scratch);
}
#ifdef GS_FORCE_WAVE32
[WaveSize(32)]
#endif
[numthreads(128,1,1)]
void scatter(uint local : SV_GroupThreadID, uint group : SV_GroupID)
{ FFX_ParallelSort_Scatter_uint(local,group,params,shift_bit,keys_in,keys_out,sums,values_in,values_out); }
