#include "model-io/fd_loader.h"

#include "../../../src/model-io/codecs/decode.h"
#include "../../../src/model-io/normalize/normalize.h"

#include <android/sharedmem.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <poll.h>
#include <utility>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <vector>

namespace gs::android::io
{
namespace
{
using namespace gs::io;
using namespace gs::io::detail;

int duplicate(int fd)
{
    return fcntl(fd, F_DUPFD_CLOEXEC, 0);
}

class Fd
{
  public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) close(fd_); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd(Fd &&other) noexcept : fd_(other.release()) {}
    Fd &operator=(Fd &&other) noexcept
    {
        if (this != &other)
        {
            if (fd_ >= 0)
                close(fd_);
            fd_ = other.release();
        }
        return *this;
    }
    int get() const { return fd_; }
    int release() { return std::exchange(fd_, -1); }

  private:
    int fd_;
};

class Mapping
{
  public:
    Mapping(void *data, size_t bytes) : data_(data), bytes_(bytes) {}
    ~Mapping() { if (data_ != MAP_FAILED) munmap(data_, bytes_); }
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;
    Mapping(Mapping &&other) noexcept
        : data_(other.release()), bytes_(other.bytes_) {}
    void *get() const { return data_; }
    void *release() { return std::exchange(data_, MAP_FAILED); }

  private:
    void *data_;
    size_t bytes_;
};

struct PositionalStream
{
    int fd;
    off_t offset = 0;
    off_t size;
};

int positional_read(void *cookie, char *data, int bytes)
{
    auto &stream = *static_cast<PositionalStream *>(cookie);
    ssize_t count;
    do { count = pread(stream.fd, data, static_cast<size_t>(bytes), stream.offset); }
    while (count < 0 && errno == EINTR);
    if (count > 0) stream.offset += count;
    return static_cast<int>(count);
}

fpos_t positional_seek(void *cookie, fpos_t offset, int whence)
{
    auto &stream = *static_cast<PositionalStream *>(cookie);
    const off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? stream.offset
        : whence == SEEK_END ? stream.size : -1;
    if (base < 0 || (offset < 0 && offset < -base) ||
        (offset > 0 && base > std::numeric_limits<off_t>::max() - offset))
    {
        errno = EINVAL;
        return -1;
    }
    stream.offset = base + offset;
    return stream.offset;
}

uint64_t available_memory()
{
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen("/proc/meminfo", "r"), fclose);
    if (file)
    {
        char line[256]{};
        while (fgets(line, sizeof(line), file.get()))
        {
            unsigned long long kilobytes = 0;
            if (sscanf(line, "MemAvailable: %llu kB", &kilobytes) == 1)
                return kilobytes <= UINT64_MAX / 1024 ? kilobytes * 1024 : UINT64_MAX;
        }
    }
    struct sysinfo info{};
    if (sysinfo(&info))
        return 0;
    const auto units = static_cast<uint64_t>(info.mem_unit);
    const auto free = static_cast<uint64_t>(info.freeram) + info.bufferram;
    return free > UINT64_MAX / units ? UINT64_MAX : free * units;
}

LoadError failure(LoadErrorCode code, LoadStage stage, const char *message)
{
    return error(code, stage, message);
}

std::optional<LoadError> read_exact(int fd, void *target, size_t bytes,
                                    const std::function<bool()> &cancelled,
                                    LoadStage stage)
{
    size_t done = 0;
    while (done < bytes)
    {
        if (cancelled && cancelled())
            return failure(LoadErrorCode::Cancelled, stage, "Input copy cancelled");
        const size_t chunk = std::min<size_t>(bytes - done, 1 << 16);
        const ssize_t count = pread(fd, static_cast<uint8_t *>(target) + done, chunk,
                                    static_cast<off_t>(done));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
            return failure(count ? LoadErrorCode::IoFailure : LoadErrorCode::TruncatedData,
                           stage, "Input copy");
        done += static_cast<size_t>(count);
    }
    return {};
}

