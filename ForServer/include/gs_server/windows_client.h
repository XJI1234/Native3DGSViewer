#pragma once
#include "gs_server/frame.h"
#include "gs_server/request.h"
#include <memory>
#include <stdexcept>

namespace gs::server
{
class HttpError : public std::runtime_error
{
  public:
    explicit HttpError(unsigned status)
        : std::runtime_error("Server HTTP " + std::to_string(status) +
                             (status == 401 ? " (check access token)" : "")),
          status_(status)
    {
    }
    unsigned status() const
    {
        return status_;
    }

  private:
    unsigned status_;
};
struct Download
{
    std::vector<uint8_t> packet;
    std::string server_stats = "{}";
    double milliseconds = 0;
    std::string cache_state, position_code;
};
class HttpCancellation
{
  public:
    HttpCancellation();
    ~HttpCancellation();
    void cancel();
    bool canceled() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct Endpoint
{
    std::string url, token;
    bool allow_private_http = false;
    std::string session;
    bool prefetch = false;
    std::shared_ptr<HttpCancellation> cancellation;
};
void validate_endpoint(const Endpoint &endpoint);
Download request_http(const Endpoint &endpoint, const std::string &route,
                      const std::string &body = "");
Download request_frame(const Endpoint &endpoint, const FrameRequest &request);
struct ClientRender
{
    std::vector<uint8_t> rgba;
    double upload_ms = 0, draw_ms = 0, readback_ms = 0;
    uint64_t allocated_bytes = 0;
};
class ThinRenderer
{
  public:
    ThinRenderer();
    ~ThinRenderer();
    ThinRenderer(const ThinRenderer &) = delete;
    ThinRenderer &operator=(const ThinRenderer &) = delete;
    ClientRender render(const Frame &frame);
    void show(const Frame &frame);
    std::string adapter() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gs::server
