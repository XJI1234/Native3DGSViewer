#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace viewer::log
{
struct Field
{
    const char *name;
    std::string value;
};

bool initialize() noexcept;
void close() noexcept;
void write(std::string_view level, std::string_view event,
           std::initializer_list<Field> fields = {}) noexcept;
void record_host_diagnostics() noexcept;
std::string utf8(std::wstring_view value);
std::string hresult(long value);
} // namespace viewer::log
