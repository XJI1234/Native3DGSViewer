#pragma once

#include <cstdint>

namespace gs::android::render
{
constexpr uint64_t timestamp_delta(uint64_t begin, uint64_t end, uint32_t valid_bits)
{
    if (!valid_bits) return 0;
    const uint64_t mask = valid_bits >= 64 ? UINT64_MAX : (uint64_t{1} << valid_bits) - 1;
    return (end - begin) & mask;
}
} // namespace gs::android::render
