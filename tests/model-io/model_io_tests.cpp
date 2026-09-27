#include "load-spz.h"
#include "model-io/model_loader.h"
#include "normalize.h"
#include "probe.h"
#include "test_loader.h"
#include <zlib.h>

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using namespace gs::io;

struct TempFile
{
    explicit TempFile(std::wstring extension = L".ply")
    {
        static std::atomic<unsigned> serial{0};
        path = std::filesystem::temp_directory_path() /
               (L"model-测试-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(serial++) + extension);
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::filesystem::path path;
};

std::vector<std::string> names()
{
    return {"rot_3",   "scale_2", "f_dc_1", "z",      "opacity", "x",     "rot_0",
            "scale_0", "y",       "rot_1",  "f_dc_0", "scale_1", "rot_2", "f_dc_2"};
}

float value(std::string_view name)
{
    if (name == "x")
        return 10;
    if (name == "y")
        return 2;
    if (name == "z")
        return 3;
    if (name == "rot_0")
        return 2;
    if (name.starts_with("f_rest_"))
        return float(std::stoi(std::string(name.substr(7))) + 1);
    return 0;
}

void write_ply(const std::filesystem::path &path, const std::vector<std::string> &properties,
               uint32_t count = 1, std::string encoding = "binary_little_endian",
               std::string invalid = {}, float invalidValue = 0, bool crlf = false,
               uint32_t invalidIndex = UINT32_MAX)
{
    std::ofstream out(path, std::ios::binary);
    const char *eol = crlf ? "\r\n" : "\n";
    out << "ply" << eol << "format " << encoding << " 1.0" << eol << "element vertex " << count
        << eol;
    for (const auto &name : properties)
        out << "property float " << name << eol;
    out << "element extras 1" << eol << "property float extra" << eol << "end_header" << eol;
    for (uint32_t i = 0; i < count; ++i)
        for (const auto &name : properties)
        {
            float v = name == invalid && (invalidIndex == UINT32_MAX || i == invalidIndex)
                          ? invalidValue
                          : value(name);
            out.write(reinterpret_cast<const char *>(&v), sizeof(v));
        }
    float extra = 7;
    out.write(reinterpret_cast<const char *>(&extra), sizeof(extra));
}

LoadResult load(const std::filesystem::path &path, Coordinates coordinates = Coordinates::Rdf,
                LoadLimits limits = {}, std::stop_token stop = {}, ProgressSink sink = {})
{
    return make_model_loader()->load({path, coordinates, limits}, stop, sink);
}

void expect_error(const LoadResult &result, LoadErrorCode code)
{
    ASSERT_TRUE(std::holds_alternative<LoadError>(result));
    EXPECT_EQ(std::get<LoadError>(result).code, code) << std::get<LoadError>(result).diagnostic;
}

std::string diagnostic(const LoadResult &result)
{
    if (auto failure = std::get_if<LoadError>(&result))
        return "code=" + std::to_string(static_cast<int>(failure->code)) + " " +
               failure->diagnostic;
    return "success";
}

void write_spz(const std::filesystem::path &path, uint32_t version, uint32_t degree = 1,
               float alpha = 0)
{
    spz::GaussianCloud cloud;
    cloud.numPoints = 1;
    cloud.shDegree = degree;
    cloud.positions = {1, 2, 3};
    cloud.scales = {0, 0, 0};
    cloud.rotations = {0, 0, 0, 1};
    cloud.alphas = {alpha};
    cloud.colors = {0, 0, 0};
    cloud.sh.assign(3 * ((degree + 1) * (degree + 1) - 1), 0);
    spz::PackOptions options;
    options.version = version == 1 ? 2 : version;
    options.from = spz::CoordinateSystem::RUB;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(spz::saveSpz(cloud, options, &bytes));
    if (version == 1)
    {
        std::vector<uint8_t> unpacked(4096);
        z_stream stream{};
        stream.next_in = bytes.data();
        stream.avail_in = static_cast<uInt>(bytes.size());
        stream.next_out = unpacked.data();
        stream.avail_out = static_cast<uInt>(unpacked.size());
        ASSERT_EQ(inflateInit2(&stream, 16 + MAX_WBITS), Z_OK);
        EXPECT_EQ(inflate(&stream, Z_FINISH), Z_STREAM_END);
        inflateEnd(&stream);
        unpacked.resize(stream.total_out);
        ASSERT_GE(unpacked.size(), 25u);
        unpacked[4] = 1; // v1 reads three IEEE half-float positions.
        const uint8_t halfPositions[]{0x00, 0x3c, 0x00, 0x40, 0x00, 0x42};
        std::vector<uint8_t> legacy(unpacked.begin(), unpacked.begin() + 16);
        legacy.insert(legacy.end(), std::begin(halfPositions), std::end(halfPositions));
        legacy.insert(legacy.end(), unpacked.begin() + 25, unpacked.end());
        ASSERT_TRUE(spz::compressGzipped(legacy.data(), legacy.size(), &bytes));
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::filesystem::path fault_helper_path()
{
    std::array<wchar_t, 32768> executable{};
    const DWORD size =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!size || size >= executable.size())
        return {};
    return std::filesystem::path(executable.data()).parent_path() / L"model-io-fault-helper.exe";
}

struct FaultMode
{
    explicit FaultMode(const wchar_t *mode)
    {
        SetEnvironmentVariableW(L"MODEL_IO_FAULT_MODE", mode);
    }
    ~FaultMode()
    {
        SetEnvironmentVariableW(L"MODEL_IO_FAULT_MODE", nullptr);
    }
};
} // namespace

TEST(ModelIo, RejectsMissingFile)
{
    expect_error(load(L"Z:\\does-not-exist.ply"), LoadErrorCode::NotFound);
}

TEST(ModelIo, ReadsReorderedPlyAndConvertsCoordinates)
{
    TempFile file;
    auto properties = names();
    properties.push_back("unknown_scalar");
    write_ply(file.path, properties);
    auto result = load(file.path);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    const auto scene = std::get<gs::SceneHandle>(result);
    EXPECT_EQ(scene->count, 1);
    EXPECT_EQ(scene->shDegree, 0);
    EXPECT_DOUBLE_EQ(scene->worldOrigin.y, -2);
    EXPECT_DOUBLE_EQ(scene->worldOrigin.z, -3);
    EXPECT_FLOAT_EQ(scene->rotation[3], 1);
    EXPECT_FLOAT_EQ(scene->opacity[0], 0.5f);
    EXPECT_FLOAT_EQ(scene->rgb0[0], 0.5f);
    EXPECT_FLOAT_EQ(scene->scale[0], 1);
    EXPECT_TRUE(scene->shRest.empty());
    result = load(file.path, Coordinates::Rub);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result));
    EXPECT_DOUBLE_EQ(std::get<gs::SceneHandle>(result)->worldOrigin.y, 2);
}

