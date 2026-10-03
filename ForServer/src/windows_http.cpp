#include "gs_server/windows_client.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <winhttp.h>

namespace gs::server
{
struct HttpCancellation::Impl
{
    std::atomic<bool> canceled = false;
};
HttpCancellation::HttpCancellation() : impl_(std::make_unique<Impl>())
{
}
HttpCancellation::~HttpCancellation()
{
    cancel();
}
void HttpCancellation::cancel()
{
    impl_->canceled = true;
}
bool HttpCancellation::canceled() const
{
    return impl_->canceled.load();
}
namespace
{
std::wstring wide(const std::string &value)
{
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()),
                                   nullptr, 0);
    if (!size)
        throw std::runtime_error("Invalid UTF-8 URL");
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()),
                        result.data(), size);
    return result;
}
void checked(bool success, const char *stage)
{
    if (!success)
        throw std::runtime_error(std::string(stage) + ": " + std::to_string(GetLastError()));
}
class Internet
{
  public:
    explicit Internet(HINTERNET handle) : handle_(handle)
    {
        checked(handle != nullptr, "WinHTTP handle");
    }
    ~Internet()
    {
        WinHttpCloseHandle(handle_);
    }
    Internet(const Internet &) = delete;
    Internet &operator=(const Internet &) = delete;
    HINTERNET get() const
    {
        return handle_;
    }

