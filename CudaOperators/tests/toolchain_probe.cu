#include <cuda_runtime.h>
#include <cub/cub.cuh>
#include <array>
#include <iostream>
#include <stdexcept>

static void checked(cudaError_t result)
{
    if (result != cudaSuccess)
        throw std::runtime_error(cudaGetErrorString(result));
}

__global__ void transform(int *values)
{
    const int i = threadIdx.x;
    values[i] = i * i + 7;
}

int main()
{
    try
    {
        cudaDeviceProp device{};
        checked(cudaGetDeviceProperties(&device, 0));
        int *values = nullptr;
        checked(cudaMalloc(&values, 32 * sizeof(int)));
        transform<<<1, 32>>>(values);
        checked(cudaGetLastError());
        std::array<int, 32> result{};
        checked(cudaMemcpy(result.data(), values, sizeof(result), cudaMemcpyDeviceToHost));
        checked(cudaFree(values));
        for (int i = 0; i < 32; ++i)
            if (result[i] != i * i + 7)
                throw std::runtime_error("SM86 arithmetic mismatch");
        int *input = nullptr, *output = nullptr;
        void *scratch = nullptr;
        size_t scratch_bytes = 0;
        checked(cudaMalloc(&input, sizeof(result)));
        checked(cudaMalloc(&output, sizeof(result)));
        for (int i = 0; i < 32; ++i) result[i] = 31 - i;
        checked(cudaMemcpy(input, result.data(), sizeof(result), cudaMemcpyHostToDevice));
        checked(cub::DeviceRadixSort::SortKeys(scratch, scratch_bytes, input, output, 32));
        checked(cudaMalloc(&scratch, scratch_bytes));
        checked(cub::DeviceRadixSort::SortKeys(scratch, scratch_bytes, input, output, 32));
        checked(cudaMemcpy(result.data(), output, sizeof(result), cudaMemcpyDeviceToHost));
        for (int i = 0; i < 32; ++i)
            if (result[i] != i) throw std::runtime_error("CUB sort mismatch");
        checked(cudaFree(scratch));
        checked(cudaFree(output));
        checked(cudaFree(input));
        int runtime = 0, driver = 0;
        checked(cudaRuntimeGetVersion(&runtime));
        checked(cudaDriverGetVersion(&driver));
        std::cout << "device=" << device.name << " sm=" << device.major << device.minor
                  << " runtime=" << runtime << " driver=" << driver
                  << " header_runtime=" << CUDART_VERSION << " msvc=" << _MSC_VER
                  << " cub=" << CUB_VERSION << " arithmetic=pass cub_sort=pass\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