TEST(ModelIo, ConvertsChannelMajorShForEveryDegree)
{
    for (uint32_t degree = 1; degree <= 3; ++degree)
    {
        TempFile file;
        auto properties = names();
        const uint32_t rest = 3 * ((degree + 1) * (degree + 1) - 1);
        for (uint32_t i = 0; i < rest; ++i)
            properties.push_back("f_rest_" + std::to_string(i));
        write_ply(file.path, properties);
        auto result = load(file.path);
        ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
        const auto scene = std::get<gs::SceneHandle>(result);
        EXPECT_EQ(scene->shDegree, degree);
        EXPECT_EQ(scene->shRest.size(), rest);
        EXPECT_FLOAT_EQ(scene->shRest[0], -1);
        EXPECT_FLOAT_EQ(scene->shRest[1], -float(rest / 3 + 1));
    }
}

TEST(ModelIo, RejectsMalformedPly)
{
    TempFile missing, partial, ascii, truncated, nan, scale;
    auto properties = names();
    properties.erase(std::remove(properties.begin(), properties.end(), "rot_0"), properties.end());
    write_ply(missing.path, properties);
    expect_error(load(missing.path), LoadErrorCode::InvalidHeader);
    properties = names();
    properties.push_back("f_rest_0");
    write_ply(partial.path, properties);
    expect_error(load(partial.path), LoadErrorCode::InvalidHeader);
    write_ply(ascii.path, names(), 1, "ascii");
    expect_error(load(ascii.path), LoadErrorCode::UnsupportedFormat);
    write_ply(truncated.path, names(), 2);
    std::filesystem::resize_file(truncated.path, std::filesystem::file_size(truncated.path) - 32);
    expect_error(load(truncated.path), LoadErrorCode::TruncatedData);
    write_ply(nan.path, names(), 1, "binary_little_endian", "x",
              std::numeric_limits<float>::quiet_NaN());
    const auto nan_result = load(nan.path);
    expect_error(nan_result, LoadErrorCode::InvalidAttribute);
    ASSERT_TRUE(std::holds_alternative<LoadError>(nan_result));
    EXPECT_NE(std::get<LoadError>(nan_result).diagnostic.find("x"), std::string::npos);
    EXPECT_TRUE(std::get<LoadError>(nan_result).byteOffset.has_value());
    write_ply(scale.path, names(), 1, "binary_little_endian", "scale_0", 100);
    const auto scale_result = load(scale.path);
    expect_error(scale_result, LoadErrorCode::InvalidAttribute);
    ASSERT_TRUE(std::holds_alternative<LoadError>(scale_result));
    EXPECT_NE(std::get<LoadError>(scale_result).diagnostic.find("scale_0"),
              std::string::npos);
}

