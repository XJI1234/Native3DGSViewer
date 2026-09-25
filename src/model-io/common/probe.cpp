#include "probe.h"

#include <zlib.h>

#include <array>
#include <charconv>
#include <cstring>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace gs::io::detail
{
namespace
{

uint32_t le32(const uint8_t *p)
{
    uint32_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

uint64_t le64(const uint8_t *p)
{
    uint64_t value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

bool parse_number(std::string_view text, uint64_t &value)
{
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && end == text.data() + text.size();
}

uint32_t type_width(std::string_view type)
{
    if (type == "char" || type == "uchar" || type == "int8" || type == "uint8")
        return 1;
    if (type == "short" || type == "ushort" || type == "int16" || type == "uint16")
        return 2;
    if (type == "int" || type == "uint" || type == "float" || type == "int32" || type == "uint32" ||
        type == "float32")
        return 4;
    if (type == "double" || type == "float64")
        return 8;
    return 0;
}

ProbeResult fail(LoadErrorCode code, std::string reason)
{
    ProbeResult result;
    result.failure = error(code, LoadStage::Inspecting, std::move(reason));
    return result;
}

ProbeResult probe_ply(std::span<const uint8_t> bytes, uint64_t fileBytes, const LoadLimits &limits)
{
    const size_t headerLimit = std::min<size_t>(bytes.size(), 65536);
    const std::string_view text(reinterpret_cast<const char *>(bytes.data()), headerLimit);
    auto end = text.find("end_header\n");
    size_t endingSize = sizeof("end_header\n") - 1;
    if (end == std::string_view::npos)
    {
        end = text.find("end_header\r\n");
        endingSize = sizeof("end_header\r\n") - 1;
    }
    if (end == std::string_view::npos)
        return fail(LoadErrorCode::InvalidHeader, "PLY header exceeds 64 KiB or is incomplete");
    const uint64_t dataStart = end + endingSize;
    std::istringstream lines(std::string(text.substr(0, dataStart)));
    std::string line, current;
    std::vector<std::tuple<std::string, uint64_t, uint64_t, bool>> elements;
    std::vector<PlyProperty> props;
    std::unordered_set<std::string> names;
    uint64_t count = 0, stride = 0;
    bool hasFormat = false, vertexFound = false;
    if (!std::getline(lines, line))
        return fail(LoadErrorCode::InvalidHeader, "PLY magic");
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line != "ply")
        return fail(LoadErrorCode::InvalidHeader, "PLY magic");
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream fields(line);
        std::string kind, a, b, c;
        fields >> kind >> a >> b >> c;
        if (kind == "format")
        {
            if (hasFormat)
                return fail(LoadErrorCode::InvalidHeader, "Duplicate format");
            if (a != "binary_little_endian" || b != "1.0")
                return fail(LoadErrorCode::UnsupportedFormat, "PLY encoding");
            hasFormat = true;
        }
        else if (kind == "element")
        {
            uint64_t n;
            if (!parse_number(b, n))
                return fail(LoadErrorCode::InvalidHeader, "Element count");
            current = a;
            elements.emplace_back(a, n, 0, false);
            if (a == "vertex")
            {
                if (vertexFound)
                    return fail(LoadErrorCode::InvalidHeader, "Duplicate vertex element");
                vertexFound = true;
                count = n;
            }
        }
        else if (kind == "property")
        {
            if (elements.empty())
                return fail(LoadErrorCode::InvalidHeader, "Property without element");
            const bool list = a == "list";
            auto &elem = elements.back();
            std::get<3>(elem) |= list;
            if (current == "vertex")
            {
                if (list)
                    return fail(LoadErrorCode::InvalidHeader, "Vertex list property");
                const uint32_t width = type_width(a);
                if (!width || b.empty() || !names.insert(b).second)
                    return fail(LoadErrorCode::InvalidHeader,
                                "Invalid or duplicate vertex property");
                props.push_back(
                    {b, static_cast<uint32_t>(stride), width, a == "float" || a == "float32"});
                if (!checked_add(stride, width, stride) || stride > UINT32_MAX)
                    return fail(LoadErrorCode::ResourceLimit, "Vertex stride");
            }
            else if (!list)
            {
                const uint32_t width = type_width(a);
                uint64_t newStride;
                if (!width || !checked_add(std::get<2>(elem), width, newStride))
                    return fail(LoadErrorCode::InvalidHeader, "Other element property");
                std::get<2>(elem) = newStride;
            }
        }
        else if (kind == "end_header")
            break;
        else if (kind != "comment" && kind != "obj_info" && !kind.empty())
            return fail(LoadErrorCode::InvalidHeader, "Unknown PLY header line");
    }
    if (!hasFormat || !vertexFound)
        return fail(LoadErrorCode::InvalidHeader, "Missing format or vertex");
    if (!count)
        return fail(LoadErrorCode::EmptyScene, "Zero vertices");
    if (count > limits.maxSplats)
        return fail(LoadErrorCode::ResourceLimit, "Vertex limit");
    constexpr std::array<std::string_view, 14> required{
        "x",       "y",       "z",       "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
        "scale_0", "scale_1", "scale_2", "rot_0",  "rot_1",  "rot_2",  "rot_3"};
    for (auto name : required)
    {
        const auto it = std::find_if(props.begin(), props.end(),
                                     [name](const auto &p) { return p.name == name; });
        if (it == props.end() || !it->isFloat)
            return fail(LoadErrorCode::InvalidHeader, "Missing or non-float32 3DGS attribute");
    }
    uint32_t restCount = 0;
    for (const auto &p : props)
    {
        if (p.name.starts_with("f_rest_"))
        {
            uint64_t index;
            if (!parse_number(std::string_view(p.name).substr(7), index) || index > 44 ||
                !p.isFloat)
                return fail(LoadErrorCode::InvalidHeader, "Invalid SH property");
            ++restCount;
        }
    }
    if (restCount != 0 && restCount != 9 && restCount != 24 && restCount != 45)
        return fail(LoadErrorCode::InvalidHeader, "Partial SH properties");
    for (uint32_t i = 0; i < restCount; ++i)
    {
        if (!names.contains("f_rest_" + std::to_string(i)))
            return fail(LoadErrorCode::InvalidHeader, "SH index gap");
    }
    uint64_t vertexOffset = dataStart;
    for (const auto &[name, n, rowBytes, hasList] : elements)
    {
        if (name == "vertex")
            break;
        uint64_t size;
        if (hasList || !checked_mul(n, rowBytes, size) ||
            !checked_add(vertexOffset, size, vertexOffset))
            return fail(LoadErrorCode::UnsupportedFeature, "Variable-size element before vertex");
    }
    uint64_t vertexBytes, vertexEnd;
    if (!checked_mul(count, stride, vertexBytes) ||
        !checked_add(vertexOffset, vertexBytes, vertexEnd))
        return fail(LoadErrorCode::ResourceLimit, "PLY body size overflow");
    if (vertexEnd > fileBytes)
        return fail(LoadErrorCode::TruncatedData, "PLY vertex body truncated");
    ProbeResult result;
    result.ok = true;
    result.probe = {SourceFormat::Ply, count,
                    static_cast<uint8_t>(restCount == 0    ? 0
                                         : restCount == 9  ? 1
                                         : restCount == 24 ? 2
                                                           : 3),
                    fileBytes};
    result.ply = {vertexOffset, stride, std::move(props)};
    return result;
}

ProbeResult probe_spz(std::span<const uint8_t> bytes, uint64_t fileBytes, const LoadLimits &limits)
{
    std::array<uint8_t, 32> header{};
    bool legacy = bytes.size() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b;
    if (legacy)
    {
        z_stream stream{};
        stream.next_in = const_cast<Bytef *>(bytes.data());
        stream.avail_in = static_cast<uInt>(std::min<size_t>(bytes.size(), 1 << 20));
        stream.next_out = header.data();
        stream.avail_out = 16;
        if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
            return fail(LoadErrorCode::InvalidHeader, "gzip init");
        const int status = inflate(&stream, Z_NO_FLUSH);
        inflateEnd(&stream);
        if (stream.total_out < 16)
            return fail(status == Z_DATA_ERROR ? LoadErrorCode::InvalidHeader
                                               : LoadErrorCode::TruncatedData,
                        "SPZ legacy header");
    }
    else
    {
        if (bytes.size() < 32)
            return fail(LoadErrorCode::TruncatedData, "SPZ v4 header");
        std::memcpy(header.data(), bytes.data(), 32);
    }
    if (le32(header.data()) != 0x5053474e)
        return fail(LoadErrorCode::UnsupportedFormat, "SPZ magic");
    const uint32_t version = le32(header.data() + 4);
    if (version < 1 || version > 4 || (version == 4) == legacy)
        return fail(LoadErrorCode::UnsupportedVersion, "SPZ version");
    const uint64_t count = le32(header.data() + 8);
    if (!count)
        return fail(LoadErrorCode::EmptyScene, "Zero SPZ points");
    if (count > limits.maxSplats)
        return fail(LoadErrorCode::ResourceLimit, "SPZ point limit");
    const uint8_t degree = header[12], flags = header[14];
    if (degree > 3 || flags)
        return fail(LoadErrorCode::UnsupportedFeature, "SPZ SH or flags");
    if (header[13] > 30)
        return fail(LoadErrorCode::InvalidHeader, "SPZ fractional bits");
    if (!legacy)
    {
        const uint8_t streams = header[15];
        const uint64_t toc = le32(header.data() + 16);
        const uint8_t expected = degree ? 6 : 5;
        uint64_t tocEnd;
        if (streams != expected || toc != 32 || !checked_add(toc, uint64_t(streams) * 16, tocEnd) ||
            tocEnd > fileBytes || tocEnd > bytes.size())
            return fail(LoadErrorCode::InvalidHeader, "SPZ table of contents");
        const uint64_t sh = 3ull * ((degree + 1) * (degree + 1) - 1);
        const std::array<uint64_t, 6> widths{9, 1, 3, 3, 4, sh};
        uint64_t compressed = tocEnd;
        for (uint8_t i = 0; i < streams; ++i)
        {
            uint64_t raw;
            if (!checked_mul(count, widths[i], raw) ||
                le64(bytes.data() + toc + 16 * i + 8) != raw ||
                !checked_add(compressed, le64(bytes.data() + toc + 16 * i), compressed) ||
                compressed > fileBytes)
                return fail(LoadErrorCode::InvalidHeader, "SPZ stream size");
        }
        if (compressed != fileBytes)
            return fail(LoadErrorCode::InvalidHeader, "SPZ trailing data");
    }
    ProbeResult result;
    result.ok = true;
    result.probe = {SourceFormat::Spz, count, degree, fileBytes};
    return result;
}

} // namespace

ProbeResult probe_file(std::span<const uint8_t> bytes, uint64_t fileBytes, const LoadLimits &limits)
{
    if (!fileBytes)
        return fail(LoadErrorCode::InvalidHeader, "Empty file");
    if (fileBytes > limits.maxInputBytes)
        return fail(LoadErrorCode::ResourceLimit, "Input limit");
    if (bytes.size() >= 3 && std::memcmp(bytes.data(), "ply", 3) == 0)
        return probe_ply(bytes, fileBytes, limits);
    if (bytes.size() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b)
        return probe_spz(bytes, fileBytes, limits);
    if (bytes.size() >= 4 && le32(bytes.data()) == 0x5053474e)
        return probe_spz(bytes, fileBytes, limits);
    return fail(LoadErrorCode::UnsupportedFormat, "Unknown signature");
}

} // namespace gs::io::detail
