#include "gs_cuda/tile.h"
#include "gpu.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>

using namespace gs::cuda_ops;
using namespace gs::render::detail;
namespace
{
struct TileFixture
{
    GpuDevice gpu{true};
    Context context{gpu.device.Get(), gpu.adapter_description.AdapterLuid};
    void upload(SharedBuffer &buffer, const void *data, uint64_t bytes)
    {
        auto staging = gpu.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped = nullptr; D3D12_RANGE empty{};
        check(staging->Map(0, &empty, &mapped), "Tile upload map");
        std::memcpy(mapped, data, size_t(bytes)); staging->Unmap(0, nullptr);
        ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Tile allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Tile list");
        transition(list.Get(), buffer.resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(buffer.resource(), 0, staging.Get(), 0, bytes);
        transition(list.Get(), buffer.resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        check(list->Close(), "Tile close"); gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value);
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    }
    template<class T> std::vector<T> read(SharedBuffer &buffer)
    {
        auto bytes = gpu.readback(buffer.resource(), buffer.bytes(), D3D12_RESOURCE_STATE_COMMON);
        std::vector<T> result(bytes.size() / sizeof(T)); std::memcpy(result.data(), bytes.data(), bytes.size()); return result;
    }
    void run(TileWorkspace &workspace, SharedBuffer &ellipses, SharedBuffer &indexes, SharedBuffer &args)
    {
        context.begin(gpu.direct.Get()); workspace.submit(context, ellipses, indexes, args); context.end(gpu.direct.Get());
        check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Tile finish");
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    }
    std::vector<uint8_t> read_texture(SharedTexture &texture)
    {
        auto desc = texture.resource()->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; uint64_t total = 0;
        gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
        auto readback = gpu.buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Tile image allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Tile image list");
        transition(list.Get(), texture.resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
        source.pResource = texture.resource(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        transition(list.Get(), texture.resource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        check(list->Close(), "Tile image close"); gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value);
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
        void *mapped = nullptr; D3D12_RANGE range{0, size_t(total)}, empty{};
        check(readback->Map(0, &range, &mapped), "Tile image map");
        std::vector<uint8_t> result(uint64_t(texture.width()) * texture.height() * 4);
        for (uint32_t y = 0; y < texture.height(); ++y) std::memcpy(result.data() + uint64_t(y) * texture.width() * 4,
            static_cast<uint8_t *>(mapped) + uint64_t(y) * footprint.Footprint.RowPitch, texture.width() * 4);
        readback->Unmap(0, &empty); return result;
    }
};
// Independent oracle: enumerate each screen tile and test the conservative quad
// bounds in double precision, then append ranks in original global order.
std::vector<uint64_t> oracle(const std::vector<ProjectedEllipse> &points, const std::vector<uint32_t> &order,
    uint32_t visible, const TileConfig &c)
{
    std::vector<uint64_t> result;
    const auto columns = (c.width + c.tile_size - 1) / c.tile_size, rows = (c.height + c.tile_size - 1) / c.tile_size;
    for (uint32_t y = 0; y < rows; ++y) for (uint32_t x = 0; x < columns; ++x)
        for (uint32_t rank = 0; rank < visible; ++rank)
        {
            const auto &e = points[order[rank]];
            const double ex = std::abs(double(e.axis0[0])) + std::abs(double(e.axis1[0]));
            const double ey = std::abs(double(e.axis0[1])) + std::abs(double(e.axis1[1]));
            if (e.color[3] <= 0 || e.center[0] + ex < 0 || e.center[1] + ey < 0 ||
                e.center[0] - ex >= c.width || e.center[1] - ey >= c.height) continue;
            if (double(e.center[0]) + ex >= x * c.tile_size && double(e.center[0]) - ex < (x + 1) * c.tile_size &&
                double(e.center[1]) + ey >= y * c.tile_size && double(e.center[1]) - ey < (y + 1) * c.tile_size)
                result.push_back((uint64_t(y * columns + x) << 32) | rank);
        }
    return result;
}
}
TEST(CudaTile, ListMatchesIndependentOracleAndRetainsStableGlobalRanks)
{
    TileFixture f;
    std::vector<ProjectedEllipse> points(513);
    std::vector<uint32_t> order(points.size()); std::iota(order.rbegin(), order.rend(), 0u);
    std::mt19937 random(42);
    for (auto &e : points)
    {
        e.center[0] = float(int(random() % 100) - 18); e.center[1] = float(int(random() % 90) - 18);
        e.axis0[0] = 2 + random() % 9; e.axis0[1] = 1;
        e.axis1[0] = -1; e.axis1[1] = e.axis0[0]; e.color[3] = .8f;
    }
    points[0] = {{0, 0}, {200, 0}, {0, 200}, {1, 0, 0, 1}};
    points[1].color[3] = 0;
    SharedBuffer ellipses(f.context, points.size() * sizeof(ProjectedEllipse)), indexes(f.context, order.size() * 4), args(f.context, 20);
    f.upload(ellipses, points.data(), ellipses.bytes()); f.upload(indexes, order.data(), indexes.bytes());
    for (auto size : {8u, 16u})
    {
        TileConfig config{513, 65, 49, size, 40000, 64ull << 20};
        TileWorkspace workspace(f.context, config);
        EXPECT_LE(workspace.allocated_bytes(), config.budget_bytes);
        for (auto visible : {513u, 0u, 257u, 513u})
        {
            DrawCounters counters{4, visible, 0, 0, 0}; f.upload(args, &counters, sizeof(counters));
            f.run(workspace, ellipses, indexes, args);
            const auto expected = oracle(points, order, visible, config);
            const auto status = f.read<TileStatus>(workspace.status())[0];
            ASSERT_EQ(status.flags, 0u); ASSERT_EQ(status.references, expected.size());
            const auto actual = f.read<uint64_t>(workspace.keys());
            ASSERT_TRUE(std::equal(expected.begin(), expected.end(), actual.begin()));
            ASSERT_TRUE(std::all_of(actual.begin() + expected.size(), actual.end(), [](auto key) { return key == UINT64_MAX; }));
            const auto ranges = f.read<TileRange>(workspace.ranges());
            uint32_t maximum = 0;
            for (uint32_t tile = 0; tile < ranges.size(); ++tile)
            {
                std::vector<uint32_t> positions;
                for (uint32_t i = 0; i < expected.size(); ++i) if (expected[i] >> 32 == tile) positions.push_back(i);
                if (positions.empty()) EXPECT_EQ(ranges[tile].end - ranges[tile].begin, 0u);
                else { EXPECT_EQ(ranges[tile].begin, positions.front()); EXPECT_EQ(ranges[tile].end, positions.back() + 1); }
                maximum = (std::max)(maximum, uint32_t(positions.size()));
            }
            EXPECT_EQ(status.max_tile_length, maximum);
        }
    }
    EXPECT_TRUE(f.context.wait_idle()); EXPECT_TRUE(f.gpu.debug_errors().empty());
}
TEST(CudaTile, ListRejectsCapacityOverflowAndInvalidIndicesWithoutTruncation)
{
    TileFixture f;
    ProjectedEllipse point{{16, 16}, {32, 0}, {0, 32}, {1, 1, 1, 1}};
    SharedBuffer ellipses(f.context, 40), indexes(f.context, 4), args(f.context, 20);
    f.upload(ellipses, &point, 40); uint32_t index = 0; f.upload(indexes, &index, 4);
    DrawCounters counters{4, 1, 0, 0, 0}; f.upload(args, &counters, 20);
    TileWorkspace workspace(f.context, {1, 32, 32, 8, 1, 8ull << 20});
    f.run(workspace, ellipses, indexes, args);
    auto status = f.read<TileStatus>(workspace.status())[0]; EXPECT_EQ(status.references, 16u); EXPECT_NE(status.flags & 2, 0u);
    auto keys = f.read<uint64_t>(workspace.keys()); EXPECT_EQ(keys[0], UINT64_MAX);
    for (const auto &range : f.read<TileRange>(workspace.ranges())) EXPECT_EQ(range.end, range.begin);
    index = 1; f.upload(indexes, &index, 4); f.run(workspace, ellipses, indexes, args);
    EXPECT_NE(f.read<TileStatus>(workspace.status())[0].flags & 1, 0u);
    index = 0; f.upload(indexes, &index, 4); point.axis0[0] = std::numeric_limits<float>::infinity(); f.upload(ellipses, &point, 40);
    f.run(workspace, ellipses, indexes, args); EXPECT_NE(f.read<TileStatus>(workspace.status())[0].flags & 1, 0u);
    counters.instance_count = 2; f.upload(args, &counters, 20);
    f.run(workspace, ellipses, indexes, args); EXPECT_NE(f.read<TileStatus>(workspace.status())[0].flags & 1, 0u);
}
TEST(CudaTile, ListValidatesDimensionsCheckedArithmeticAndBudgetBeforeAllocation)
{
    TileFixture f;
    TileConfig config{UINT32_MAX, 16384, 16384, 8, INT32_MAX, 1};
    const auto bytes = TileWorkspace::required_bytes(f.context, config);
    EXPECT_GT(bytes, uint64_t(UINT32_MAX));
    EXPECT_THROW(TileWorkspace(f.context, config), std::out_of_range);
    for (auto invalid : {TileConfig{1, 0, 32, 8, 10, 1ull << 20}, TileConfig{1, 32, 32, 4, 10, 1ull << 20},
        TileConfig{1, UINT32_MAX, UINT32_MAX, 8, 10, 1ull << 20}, TileConfig{1, 32, 32, 8, UINT32_MAX, 1ull << 20}})
        EXPECT_THROW(TileWorkspace::required_bytes(f.context, invalid), std::invalid_argument);
}
TEST(CudaTile, ListCountsMoreThanUint32ReferencesAndRejectsWholeFrame)
{
    TileFixture f;
    constexpr uint32_t count = 1025;
    std::vector<ProjectedEllipse> points(count, {{8192, 8192}, {16384, 0}, {0, 16384}, {1, 1, 1, 1}});
    std::vector<uint32_t> order(count); std::iota(order.begin(), order.end(), 0u);
    SharedBuffer ellipses(f.context, points.size() * 40), indexes(f.context, order.size() * 4), args(f.context, 20);
    f.upload(ellipses, points.data(), ellipses.bytes()); f.upload(indexes, order.data(), indexes.bytes());
    DrawCounters counters{4, count, 0, 0, 0}; f.upload(args, &counters, 20);
    TileWorkspace workspace(f.context, {count, 16384, 16384, 8, 1, 64ull << 20});
    f.run(workspace, ellipses, indexes, args);
    const auto status = f.read<TileStatus>(workspace.status())[0];
    EXPECT_EQ(status.references, uint64_t(count) * 2048 * 2048);
    EXPECT_GT(status.references, uint64_t(UINT32_MAX)); EXPECT_EQ(status.flags, 2u);
    EXPECT_EQ(f.read<uint64_t>(workspace.keys())[0], UINT64_MAX);
    EXPECT_EQ(status.max_tile_length, 0u);
}
TEST(CudaTile, ListHandlesEmptySourceAndValidatesInputCapacityAndAliases)
{
    TileFixture f;
    SharedBuffer ellipses(f.context, 40), indexes(f.context, 4), args(f.context, 20);
    DrawCounters counters{4, 0, 0, 0, 0}; f.upload(args, &counters, 20);
    TileWorkspace empty(f.context, {0, 17, 9, 8, 1, 8ull << 20});
    f.run(empty, ellipses, indexes, args);
    const auto status = f.read<TileStatus>(empty.status())[0];
    EXPECT_EQ(status.references, 0u); EXPECT_EQ(status.flags, 0u); EXPECT_EQ(status.max_tile_length, 0u);
    TileWorkspace too_large(f.context, {2, 17, 9, 8, 1, 8ull << 20});
    EXPECT_THROW(too_large.submit(f.context, ellipses, indexes, args), std::out_of_range);
    EXPECT_THROW(empty.submit(f.context, ellipses, ellipses, args), std::invalid_argument);
}
TEST(CudaTile, ImageMatchesCpuBackToFrontAcrossMultipleBoundedBatchesAndEmptyReset)
{
    for (const auto size : {8u, 16u}) for (const auto quantize : {false, true})
    {
        TileFixture f;
        constexpr uint32_t count = 1301, width = 33, height = 19;
        std::vector<ProjectedEllipse> points(count);
        std::vector<uint32_t> order(count); std::iota(order.rbegin(), order.rend(), 0u);
        for (uint32_t i = 0; i < count; ++i)
        {
            points[i] = {{16.5f, 9.5f}, {20, 0}, {0, 20}, {.1f, .2f, .3f, .002f}};
            points[i].color[i % 3] = .9f;
        }
        SharedBuffer ellipses(f.context, points.size() * 40), indexes(f.context, order.size() * 4), args(f.context, 20);
        f.upload(ellipses, points.data(), ellipses.bytes()); f.upload(indexes, order.data(), indexes.bytes());
        DrawCounters counters{4, count, 0, 0, 0}; f.upload(args, &counters, 20);
        TileWorkspace lists(f.context, {count, width, height, size, count * 20, 16ull << 20});
        SharedTexture target(f.context, width, height);
        TileCompositeWorkspace composite(f.context, lists, 16ull << 20);
        for (const auto visible : {count, 0u, count})
        {
            counters.instance_count = visible; f.upload(args, &counters, 20);
            f.context.begin(f.gpu.direct.Get());
            lists.submit(f.context, ellipses, indexes, args);
            composite.submit(f.context, ellipses, indexes, target, {3, 0, quantize});
            f.context.end(f.gpu.direct.Get());
            check(f.gpu.direct->Signal(f.gpu.direct_fence.Get(), ++f.gpu.direct_value), "Composite finish");
            f.gpu.wait(f.gpu.direct_fence.Get(), f.gpu.direct_value);
            const auto actual = f.read_texture(target);
            for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x)
            {
                double result[4]{};
                for (uint32_t rank = 0; rank < visible; ++rank)
                {
                    const auto &e = points[order[rank]];
                    const double dx = x + .5 - e.center[0], dy = y + .5 - e.center[1];
                    const double determinant = double(e.axis0[0]) * e.axis1[1] - double(e.axis0[1]) * e.axis1[0];
                    const double u = (dx * e.axis1[1] - dy * e.axis1[0]) / determinant;
                    const double v = (dy * e.axis0[0] - dx * e.axis0[1]) / determinant;
                    const double radius2 = (u * u + v * v) * 9;
                    if (radius2 > 9) continue;
                    const double alpha = (std::min)(.999, e.color[3] * std::exp(-.5 * radius2));
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const double source = channel == 3 ? alpha : e.color[channel] * alpha;
                        result[channel] = source + (1 - alpha) * result[channel];
                        if (quantize) result[channel] = std::nearbyint(result[channel] * 255) / 255;
                    }
                }
                for (int channel = 0; channel < 4; ++channel)
                    ASSERT_LE(std::abs(int(actual[(y * width + x) * 4 + channel]) - int(std::nearbyint(result[channel] * 255))), 2)
                        << "pixel=" << x << ',' << y << " channel=" << channel << " tile=" << size << " quantize=" << quantize;
            }
        }
        EXPECT_TRUE(f.context.wait_idle()); EXPECT_TRUE(f.gpu.debug_errors().empty());
    }
}