TEST(ModelIo, StreamsAcrossMultiplePlyChunks)
{
    constexpr uint32_t count = 200'001;
    TempFile file;
    write_ply(file.path, names(), count);
    auto loaded = load(file.path);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(loaded)) << diagnostic(loaded);
    EXPECT_EQ(std::get<gs::SceneHandle>(loaded)->count, count);
    write_ply(file.path, names(), count, "binary_little_endian", "x",
              std::numeric_limits<float>::quiet_NaN(), false, count - 1);
    auto invalid = load(file.path);
    expect_error(invalid, LoadErrorCode::InvalidAttribute);
    ASSERT_TRUE(std::holds_alternative<LoadError>(invalid));
    EXPECT_NE(std::get<LoadError>(invalid).diagnostic.find("index 200000 (x)"),
              std::string::npos);
}

TEST(ModelIo, ConvertsInfinitePlyOpacityLogits)
{
    for (const auto [logit, expected] :
         {std::pair{std::numeric_limits<float>::infinity(), 1.0f},
          std::pair{-std::numeric_limits<float>::infinity(), 0.0f}})
    {
        TempFile file;
        write_ply(file.path, names(), 1, "binary_little_endian", "opacity", logit);
        auto result = load(file.path);
        ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
        EXPECT_FLOAT_EQ(std::get<gs::SceneHandle>(result)->opacity[0], expected);
    }
    TempFile nan;
    write_ply(nan.path, names(), 1, "binary_little_endian", "opacity",
              std::numeric_limits<float>::quiet_NaN());
    expect_error(load(nan.path), LoadErrorCode::InvalidAttribute);
}

TEST(ModelIo, EnforcesLimitsCancellationAndObserverErrors)
{
    TempFile file;
    write_ply(file.path, names());
    LoadLimits limits;
    limits.maxSplats = 0;
    expect_error(load(file.path, Coordinates::Rdf, limits), LoadErrorCode::ResourceLimit);
    limits.maxSplats = uint64_t(UINT32_MAX) + 1;
    expect_error(load(file.path, Coordinates::Rdf, limits), LoadErrorCode::ResourceLimit);
    std::stop_source stop;
    stop.request_stop();
    expect_error(load(file.path, Coordinates::Rdf, {}, stop.get_token()), LoadErrorCode::Cancelled);
    expect_error(load(file.path, Coordinates::Rdf, {}, {},
                      [](const LoadProgress &) { throw std::runtime_error("observer"); }),
                 LoadErrorCode::ObserverFailure);
}

TEST(ModelIo, ReadsSpzVersionsOneThroughFour)
{
    for (uint32_t version = 1; version <= 4; ++version)
    {
        TempFile file(L".spz");
        write_spz(file.path, version);
        auto result = load(file.path);
        ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result))
            << "version=" << version << " " << diagnostic(result);
        const auto scene = std::get<gs::SceneHandle>(result);
        EXPECT_EQ(scene->sourceFormat, gs::SourceFormat::Spz);
        EXPECT_EQ(scene->shDegree, 1);
        EXPECT_NEAR(scene->worldOrigin.x, 1, 0.01);
    }
}

