#include "model-io/fd_loader.h"
#include "render-core/device_probe.h"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace
{
std::atomic<int> failures = 0;
void expect(bool value, const char *name)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++failures;
    }
}

void write_fixture(int fd, float alpha, bool rewind = true)
{
    const std::string header =
        "ply\nformat binary_little_endian 1.0\nelement vertex 3\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
        "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n"
        "property float opacity\nproperty float f_dc_0\nproperty float f_dc_1\n"
        "property float f_dc_2\nend_header\n";
    const float row[] = {1, 2, 3, 0, 0, 0, 1, 0, 0, 0, alpha, 0, 0, 0};
    expect(write(fd, header.data(), header.size()) == static_cast<ssize_t>(header.size()),
           "write header");
    expect(write(fd, row, sizeof(row)) == static_cast<ssize_t>(sizeof(row)), "write row");
    float second[14];
    std::copy(std::begin(row), std::end(row), second);
    second[0] = 3;
    second[2] = 1;
    expect(write(fd, second, sizeof(second)) == sizeof(second), "write second row");
    second[0] = 1;
    second[2] = -2;
    expect(write(fd, second, sizeof(second)) == sizeof(second), "write third row");
    if (rewind)
        expect(lseek(fd, 0, SEEK_SET) == 0, "rewind fixture");
}
} // namespace

int run_joint_cases()
{
    char name[] = "/data/local/tmp/gs-android-model-XXXXXX";
    int fd = mkstemp(name);
    expect(fd >= 0, "create fixture");
    if (fd < 0)
        return 1;
    unlink(name);
    write_fixture(fd, INFINITY);
    auto result = gs::android::io::load_fd({fd});
    expect(std::holds_alternative<gs::SceneHandle>(result), "load valid PLY");
    if (auto scene = std::get_if<gs::SceneHandle>(&result))
    {
        expect((*scene)->count == 3, "count");
        expect((*scene)->opacity[0] == 1.0f, "infinite opacity");
        expect((*scene)->centerLocal[0] == -1.0f, "center origin");
        const auto device = gs::android::render::probe_device();
        std::printf("vulkan supported=%d result=%d gpu=%s range=%u budget=%llu diagnostic=%s\n",
                    device.supported, device.result, device.device_name.c_str(),
                    device.max_storage_buffer_range,
                    static_cast<unsigned long long>(device.heap_budget_bytes),
                    device.diagnostic.c_str());
        if (device.supported)
        {
            expect(gs::android::render::assess_scene(*scene, device).accepted,
                   "decoded scene accepted by Vulkan core");
            const char *directory = std::getenv("GS_SHADER_DIRECTORY");
            const auto diagnostic = gs::android::render::sort_scene_self_test(
                *scene, directory ? directory : "/data/local/tmp/shaders");
            expect(diagnostic.empty(), "decoded scene GPU stable ordering");
            if (!diagnostic.empty()) std::fprintf(stderr, "%s\n", diagnostic.c_str());
        }
    }
    expect(fcntl(fd, F_GETFD) >= 0, "caller retains fd");
    auto shared = gs::android::io::load_shared_fd({fd});
    expect(std::holds_alternative<gs::android::io::SharedScene>(shared),
           "decode shared scene");
    if (auto value = std::get_if<gs::android::io::SharedScene>(&shared))
    {
        int transfer = value->duplicate_fd();
        expect(transfer >= 0, "duplicate transfer fd");
        auto imported = gs::android::io::import_shared_fd(transfer, 1 << 20);
        if (auto error = std::get_if<gs::io::LoadError>(&imported))
            std::fprintf(stderr, "import error %d: %s\n", static_cast<int>(error->code),
                         error->diagnostic.c_str());
        expect(std::holds_alternative<gs::SceneHandle>(imported),
               "import validated shared scene");
        expect(fcntl(transfer, F_GETFD) >= 0, "import retains caller fd");
        close(transfer);
    }
    close(fd);
    int pipe_fds[2]{};
    if (pipe(pipe_fds) != 0)
    {
        expect(false, "create provider pipe");
        return 1;
    }
    std::thread writer([&] { write_fixture(pipe_fds[1], 0, false); close(pipe_fds[1]); });
    gs::android::io::FdLoadRequest stream_request{pipe_fds[0]};
    stream_request.temporary_directory = "/data/local/tmp";
    auto streamed = gs::android::io::load_fd(stream_request);
    expect(std::holds_alternative<gs::SceneHandle>(streamed), "nonseek provider stream");
    writer.join();
    expect(fcntl(pipe_fds[0], F_GETFD) >= 0, "caller retains pipe fd");
    close(pipe_fds[0]);
    std::printf("model-io tests: %d failure(s)\n", failures.load());
    return failures ? 1 : 0;
}

TEST(AndroidJoint, DecodeShareAndAdmitScene)
{
    EXPECT_EQ(run_joint_cases(), 0);
}