std::optional<LoadError> read_prefix(int fd, uint64_t size, std::vector<uint8_t> &prefix)
{
    prefix.resize(static_cast<size_t>((std::min<uint64_t>)(size, 1ull << 20)));
    size_t done = 0;
    while (done < prefix.size())
    {
        const ssize_t n = pread(fd, prefix.data() + done, prefix.size() - done,
                                static_cast<off_t>(done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return failure(n ? LoadErrorCode::IoFailure : LoadErrorCode::TruncatedData,
                           LoadStage::Inspecting, "Input prefix read");
        done += static_cast<size_t>(n);
    }
    return {};
}

std::optional<LoadError> copy_provider(int source, const FdLoadRequest &request,
                                       const std::function<bool()> &cancelled,
                                       ProgressSink progress, Fd &destination,
                                       uint64_t &bytes_copied)
{
    if (request.temporary_directory.empty())
        return failure(LoadErrorCode::UnsupportedFeature, LoadStage::Opening,
                       "Private temporary directory required for provider stream");
    std::string path = request.temporary_directory + "/native3dgs-XXXXXX";
    const int fd = mkostemp(path.data(), O_CLOEXEC);
    if (fd < 0)
        return failure(LoadErrorCode::IoFailure, LoadStage::Opening, "Temporary file creation");
    destination = Fd(fd);
    if (unlink(path.c_str()))
        return failure(LoadErrorCode::IoFailure, LoadStage::Opening, "Temporary file removal");
    std::array<uint8_t, 1 << 16> buffer{};
    for (;;)
    {
        if (cancelled && cancelled())
            return failure(LoadErrorCode::Cancelled, LoadStage::Opening, "Provider copy cancelled");
        pollfd ready{source, POLLIN, 0};
        const int available = poll(&ready, 1, 50);
        if (available < 0 && errno == EINTR)
            continue;
        if (available < 0)
            return failure(LoadErrorCode::IoFailure, LoadStage::Opening, "Provider wait");
        if (!available)
            continue;
        const ssize_t count = read(source, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            return failure(LoadErrorCode::IoFailure, LoadStage::Opening, "Provider read");
        if (!count)
            break;
        if (static_cast<uint64_t>(count) > request.limits.maxInputBytes - bytes_copied)
            return failure(LoadErrorCode::ResourceLimit, LoadStage::Opening, "Provider input limit");
        size_t written = 0;
        while (written < static_cast<size_t>(count))
        {
            const ssize_t n = write(fd, buffer.data() + written,
                                    static_cast<size_t>(count) - written);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return failure(LoadErrorCode::IoFailure, LoadStage::Opening, "Provider cache write");
            written += static_cast<size_t>(n);
        }
        bytes_copied += static_cast<uint64_t>(count);
        if (progress)
            progress({LoadStage::Opening, bytes_copied, std::nullopt});
    }
    if (!bytes_copied)
        return failure(LoadErrorCode::InvalidHeader, LoadStage::Opening, "Empty provider stream");
    return {};
}

SceneHandle make_scene(const SceneHeader *header, std::shared_ptr<const void> storage)
{
    auto scene = std::make_shared<SplatScene>();
    scene->storage = std::move(storage);
    scene->count = header->count;
    scene->shDegree = header->shDegree;
    scene->sourceFormat = static_cast<SourceFormat>(header->sourceFormat);
    scene->worldOrigin = {header->origin[0], header->origin[1], header->origin[2]};
    scene->bounds = {{header->min[0], header->min[1], header->min[2]},
                     {header->max[0], header->max[1], header->max[2]}};
    scene->maxScale = header->maxScale;
    auto span = [header](ArrayIndex index) {
        const auto *values = reinterpret_cast<const float *>(
            reinterpret_cast<const uint8_t *>(header) + header->offsets[index]);
        return std::span<const float>(values, static_cast<size_t>(header->lengths[index]));
    };
    scene->centerLocal = span(Center);
    scene->scale = span(Scale);
    scene->rotation = span(Rotation);
    scene->opacity = span(Opacity);
    scene->rgb0 = span(Rgb0);
    scene->shRest = span(ShRest);
    return scene;
}
} // namespace

SharedScene::~SharedScene()
{
    if (fd_ >= 0)
        close(fd_);
}

SharedScene::SharedScene(SharedScene &&other) noexcept
    : scene_(std::move(other.scene_)), fd_(std::exchange(other.fd_, -1)) {}

SharedScene &SharedScene::operator=(SharedScene &&other) noexcept
{
    if (this != &other)
    {
        if (fd_ >= 0)
            close(fd_);
        scene_ = std::move(other.scene_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

int SharedScene::duplicate_fd() const
{
    return fd_ >= 0 ? duplicate(fd_) : -1;
}

LoadResult import_shared_fd(int fd, uint64_t max_scene_bytes,
                            const std::function<bool()> &cancelled)
{
    try
    {
    if (fd < 0)
        return failure(LoadErrorCode::IoFailure, LoadStage::Validating, "Invalid shared descriptor");
    Fd owned(duplicate(fd));
    if (owned.get() < 0)
        return failure(LoadErrorCode::IoFailure, LoadStage::Validating, "Shared descriptor status");
    const uint64_t bytes = ASharedMemory_getSize(owned.get());
    const uint64_t available = available_memory();
    const uint64_t live_limit = available > (64ull << 20)
        ? (available - (64ull << 20)) / 2 : 0;
    if (bytes < sizeof(SceneHeader) || bytes > max_scene_bytes || bytes > live_limit ||
        bytes > SIZE_MAX)
        return failure(LoadErrorCode::ResourceLimit, LoadStage::Validating, "Shared scene size");
    Mapping private_copy(mmap(nullptr, static_cast<size_t>(bytes), PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), static_cast<size_t>(bytes));
    if (private_copy.get() == MAP_FAILED)
        return failure(LoadErrorCode::OutOfMemory, LoadStage::Validating,
                       "Private scene allocation");
    if (auto issue = read_exact(owned.get(), private_copy.get(), static_cast<size_t>(bytes),
                                cancelled, LoadStage::Validating))
    {
        struct stat info{};
        if (issue->code != LoadErrorCode::IoFailure || fstat(owned.get(), &info) ||
            !S_ISCHR(info.st_mode))
            return *issue;
        Mapping legacy(mmap(nullptr, static_cast<size_t>(bytes), PROT_READ, MAP_SHARED,
                            owned.get(), 0), static_cast<size_t>(bytes));
        if (legacy.get() == MAP_FAILED)
            return failure(LoadErrorCode::IoFailure, LoadStage::Validating,
                           "Legacy shared scene mapping");
        for (size_t offset = 0; offset < static_cast<size_t>(bytes);)
        {
            if (cancelled && cancelled())
                return failure(LoadErrorCode::Cancelled, LoadStage::Validating,
                               "Shared scene copy cancelled");
            const size_t chunk = std::min<size_t>(static_cast<size_t>(bytes) - offset, 1 << 16);
            std::memcpy(static_cast<uint8_t *>(private_copy.get()) + offset,
                        static_cast<const uint8_t *>(legacy.get()) + offset, chunk);
            offset += chunk;
        }
    }
    const auto *header = static_cast<const SceneHeader *>(private_copy.get());
    if (!validate_scene(*header, bytes))
        return failure(LoadErrorCode::InvalidAttribute, LoadStage::Validating,
                       "Shared scene validation");
    if (mprotect(private_copy.get(), static_cast<size_t>(bytes), PROT_READ))
        return failure(LoadErrorCode::IoFailure, LoadStage::Validating,
                       "Private scene protection");
    auto owner = std::make_shared<Mapping>(std::move(private_copy));
    const auto *published = static_cast<const SceneHeader *>(owner->get());
    return make_scene(published, std::shared_ptr<const void>(owner, published));
    }
    catch (const std::bad_alloc &)
    {
        return failure(LoadErrorCode::OutOfMemory, LoadStage::Validating,
                       "Shared scene allocation");
    }
    catch (...)
    {
        return failure(LoadErrorCode::ObserverFailure, LoadStage::Validating,
                       "Shared scene observer failure");
    }
}

SharedLoadResult load_shared_fd(const FdLoadRequest &request,
                                 const std::function<bool()> &cancelled,
                                 ProgressSink progress)
{
    LoadStage stage = LoadStage::Opening;
    try
    {
        auto report = [&](LoadStage next, uint64_t bytes = 0,
                          std::optional<uint64_t> total = {}) {
            stage = next;
            if (progress)
                progress({next, bytes, total});
        };
        if (request.fd < 0 || request.ply_coordinates > Coordinates::Rub ||
            request.limits.maxSplats > UINT32_MAX)
            return failure(LoadErrorCode::InvalidHeader, stage, "Invalid load request");
        if (cancelled && cancelled())
            return failure(LoadErrorCode::Cancelled, stage, "Cancelled");
        Fd input(duplicate(request.fd));
        if (input.get() < 0)
            return failure(LoadErrorCode::IoFailure, stage, "Input descriptor duplication");
        struct stat info{};
        if (fstat(input.get(), &info))
            return failure(LoadErrorCode::IoFailure, stage, "Input descriptor status");
        if (S_ISREG(info.st_mode) && info.st_size <= 0)
            return failure(LoadErrorCode::TruncatedData, stage, "Empty regular file");
        const bool seekable = S_ISREG(info.st_mode);
        uint64_t input_bytes = 0;
        if (seekable)
        {
            input_bytes = static_cast<uint64_t>(info.st_size);
            if (input_bytes > request.limits.maxInputBytes)
                return failure(LoadErrorCode::ResourceLimit, stage, "Input limit");
        }
        else
        {
            Fd cache;
            if (auto issue = copy_provider(input.get(), request, cancelled, progress, cache,
                                           input_bytes))
                return *issue;
            input = std::move(cache);
        }
        report(LoadStage::Inspecting, 0, input_bytes);
        std::vector<uint8_t> prefix;
        if (auto issue = read_prefix(input.get(), input_bytes, prefix))
            return *issue;
        const auto probe = probe_file(prefix, input_bytes, request.limits);
        if (!probe.ok)
            return probe.failure;
        const auto layout = make_layout(probe.probe, request.limits.maxSceneBytes);
        if (!layout || layout->totalBytes > static_cast<uint64_t>(SIZE_MAX) ||
            layout->totalBytes > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
            return failure(LoadErrorCode::ResourceLimit, stage, "Scene layout limit");
        const uint64_t available = request.available_memory_bytes
                                       ? request.available_memory_bytes : available_memory();
        uint64_t peak = layout->totalBytes;
        uint64_t temporary = 4ull << 20;
        if (probe.probe.format == SourceFormat::Spz)
        {
            // Niantic decoding retains packed data and the complete float cloud.
            const uint64_t rest = 3ull * ((probe.probe.degree + 1) *
                                         (probe.probe.degree + 1) - 1);
            if (!checked_mul(probe.probe.count, 76 + 5 * rest, temporary) ||
                !checked_add(temporary, input_bytes, temporary))
                return failure(LoadErrorCode::ResourceLimit, stage, "SPZ peak byte overflow");
        }
        else
            temporary = std::max<uint64_t>(temporary, probe.ply.stride);
        if (!checked_add(peak, temporary, peak) ||
            !checked_add(peak, request.memory_reserve_bytes, peak))
            return failure(LoadErrorCode::ResourceLimit, stage, "CPU peak byte overflow");
        if (!available || peak > available)
            return error(LoadErrorCode::ResourceLimit, stage,
                         "CPU memory peak=" + std::to_string(peak) +
                         " available=" + std::to_string(available));
        Fd output(ASharedMemory_create("native3dgs-scene", static_cast<size_t>(layout->totalBytes)));
        if (output.get() < 0)
            return failure(LoadErrorCode::OutOfMemory, stage, "Shared scene allocation");
        Mapping writable(mmap(nullptr, static_cast<size_t>(layout->totalBytes),
                              PROT_READ | PROT_WRITE, MAP_SHARED, output.get(), 0),
                         static_cast<size_t>(layout->totalBytes));
        if (writable.get() == MAP_FAILED)
            return failure(LoadErrorCode::OutOfMemory, stage, "Shared scene mapping");
        auto *header = static_cast<SceneHeader *>(writable.get());
        std::memcpy(header, &*layout, sizeof(SceneHeader));
        report(LoadStage::Decoding, 0, input_bytes);
        std::optional<LoadError> issue;
        if (probe.probe.format == SourceFormat::Ply)
        {
            if (input_bytes > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
                return failure(LoadErrorCode::ResourceLimit, stage, "PLY input offset range");
            PositionalStream cursor{input.get(), 0, static_cast<off_t>(input_bytes)};
            FILE *stream = funopen(&cursor, positional_read, nullptr, positional_seek, nullptr);
            issue = stream ? decode_ply(stream, probe, request.ply_coordinates, header, cancelled,
                                        [&](uint64_t bytes) { report(LoadStage::Decoding, bytes, input_bytes); })
                           : std::optional<LoadError>(failure(LoadErrorCode::IoFailure, stage,
                                                              "PLY input stream"));
        }
        else
        {
            Mapping source(mmap(nullptr, static_cast<size_t>(input_bytes),
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0),
                           static_cast<size_t>(input_bytes));
            if (source.get() == MAP_FAILED)
                issue = failure(LoadErrorCode::OutOfMemory, stage, "SPZ input allocation");
            else if (auto copy_error = read_exact(input.get(), source.get(),
                                                  static_cast<size_t>(input_bytes), cancelled,
                                                  stage))
                issue = *copy_error;
            else
                issue = decode_spz({static_cast<const uint8_t *>(source.get()),
                                    static_cast<size_t>(input_bytes)}, probe, header, cancelled);
        }
        if (issue)
            return *issue;
        struct stat completed{};
        if (fstat(input.get(), &completed) || completed.st_size < 0 ||
            static_cast<uint64_t>(completed.st_size) != input_bytes)
            return failure(LoadErrorCode::IoFailure, stage, "Input length changed during decode");
        if (cancelled && cancelled())
            return failure(LoadErrorCode::Cancelled, stage, "Cancelled");
        report(LoadStage::Validating, input_bytes, input_bytes);
        if (!validate_scene(*header, layout->totalBytes))
            return failure(LoadErrorCode::InvalidAttribute, stage, "Shared scene validation");
        if (mprotect(writable.get(), static_cast<size_t>(layout->totalBytes), PROT_READ) ||
            ASharedMemory_setProt(output.get(), PROT_READ))
            return failure(LoadErrorCode::IoFailure, stage, "Read-only scene protection");
        auto owner = std::make_shared<Mapping>(std::move(writable));
        const auto *published = static_cast<const SceneHeader *>(owner->get());
        auto storage = std::shared_ptr<const void>(owner, published);
        report(LoadStage::Ready, input_bytes, input_bytes);
        if (cancelled && cancelled())
            return failure(LoadErrorCode::Cancelled, stage, "Cancelled before publication");
        auto scene = make_scene(published, std::move(storage));
        return SharedScene(std::move(scene), output.release());
    }
    catch (const std::bad_alloc &)
    {
        return failure(LoadErrorCode::OutOfMemory, stage, "Allocation failure");
    }
    catch (...)
    {
        return failure(LoadErrorCode::ObserverFailure, stage, "Observer or decoder failure");
    }
}

LoadResult load_fd(const FdLoadRequest &request, const std::function<bool()> &cancelled,
                   ProgressSink progress)
{
    auto result = load_shared_fd(request, cancelled, std::move(progress));
    if (auto scene = std::get_if<SharedScene>(&result))
        return scene->scene();
    return std::get<LoadError>(std::move(result));
}

} // namespace gs::android::io
