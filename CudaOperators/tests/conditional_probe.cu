#include <cuda_runtime.h>
#include "gs_cuda/interop.h"
#include "gpu.h"
#include <cstdio>
#include <stdexcept>
void checked(cudaError_t error) { if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error)); }
__global__ void start(unsigned *cursor, cudaGraphConditionalHandle h) { *cursor = 0; cudaGraphSetConditional(h, 1); }
__global__ void consume(const unsigned *cursor, unsigned *result) { atomicAdd(result, *cursor + 1); }
__global__ void advance(unsigned *cursor, cudaGraphConditionalHandle h) { ++*cursor; cudaGraphSetConditional(h, *cursor < 2); }
__global__ void advance_plain(unsigned *cursor) { ++*cursor; }
template<class Work> void capture(cudaStream_t stream, cudaGraph_t graph, Work work)
{
    checked(cudaStreamBeginCaptureToGraph(stream, graph, nullptr, nullptr, 0, cudaStreamCaptureModeThreadLocal));
    work(); cudaGraph_t captured{}; checked(cudaStreamEndCapture(stream, &captured));
}
int main(int argc, char **argv)
{
    try
    {
        const int mode = argc > 1 ? std::atoi(argv[1]) : 2;
        std::unique_ptr<gs::render::detail::GpuDevice> gpu;
        std::unique_ptr<gs::cuda_ops::Context> context;
        std::unique_ptr<gs::cuda_ops::SharedBuffer> buffer;
        std::unique_ptr<gs::cuda_ops::SharedTexture> texture;
        cudaStream_t stream{};
        if (mode == 1 || mode == 2 || mode == 4)
        {
            gpu = std::make_unique<gs::render::detail::GpuDevice>();
            context = std::make_unique<gs::cuda_ops::Context>(gpu->device.Get(), gpu->adapter_description.AdapterLuid);
            buffer = std::make_unique<gs::cuda_ops::SharedBuffer>(*context, 104080);
            texture = std::make_unique<gs::cuda_ops::SharedTexture>(*context, 33, 19);
            stream = static_cast<cudaStream_t>(context->native_stream());
        }
        else checked(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
        unsigned *cursor{}, *result{}; void *pixels{};
        checked(cudaMalloc(&pixels, 10032)); checked(cudaMalloc(&cursor, sizeof(unsigned))); checked(cudaMalloc(&result, sizeof(unsigned)));
        checked(cudaMemsetAsync(result, 0, sizeof(unsigned), stream));
        cudaGraph_t graph{}; checked(cudaGraphCreate(&graph, 0)); cudaGraphConditionalHandle handle{};
        checked(cudaGraphConditionalHandleCreate(&handle, graph, 0, cudaGraphCondAssignDefault));
        capture(stream, graph, [&] { checked(cudaMemsetAsync(pixels, 0, 10032, stream)); start<<<1, 1, 0, stream>>>(cursor, handle); checked(cudaGetLastError()); });
        cudaGraphNode_t parent{}, nodes[2]{}; size_t count = 2; checked(cudaGraphGetNodes(graph, nodes, &count));
        for (auto node : nodes) { size_t children = 0; checked(cudaGraphNodeGetDependentNodes(node, nullptr, nullptr, &children)); if (!children) parent = node; }
        cudaGraphNodeParams params{}; params.type = cudaGraphNodeTypeConditional;
        params.conditional.handle = handle; params.conditional.type = cudaGraphCondTypeWhile; params.conditional.size = 1;
        cudaGraphNode_t node{}; checked(cudaGraphAddNode(&node, graph, &parent, nullptr, 1, &params));
        capture(stream, params.conditional.phGraph_out[0], [&] {
            consume<<<1, 64, 0, stream>>>(cursor, result); advance<<<1, 1, 0, stream>>>(cursor, handle); checked(cudaGetLastError());
        });
        cudaGraphExec_t executable{}; checked(cudaGraphInstantiate(&executable, graph, 0));
        if (mode == 2) context->begin(gpu->direct.Get());
        if (mode >= 3)
        {
            checked(cudaMemsetAsync(cursor, 0, sizeof(unsigned), stream));
            for (int i = 0; i < 2; ++i) { consume<<<1, 64, 0, stream>>>(cursor, result); advance_plain<<<1, 1, 0, stream>>>(cursor); }
        }
        else checked(cudaGraphLaunch(executable, stream));
        if (mode == 2) context->end(gpu->direct.Get());
        checked(cudaStreamSynchronize(stream));
        unsigned value = 0; checked(cudaMemcpy(&value, result, sizeof(value), cudaMemcpyDeviceToHost));
        std::printf("expected=192 actual=%u\n", value);
        checked(cudaGraphExecDestroy(executable)); checked(cudaGraphDestroy(graph));
        checked(cudaFree(result)); checked(cudaFree(cursor)); checked(cudaFree(pixels));
        if (mode == 0 || mode == 3) checked(cudaStreamDestroy(stream));
        return value == 192 ? 0 : 1;
    }
    catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
