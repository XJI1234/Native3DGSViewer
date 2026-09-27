#include "model-io/fd_loader.h"
#include "../../src/model-io/common/protocol.h"

#include <array>
#include <dirent.h>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <android/sharedmem.h>
#include <android/log.h>
#include "load-spz.h"
#include <string>
#include <string_view>
#include <thread>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

namespace
{
int failures = 0;
void expect(bool condition, const char *case_name)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL %s\n", case_name);
        ++failures;
    }
}

int fixture(float opacity, uint32_t count = 1)
{
    char path[] = "/data/local/tmp/gs-model-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0)
        return fd;
    unlink(path);
    const std::string header =
        "ply\nformat binary_little_endian 1.0\nelement vertex " + std::to_string(count) + "\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
        "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n"
        "property float opacity\nproperty float f_dc_0\nproperty float f_dc_1\n"
        "property float f_dc_2\nend_header\n";
    const std::array<float, 14> row{1, 2, 3, 0, 0, 0, 1, 0, 0, 0,
                                    opacity, 0, 0, 0};
    if (write(fd, header.data(), header.size()) != static_cast<ssize_t>(header.size()))
    {
        close(fd);
        return -1;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        if (write(fd, row.data(), sizeof(row)) != sizeof(row))
        {
            close(fd);
            return -1;
        }
    }
    return fd;
}

void expect_error(const gs::io::LoadResult &result, gs::io::LoadErrorCode code,
                  const char *case_name)
{
    const auto *error = std::get_if<gs::io::LoadError>(&result);
    expect(error && error->code == code, case_name);
}
} // namespace

TEST(AndroidModelIo, ImportCopiesWritableSenderScene)
{
    const int input = fixture(0);
    ASSERT_GE(input, 0);
    auto decoded = gs::android::io::load_shared_fd({input});
    close(input);
    ASSERT_TRUE(std::holds_alternative<gs::android::io::SharedScene>(decoded));
    const int source_fd = std::get<gs::android::io::SharedScene>(decoded).duplicate_fd();
    ASSERT_GE(source_fd, 0);
    const auto bytes = ASharedMemory_getSize(source_fd);
    ASSERT_GT(bytes, 0);
    const int writable_fd = ASharedMemory_create("mutable-scene-test", bytes);
    ASSERT_GE(writable_fd, 0);
    void *source = mmap(nullptr, bytes, PROT_READ, MAP_SHARED, source_fd, 0);
    void *writable = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, writable_fd, 0);
    ASSERT_NE(source, MAP_FAILED);
    ASSERT_NE(writable, MAP_FAILED);
    std::memcpy(writable, source, bytes);
    auto imported = gs::android::io::import_shared_fd(writable_fd, bytes);
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(imported));
    const auto scene = std::get<gs::SceneHandle>(imported);
    const float original = scene->centerLocal[0];
    const auto *header = static_cast<const gs::io::detail::SceneHeader *>(writable);
    auto *center = reinterpret_cast<float *>(static_cast<uint8_t *>(writable) +
                                             header->offsets[gs::io::detail::Center]);
    center[0] = original + 100;
    EXPECT_FLOAT_EQ(original, scene->centerLocal[0]);
    auto *mutable_header = static_cast<gs::io::detail::SceneHeader *>(writable);
    mutable_header->offsets[gs::io::detail::Center] = UINT64_MAX;
    auto malformed = gs::android::io::import_shared_fd(writable_fd, bytes);
    EXPECT_TRUE(std::holds_alternative<gs::io::LoadError>(malformed));
    if (const auto *error = std::get_if<gs::io::LoadError>(&malformed))
        EXPECT_EQ(error->code, gs::io::LoadErrorCode::InvalidAttribute);
    munmap(source, bytes);
    munmap(writable, bytes);
    close(source_fd);
    close(writable_fd);
}