TEST(ModelIo, RejectsSpzFeatures)
{
    TempFile file(L".spz");
    write_spz(file.path, 4, 4);
    expect_error(load(file.path), LoadErrorCode::UnsupportedFeature);
    write_spz(file.path, 4);
    std::fstream data(file.path, std::ios::binary | std::ios::in | std::ios::out);
    data.seekp(14);
    char flag = 1;
    data.write(&flag, 1);
    data.close();
    expect_error(load(file.path), LoadErrorCode::UnsupportedFeature);
}

TEST(ModelIo, ConvertsSaturatedSpzAlphaToFiniteOpacity)
{
    TempFile opaque(L".spz"), transparent(L".spz");
    write_spz(opaque.path, 4, 0, 1000);
    write_spz(transparent.path, 4, 0, -1000);
    auto result = load(opaque.path);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    EXPECT_FLOAT_EQ(std::get<gs::SceneHandle>(result)->opacity[0], 1);
    result = load(transparent.path);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    EXPECT_FLOAT_EQ(std::get<gs::SceneHandle>(result)->opacity[0], 0);
}

TEST(ModelIo, RejectsInvalidRotationScaleAndDuplicateProperties)
{
    TempFile rotation, scale, duplicate;
    write_ply(rotation.path, names(), 1, "binary_little_endian", "rot_0", 0);
    expect_error(load(rotation.path), LoadErrorCode::InvalidAttribute);
    write_ply(scale.path, names(), 1, "binary_little_endian", "scale_0", 100);
    expect_error(load(scale.path), LoadErrorCode::InvalidAttribute);
    auto properties = names();
    properties.push_back("x");
    write_ply(duplicate.path, properties);
    expect_error(load(duplicate.path), LoadErrorCode::InvalidHeader);
}

TEST(ModelIo, SniffsContentAndReportsMonotonicProgress)
{
    TempFile file(L".spz");
    write_ply(file.path, names());
    std::vector<LoadStage> stages;
    auto result = load(file.path, Coordinates::Rdf, {}, {}, [&](const LoadProgress &p) {
        stages.push_back(p.stage);
        if (p.stage == LoadStage::Decoding || p.stage == LoadStage::Validating)
            EXPECT_EQ(p.bytesRead, 0);
    });
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    EXPECT_EQ(std::get<gs::SceneHandle>(result)->sourceFormat, gs::SourceFormat::Ply);
    EXPECT_EQ(stages, (std::vector<LoadStage>{LoadStage::Opening, LoadStage::Inspecting,
                                              LoadStage::Decoding, LoadStage::Validating,
                                              LoadStage::Ready}));
}

TEST(ModelIo, AcceptsCrlfPlyHeader)
{
    TempFile file;
    write_ply(file.path, names(), 1, "binary_little_endian", {}, 0, true);
    auto result = load(file.path);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
}

TEST(ModelIo, RandomHeaderAndLayoutMutationsNeverPublishScene)
{
    std::mt19937_64 random(0x37a5a715);
    LoadLimits limits;
    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        const size_t length = random() % 256;
        std::vector<uint8_t> bytes(length);
        for (auto &byte : bytes)
            byte = static_cast<uint8_t>(random());
        auto result = gs::io::detail::probe_file(bytes, length, limits);
        EXPECT_FALSE(result.ok);
    }
    gs::io::detail::Probe probe{gs::SourceFormat::Ply, 2, 3, 0};
    auto layout = gs::io::detail::make_layout(probe, 4096);
    ASSERT_TRUE(layout);
    std::vector<uint8_t> bytes(layout->totalBytes);
    std::memcpy(bytes.data(), &*layout, sizeof(*layout));
    auto *header = reinterpret_cast<gs::io::detail::SceneHeader *>(bytes.data());
    for (size_t i = 0; i < gs::io::detail::kArrayCount; ++i)
    {
        const auto saved = header->offsets[i];
        header->offsets[i] = saved + 16;
        EXPECT_FALSE(gs::io::detail::validate_scene(*header, bytes.size()));
        header->offsets[i] = saved;
    }
}

