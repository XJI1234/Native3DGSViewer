#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace gs::io::detail
{

class UniqueHandle
{
  public:
    explicit UniqueHandle(HANDLE handle = nullptr) : handle_(handle)
    {
    }
    ~UniqueHandle()
    {
        reset();
    }
    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;
    UniqueHandle(UniqueHandle &&other) noexcept : handle_(other.release())
    {
    }
    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    HANDLE get() const
    {
        return handle_;
    }
    bool valid() const
    {
        return handle_ && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release()
    {
        auto value = handle_;
        handle_ = nullptr;
        return value;
    }
    void reset(HANDLE value = nullptr)
    {
        if (valid())
            CloseHandle(handle_);
        handle_ = value;
    }

  private:
    HANDLE handle_;
};

class UniqueView
{
  public:
    explicit UniqueView(void *view = nullptr) : view_(view)
    {
    }
    ~UniqueView()
    {
        if (view_)
            UnmapViewOfFile(view_);
    }
    UniqueView(const UniqueView &) = delete;
    UniqueView &operator=(const UniqueView &) = delete;
    void *get() const
    {
        return view_;
    }
    void *release()
    {
        auto value = view_;
        view_ = nullptr;
        return value;
    }

  private:
    void *view_;
};

} // namespace gs::io::detail