  private:
    HINTERNET handle_;
};
class HttpRequest
{
  public:
    HttpRequest(HINTERNET handle, bool asynchronous, std::shared_ptr<HttpCancellation> cancellation,
                std::chrono::steady_clock::time_point deadline)
        : handle_(handle), asynchronous_(asynchronous), cancellation_(std::move(cancellation)),
          deadline_(deadline)
    {
        checked(handle_ != nullptr, "HTTP request handle");
        if (!asynchronous_)
            return;
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        if (!WinHttpSetOption(handle_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
            WinHttpSetStatusCallback(handle_, callback,
                                     WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                         WINHTTP_CALLBACK_FLAG_HANDLES,
                                     0) == WINHTTP_INVALID_STATUS_CALLBACK)
        {
            const auto error = GetLastError();
            WinHttpCloseHandle(handle_);
            SetLastError(error);
            checked(false, "Async HTTP callback");
        }
    }
    ~HttpRequest()
    {
        WinHttpCloseHandle(handle_);
        if (asynchronous_)
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return closing_; });
        }
    }
    HttpRequest(const HttpRequest &) = delete;
    HttpRequest &operator=(const HttpRequest &) = delete;
    HINTERNET get() const
    {
        return handle_;
    }
    void send(const std::wstring &headers, const std::string &body)
    {
        perform(
            [&] {
                return WinHttpSendRequest(handle_, headers.c_str(), DWORD(-1),
                                          body.empty() ? nullptr : const_cast<char *>(body.data()),
                                          DWORD(body.size()), DWORD(body.size()),
                                          asynchronous_ ? reinterpret_cast<DWORD_PTR>(this) : 0);
            },
            "Send request");
    }
    void receive()
    {
        perform([&] { return WinHttpReceiveResponse(handle_, nullptr); }, "Receive response");
    }
    DWORD read(std::span<uint8_t> buffer)
    {
        DWORD received = 0;
        perform(
            [&] {
                return WinHttpReadData(handle_, buffer.data(), DWORD(buffer.size()),
                                       asynchronous_ ? nullptr : &received);
            },
            "Read response");
        return asynchronous_ ? transferred_ : received;
    }

  private:
    static void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void *information,
                                  DWORD length)
    {
        if (!context)
            return;
        auto *request = reinterpret_cast<HttpRequest *>(context);
        std::lock_guard lock(request->mutex_);
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
            request->closing_ = true;
        else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        {
            request->error_ = static_cast<WINHTTP_ASYNC_RESULT *>(information)->dwError;
            request->completed_ = true;
        }
        else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                 status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                 status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
        {
            request->transferred_ = length;
            request->completed_ = true;
        }
        request->condition_.notify_all();
    }
    void perform(const std::function<BOOL()> &operation, const char *stage)
    {
        if (cancellation_ && cancellation_->canceled())
            throw std::runtime_error("HTTP request canceled");
        {
            std::lock_guard lock(mutex_);
            completed_ = false;
            error_ = 0;
        }
        const auto accepted = operation();
        const auto error = accepted ? ERROR_SUCCESS : GetLastError();
        if (!asynchronous_ || (!accepted && error != ERROR_IO_PENDING))
        {
            SetLastError(error);
            checked(accepted, stage);
            return;
        }
        std::unique_lock lock(mutex_);
        while (!completed_)
        {
            if (cancellation_ && cancellation_->canceled())
                throw std::runtime_error("HTTP request canceled");
            if (std::chrono::steady_clock::now() >= deadline_)
                throw std::runtime_error("HTTP total deadline");
            condition_.wait_for(lock, std::chrono::milliseconds(25));
        }
        if (error_)
        {
            SetLastError(error_);
            checked(false, stage);
        }
    }
    HINTERNET handle_;
    bool asynchronous_, completed_ = false, closing_ = false;
    DWORD error_ = 0, transferred_ = 0;
    std::shared_ptr<HttpCancellation> cancellation_;
    std::chrono::steady_clock::time_point deadline_;
    std::mutex mutex_;
    std::condition_variable condition_;
};
bool private_host(const std::wstring &host)
{
    std::wistringstream input(host);
    std::array<unsigned, 4> octets{};
    for (size_t index = 0; index < octets.size(); ++index)
    {
        if (!(input >> octets[index]) || octets[index] > 255)
            return false;
        if (index != 3)
        {
            wchar_t separator = 0;
            if (!(input >> separator) || separator != L'.')
                return false;
        }
    }
    wchar_t trailing;
    if (input >> trailing)
        return false;
    return octets[0] == 10 || (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
           (octets[0] == 192 && octets[1] == 168);
}
std::string ascii_header(HINTERNET request, const wchar_t *name)
{
    std::array<wchar_t, 8192> buffer{};
    DWORD bytes = DWORD(buffer.size() * sizeof(wchar_t));
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, buffer.data(), &bytes, nullptr))
        return {};
    std::string result;
    for (size_t index = 0; buffer[index]; ++index)
    {
        if (buffer[index] > 127 || buffer[index] < 32)
            throw std::runtime_error("Non-ASCII response header");
        result.push_back(char(buffer[index]));
    }
    return result;
}
} // namespace
void validate_endpoint(const Endpoint &endpoint)
{
    if ((!endpoint.session.empty() &&
         (endpoint.session.size() != 48 ||
          endpoint.session.find_first_not_of("0123456789abcdef") != std::string::npos)) ||
        (endpoint.prefetch && endpoint.session.empty()))
        throw std::runtime_error("Invalid session identity");
    if (endpoint.url.size() > 2048 || endpoint.token.size() > 256 ||
        endpoint.token.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
            std::string::npos)
        throw std::runtime_error("Invalid endpoint or token");
    const auto address = wide(endpoint.url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength =
        components.dwUserNameLength = components.dwPasswordLength = DWORD(-1);
    checked(WinHttpCrackUrl(address.c_str(), DWORD(address.size()), 0, &components), "Parse URL");
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    const std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (host.empty() || (path != L"" && path != L"/") || components.dwExtraInfoLength ||
        components.dwUserNameLength || components.dwPasswordLength)
        throw std::runtime_error("Endpoint must be a root URL without credentials/query");
    if (components.nScheme == INTERNET_SCHEME_HTTPS)
        return;
    const bool loopback =
        host == L"127.0.0.1" || host == L"localhost" || host == L"::1" || host == L"[::1]";
    if (components.nScheme != INTERNET_SCHEME_HTTP ||
        (!loopback && !(endpoint.allow_private_http && private_host(host))))
        throw std::runtime_error(
            "HTTPS required; private HTTP requires explicit consent and a literal RFC1918 address");
}
Download request_http(const Endpoint &endpoint, const std::string &route, const std::string &body)
{
    validate_endpoint(endpoint);
    if ((route != "/health" && route != "/models" && route != "/status" && route != "/frame" &&
         route != "/sessions" && route != "/sessions/heartbeat" && route != "/sessions/close") ||
        body.size() > 1024)
        throw std::runtime_error("Invalid route/request budget");
    const auto start = std::chrono::steady_clock::now();
    const auto address = wide(endpoint.url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = DWORD(-1);
    checked(WinHttpCrackUrl(address.c_str(), DWORD(address.size()), 0, &components),
            "Parse endpoint");
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    const bool image_request = route == "/frame";
    if (image_request && endpoint.cancellation && endpoint.cancellation->canceled())
        throw std::runtime_error("HTTP request canceled");
    Internet session(WinHttpOpen(L"Native3DGS-Image/2", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr,
                                 nullptr, image_request ? WINHTTP_FLAG_ASYNC : 0));
    const int total_deadline = image_request ? 200000 : 15000;
    checked(WinHttpSetTimeouts(session.get(), image_request ? 5000 : 3000,
                               image_request ? 5000 : 3000, image_request ? 10000 : 3000,
                               image_request ? 180000 : 5000),
            "HTTP deadlines");
    Internet connection(WinHttpConnect(session.get(), host.c_str(), components.nPort, 0));
    const auto path = wide(route);
    std::array<uint8_t, 65536> buffer{};
    HttpRequest request(WinHttpOpenRequest(connection.get(), body.empty() ? L"GET" : L"POST",
                                           path.c_str(), nullptr, nullptr, nullptr,
                                           secure ? WINHTTP_FLAG_SECURE : 0),
                        image_request, image_request ? endpoint.cancellation : nullptr,
                        start + std::chrono::milliseconds(total_deadline));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    checked(WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects,
                             sizeof(redirects)),
            "Disable redirects");
    auto headers =
        L"Content-Type: text/plain\r\n" +
        (endpoint.token.empty() ? std::wstring{}
                                : L"Authorization: Bearer " + wide(endpoint.token) + L"\r\n");
    if (!endpoint.session.empty())
        headers += L"X-GS-Session: " + wide(endpoint.session) + L"\r\n";
    if (endpoint.prefetch)
        headers += L"X-GS-Prefetch: 1\r\n";
    request.send(headers, body);
    request.receive();
    DWORD status = 0, bytes = sizeof(status);
    checked(WinHttpQueryHeaders(request.get(),
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                                &status, &bytes, nullptr),
            "HTTP status");
    if (status != 200)
        throw HttpError(status);
    if (route == "/frame")
    {
        const auto identity = parse_request(body);
        if (ascii_header(request.get(), L"X-GS-Request-Id") !=
                std::to_string(identity.request_id) ||
            ascii_header(request.get(), L"X-GS-Model-Id") != identity.model_id)
            throw std::runtime_error("Response request/model identity mismatch");
    }
    Download output;
    output.cache_state = ascii_header(request.get(), L"X-GS-Cache");
    output.position_code = ascii_header(request.get(), L"X-GS-View-Code");
    const auto stats = ascii_header(request.get(), L"X-GS-Stats");
    if (!stats.empty())
        output.server_stats = stats;
    const size_t limit = route == "/frame" ? (64ull << 20) + 64 : 4ull << 20;
    for (;;)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        if (elapsed >= total_deadline)
            throw std::runtime_error("HTTP total deadline");
        checked(WinHttpSetTimeouts(request.get(), 3000, 3000, 3000, int(total_deadline - elapsed)),
                "Read deadline");
        const auto received = request.read(buffer);
        if (!received)
            break;
        if (output.packet.size() + received > limit)
            throw std::runtime_error("Response exceeds budget");
        output.packet.insert(output.packet.end(), buffer.begin(), buffer.begin() + received);
    }
    output.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return output;
}
Download request_frame(const Endpoint &endpoint, const FrameRequest &request)
{
    auto output = request_http(endpoint, "/frame", format_request(request));
    const auto info = inspect_frame(output.packet);
    if (output.position_code != view_code(request))
        throw std::runtime_error("Response spherical position mismatch");
    if (info.width != request.width || info.height != request.height ||
        info.profile != request.profile)
        throw std::runtime_error("Response dimensions/profile mismatch");
    return output;
}
} // namespace gs::server