TEST(ModelIoFault, IsolatesCrashMalformedIpcAndSharedData)
{
    TempFile file;
    write_ply(file.path, names());
    auto path = fault_helper_path();
    ASSERT_TRUE(exists(path));
    for (const auto &[mode, code] :
         std::array{std::pair{L"crash", LoadErrorCode::DecoderCrashed},
                    std::pair{L"malformed", LoadErrorCode::DecoderFailure},
                    std::pair{L"corrupt", LoadErrorCode::InvalidAttribute},
                    std::pair{L"flood", LoadErrorCode::DecoderFailure}})
    {
        FaultMode fault(mode);
        auto loader = gs::io::detail::make_model_loader_for_testing({path});
        expect_error(loader->load({file.path}, {}, {}), code);
    }
}

TEST(ModelIoFault, BoundsTimeoutCancellationAndJobFailure)
{
    TempFile file;
    write_ply(file.path, names());
    auto path = fault_helper_path();
    ASSERT_TRUE(exists(path));
    {
        FaultMode fault(L"hang");
        auto loader =
            gs::io::detail::make_model_loader_for_testing({path, std::chrono::milliseconds(100)});
        expect_error(loader->load({file.path}, {}, {}), LoadErrorCode::Timeout);
        std::stop_source cancel;
        LoadResult result;
        std::thread worker([&] {
            auto next = gs::io::detail::make_model_loader_for_testing({path});
            result = next->load({file.path}, cancel.get_token(), {});
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        cancel.request_stop();
        worker.join();
        expect_error(result, LoadErrorCode::Cancelled);
    }
    auto loader = gs::io::detail::make_model_loader_for_testing(
        {path, std::chrono::milliseconds(180000), true});
    expect_error(loader->load({file.path}, {}, {}), LoadErrorCode::DecoderFailure);
}

TEST(ModelIoFault, ReportsHelperJobMemoryLimit)
{
    TempFile file;
    write_ply(file.path, names());
    FaultMode fault(L"memory");
    auto loader = gs::io::detail::make_model_loader_for_testing(
        {fault_helper_path(), std::chrono::milliseconds(180000), false, 128ull << 20});
    expect_error(loader->load({file.path}, {}, {}), LoadErrorCode::ResourceLimit);
}

TEST(ModelIo, FailureDoesNotPoisonNextLoad)
{
    TempFile bad, good;
    write_ply(bad.path, names(), 1, "ascii");
    write_ply(good.path, names());
    auto loader = make_model_loader();
    EXPECT_TRUE(std::holds_alternative<LoadError>(loader->load({bad.path}, {}, {})));
    EXPECT_TRUE(std::holds_alternative<gs::SceneHandle>(loader->load({good.path}, {}, {})));
}

TEST(ModelIo, RepeatedLoadsReleaseHandles)
{
    TempFile file;
    write_ply(file.path, names());
    DWORD before = 0, after = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 100; ++i)
    {
        auto result = load(file.path);
        ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    }
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_LE(after, before + 4);
}

TEST(ModelIoCorpus, LoadsWorkspaceSamples)
{
    const auto workspace =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const auto ply = workspace / "1.ply";
    const auto smallSpz = workspace / "Viewer_android/public/scene/jidaoshan.spz";
    const auto largeSpz = workspace / "Viewer_android/public/scene/zhihuizhimen.spz";
    if (!exists(ply) || !exists(smallSpz) || !exists(largeSpz))
        GTEST_SKIP() << "External sample corpus unavailable";
    auto result = load(ply);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    EXPECT_EQ(std::get<gs::SceneHandle>(result)->count, 1'179'648);
    result = load(smallSpz);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    EXPECT_EQ(std::get<gs::SceneHandle>(result)->count, 509'812);
    result = load(largeSpz);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result)) << diagnostic(result);
    EXPECT_EQ(std::get<gs::SceneHandle>(result)->count, 3'914'609);
}

TEST(ModelIoCorpus, CancelsActiveDecode)
{
    const auto workspace =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const auto largeSpz = workspace / "Viewer_android/public/scene/zhihuizhimen.spz";
    if (!exists(largeSpz))
        GTEST_SKIP() << "External sample corpus unavailable";
    std::stop_source cancel;
    LoadResult result;
    std::thread worker([&] { result = load(largeSpz, Coordinates::Rdf, {}, cancel.get_token()); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel.request_stop();
    worker.join();
    expect_error(result, LoadErrorCode::Cancelled);
}