TEST(AndroidModelIo, ConcurrentLoadsDoNotChangeCallerFileOffset)
{
    const int input = fixture(0, 20'000);
    ASSERT_GE(input, 0);
    ASSERT_EQ(lseek(input, 5, SEEK_SET), 5);
    gs::io::LoadResult first, second;
    std::thread a([&] { first = gs::android::io::load_fd({input}); });
    std::thread b([&] { second = gs::android::io::load_fd({input}); });
    a.join();
    b.join();
    EXPECT_TRUE(std::holds_alternative<gs::SceneHandle>(first));
    EXPECT_TRUE(std::holds_alternative<gs::SceneHandle>(second));
    EXPECT_EQ(lseek(input, 0, SEEK_CUR), 5);
    close(input);
}

TEST(AndroidModelIo, SharedImportCanBeCancelled)
{
    const int input = fixture(0);
    ASSERT_GE(input, 0);
    auto decoded = gs::android::io::load_shared_fd({input});
    close(input);
    ASSERT_TRUE(std::holds_alternative<gs::android::io::SharedScene>(decoded));
    const int transferred = std::get<gs::android::io::SharedScene>(decoded).duplicate_fd();
    ASSERT_GE(transferred, 0);
    auto imported = gs::android::io::import_shared_fd(transferred, UINT64_MAX,
                                                       [] { return true; });
    close(transferred);
    ASSERT_TRUE(std::holds_alternative<gs::io::LoadError>(imported));
    EXPECT_EQ(std::get<gs::io::LoadError>(imported).code, gs::io::LoadErrorCode::Cancelled);
}

TEST(AndroidModelIo, EmptyRegularFileIsTruncated)
{
    char path[] = "/data/local/tmp/gs-empty-XXXXXX";
    const int input = mkstemp(path);
    ASSERT_GE(input, 0);
    unlink(path);
    auto result = gs::android::io::load_fd({input});
    close(input);
    ASSERT_TRUE(std::holds_alternative<gs::io::LoadError>(result));
    EXPECT_EQ(std::get<gs::io::LoadError>(result).code, gs::io::LoadErrorCode::TruncatedData);
}

TEST(AndroidModelIo, TruncatedSpzReturnsErrorWithoutMappingFault)
{
    spz::GaussianCloud cloud;
    cloud.numPoints = 1;
    cloud.shDegree = 0;
    cloud.positions = {1, 2, 3};
    cloud.scales = {0, 0, 0};
    cloud.rotations = {0, 0, 0, 1};
    cloud.alphas = {0};
    cloud.colors = {0, 0, 0};
    spz::PackOptions options;
    options.from = spz::CoordinateSystem::RUB;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(spz::saveSpz(cloud, options, &bytes));
    char path[] = "/data/local/tmp/gs-truncate-XXXXXX";
    const int input = mkstemp(path);
    ASSERT_GE(input, 0);
    unlink(path);
    ASSERT_EQ(write(input, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
    auto result = gs::android::io::load_fd({input}, {}, [&](const gs::io::LoadProgress &progress) {
        if (progress.stage == gs::io::LoadStage::Decoding) ftruncate(input, 0);
    });
    close(input);
    ASSERT_TRUE(std::holds_alternative<gs::io::LoadError>(result));
    EXPECT_EQ(std::get<gs::io::LoadError>(result).code, gs::io::LoadErrorCode::TruncatedData);
}

int run_model_io_cases()
{
    int fd = fixture(INFINITY);
    expect(fd >= 0, "create fixture");
    if (fd < 0)
        return 1;
    auto loaded = gs::android::io::load_fd({fd});
    expect(std::holds_alternative<gs::SceneHandle>(loaded), "valid infinite opacity");
    if (const auto *scene = std::get_if<gs::SceneHandle>(&loaded))
        expect((*scene)->opacity[0] == 1, "positive infinite opacity is opaque");

    gs::android::io::FdLoadRequest limited{fd};
    limited.limits.maxInputBytes = 32;
    expect_error(gs::android::io::load_fd(limited), gs::io::LoadErrorCode::ResourceLimit,
                 "seekable input byte limit");
    expect_error(gs::android::io::load_fd({fd}, [] { return true; }),
                 gs::io::LoadErrorCode::Cancelled, "cancel before decode");
    expect(fcntl(fd, F_GETFD) >= 0, "caller fd survives load failure");
    close(fd);

    fd = fixture(NAN);
    expect(fd >= 0, "create corrupt fixture");
    if (fd >= 0)
    {
        expect_error(gs::android::io::load_fd({fd}),
                     gs::io::LoadErrorCode::InvalidAttribute, "reject NaN opacity");
        close(fd);
    }

    int pipes[2]{};
    if (pipe(pipes) != 0) return 1;
    gs::android::io::FdLoadRequest request{pipes[0]};
    request.temporary_directory = "/data/local/tmp";
    request.limits.maxInputBytes = 4;
    std::thread producer([&] {
        const char data[] = "not a valid model";
        write(pipes[1], data, sizeof(data));
        close(pipes[1]);
    });
    expect_error(gs::android::io::load_fd(request), gs::io::LoadErrorCode::ResourceLimit,
                 "provider copy byte limit");
    producer.join();
    close(pipes[0]);

    int idle[2]{};
    if (pipe(idle) != 0) return 1;
    request.fd = idle[0];
    int polls = 0;
    expect_error(gs::android::io::load_fd(request, [&] { return ++polls > 2; }),
                 gs::io::LoadErrorCode::Cancelled, "cancel idle provider stream");
    close(idle[0]);
    close(idle[1]);

    expect_error(gs::android::io::import_shared_fd(-1, 1024),
                 gs::io::LoadErrorCode::IoFailure, "reject invalid transfer fd");
    std::printf("model-io independent: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}

TEST(AndroidModelIo, InputCancellationAndResourceContracts)
{
    EXPECT_EQ(run_model_io_cases(), 0);
}

TEST(AndroidModelIo, SpzDegreesRoundTrip)
{
    for (uint8_t degree = 0; degree <= 3; ++degree)
    {
        spz::GaussianCloud cloud;
        cloud.numPoints = 1;
        cloud.shDegree = degree;
        cloud.positions = {1, 2, 3};
        cloud.scales = {0, 0, 0};
        cloud.rotations = {0, 0, 0, 1};
        cloud.alphas = {0};
        cloud.colors = {0, 0, 0};
        cloud.sh.resize(3 * ((degree + 1) * (degree + 1) - 1), 0.1f);
        spz::PackOptions options;
        options.from = spz::CoordinateSystem::RUB;
        std::vector<uint8_t> bytes;
        ASSERT_TRUE(spz::saveSpz(cloud, options, &bytes));
        char path[] = "/data/local/tmp/gs-spz-XXXXXX";
        const int fd = mkstemp(path);
        ASSERT_GE(fd, 0);
        unlink(path);
        ASSERT_EQ(write(fd, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
        auto result = gs::android::io::load_fd({fd});
        close(fd);
        ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(result));
        const auto &scene = std::get<gs::SceneHandle>(result);
        EXPECT_EQ(scene->count, 1);
        EXPECT_EQ(scene->shDegree, degree);
        EXPECT_EQ(scene->shRest.size(), cloud.sh.size());
    }
}

TEST(AndroidModelIo, EmptySharedSceneIsRejected)
{
    const int fd = ASharedMemory_create("bad-scene", 1024);
    ASSERT_GE(fd, 0);
    const auto result = gs::android::io::import_shared_fd(fd, 1024);
    close(fd);
    ASSERT_TRUE(std::holds_alternative<gs::io::LoadError>(result));
    const auto code = std::get<gs::io::LoadError>(result).code;
    EXPECT_TRUE(code == gs::io::LoadErrorCode::IoFailure ||
                code == gs::io::LoadErrorCode::TruncatedData ||
                code == gs::io::LoadErrorCode::InvalidAttribute);
}

TEST(AndroidModelIo, BudgetUsesDecodePeak)
{
    const int fd = fixture(0);
    ASSERT_GE(fd, 0);
    gs::android::io::FdLoadRequest request{fd};
    request.available_memory_bytes = 1 << 20;
    const auto result = gs::android::io::load_fd(request);
    close(fd);
    ASSERT_TRUE(std::holds_alternative<gs::io::LoadError>(result));
    EXPECT_EQ(std::get<gs::io::LoadError>(result).code, gs::io::LoadErrorCode::ResourceLimit);
}

TEST(AndroidModelIo, CrossChunkDecodeAndCancellation)
{
    const int fd = fixture(0, 75000);
    ASSERT_GE(fd, 0);
    auto loaded = gs::android::io::load_fd({fd});
    ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(loaded));
    EXPECT_EQ(std::get<gs::SceneHandle>(loaded)->count, 75000);
    int polls = 0;
    const auto cancelled = gs::android::io::load_fd({fd}, [&] { return ++polls >= 3; });
    close(fd);
    ASSERT_TRUE(std::holds_alternative<gs::io::LoadError>(cancelled));
    EXPECT_EQ(std::get<gs::io::LoadError>(cancelled).code, gs::io::LoadErrorCode::Cancelled);
}

TEST(AndroidModelIo, RepeatedSharedSceneReleaseDoesNotLeakDescriptors)
{
    auto descriptor_count = [] {
        size_t count = 0;
        DIR *directory = opendir("/proc/self/fd");
        if (!directory)
            return SIZE_MAX;
        while (auto *entry = readdir(directory))
            if (entry->d_name[0] != '.')
                ++count;
        closedir(directory);
        return count;
    };
    const int fd = fixture(0);
    ASSERT_GE(fd, 0);
    const auto before = descriptor_count();
    ASSERT_NE(before, SIZE_MAX);
    for (int i = 0; i < 100; ++i)
    {
        auto shared = gs::android::io::load_shared_fd({fd});
        ASSERT_TRUE(std::holds_alternative<gs::android::io::SharedScene>(shared));
        auto &value = std::get<gs::android::io::SharedScene>(shared);
        const int transferred = value.duplicate_fd();
        ASSERT_GE(transferred, 0);
        const auto imported = gs::android::io::import_shared_fd(transferred, 1 << 20);
        close(transferred);
        ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(imported));
    }
    EXPECT_EQ(descriptor_count(), before);
    close(fd);
}

TEST(AndroidModelIo, PinnedSpzSamples)
{
    const char *directory = std::getenv("GS_SAMPLE_DIRECTORY");
    if (!directory)
        GTEST_SKIP() << "Run tests/run-device-tests.ps1 to install pinned sample fixtures";
    for (const char *name : {"hornedlizard.spz", "racoonfamily.spz"})
    {
        const int fd = open((std::string(directory) + "/" + name).c_str(), O_RDONLY | O_CLOEXEC);
        ASSERT_GE(fd, 0);
        const auto started = std::chrono::steady_clock::now();
        auto result = gs::android::io::load_fd({fd});
        close(fd);
        if (auto *error = std::get_if<gs::io::LoadError>(&result))
        {
            if (std::string_view(name) == "racoonfamily.spz")
            {
                EXPECT_EQ(error->code, gs::io::LoadErrorCode::UnsupportedFeature);
                std::printf("sample=%s unsupported_feature=%s\n", name, error->diagnostic.c_str());
                continue;
            }
            FAIL() << error->diagnostic;
        }
        const auto &scene = std::get<gs::SceneHandle>(result);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        struct rusage usage{};
        ASSERT_EQ(getrusage(RUSAGE_SELF, &usage), 0);
        std::printf("sample=%s count=%llu degree=%u decode_ms=%lld process_peak_rss_kib=%ld\n",
                    name, static_cast<unsigned long long>(scene->count), scene->shDegree,
                    static_cast<long long>(elapsed), usage.ru_maxrss);
        EXPECT_GT(scene->count, 0);
    }
}
