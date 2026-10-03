#include "gs_server/packet_cache.h"
#include "gs_server/windows_client.h"
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <winsock2.h>
using namespace gs::server;
TEST(Transport, PrivateHttpRequiresExplicitLiteralAddressConsent)
{
    EXPECT_NO_THROW(validate_endpoint({"http://127.0.0.1:8888"}));
    EXPECT_NO_THROW(validate_endpoint({"https://example.com:8888"}));
    EXPECT_THROW(validate_endpoint({"http://10.129.4.104:8046"}), std::exception);
    EXPECT_NO_THROW(validate_endpoint({"http://10.129.4.104:8046", "", true}));
    for (const auto *url :
         {"http://example.com", "http://8.8.8.8", "http://172.15.0.1", "http://10.0.0.1/frame",
          "http://user:pass@10.0.0.1", "http://10.0.0.1?x=1"})
        EXPECT_THROW(validate_endpoint({url, "", true}), std::exception);
    EXPECT_THROW(validate_endpoint({"https://example.com", "bad\r\nheader", false}),
                 std::exception);
    Endpoint session_endpoint{"https://example.com"};
    session_endpoint.session = "bad\r\nheader";
    EXPECT_THROW(validate_endpoint(session_endpoint), std::exception);
    session_endpoint.session.assign(48, 'a');
    session_endpoint.prefetch = true;
    EXPECT_NO_THROW(validate_endpoint(session_endpoint));
}
TEST(Transport, CompressedPacketCacheIsBoundedAndReusesOverlap)
{
    PacketCache cache(10);
    Download first, second, third;
    first.packet.assign(4, 1);
    second.packet.assign(4, 2);
    third.packet.assign(4, 3);
    cache.put("a", first);
    cache.put("b", second);
    EXPECT_EQ(cache.get("a")->packet, first.packet);
    cache.put("c", third);
    EXPECT_FALSE(cache.get("b"));
    EXPECT_LE(cache.bytes(), 10);
    EXPECT_TRUE(cache.get("a"));
    EXPECT_TRUE(cache.contains("a"));
    cache.erase("a");
    EXPECT_FALSE(cache.contains("a"));
    EXPECT_EQ(cache.bytes(), 4);
    cache.erase("missing");
    EXPECT_EQ(cache.bytes(), 4);
    cache.clear();
    EXPECT_EQ(cache.bytes(), 0);
    cache.protect({"near"});
    cache.put("near", first);
    cache.put("far1", second);
    cache.put("far2", third);
    EXPECT_TRUE(cache.get("near"));
    EXPECT_LE(cache.bytes(), 10);
}
TEST(Transport, CanceledDownloadNeverSendsRequest)
{
    Endpoint endpoint{"http://127.0.0.1:1"};
    endpoint.cancellation = std::make_shared<HttpCancellation>();
    endpoint.cancellation->cancel();
    endpoint.cancellation->cancel();
    try
    {
        request_http(endpoint, "/frame", "NGSREQ3 1 m-test 8 8 0 0 1 rgba 0");
        FAIL() << "Cancellation ignored";
    }
    catch (const std::exception &error)
    {
        EXPECT_STREQ(error.what(), "HTTP request canceled");
    }
}
TEST(Transport, CancelStalledAsyncHeadersReleasesRequestPromptly)
{
    WSADATA data{};
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &data), 0);
    struct SocketOwner
    {
        SOCKET listener = INVALID_SOCKET, connection = INVALID_SOCKET;
        void close()
        {
            if (connection != INVALID_SOCKET)
                closesocket(connection);
            if (listener != INVALID_SOCKET)
                closesocket(listener);
            connection = listener = INVALID_SOCKET;
        }
        ~SocketOwner()
        {
            close();
            WSACleanup();
        }
    } sockets;
    sockets.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sockets.listener, INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(sockets.listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    int size = sizeof(address);
    ASSERT_EQ(getsockname(sockets.listener, reinterpret_cast<sockaddr *>(&address), &size), 0);
    ASSERT_EQ(listen(sockets.listener, 1), 0);
    Endpoint endpoint{"http://127.0.0.1:" + std::to_string(ntohs(address.sin_port))};
    endpoint.cancellation = std::make_shared<HttpCancellation>();
    auto download = std::async(std::launch::async, [&] {
        try
        {
            request_http(endpoint, "/frame", "NGSREQ3 1 m-test 8 8 0 0 1 rgba 0");
            return std::string{"unexpected success"};
        }
        catch (const std::exception &error)
        {
            return std::string{error.what()};
        }
    });
    struct DownloadGuard
    {
        std::shared_ptr<HttpCancellation> cancellation;
        SocketOwner &sockets;
        ~DownloadGuard()
        {
            cancellation->cancel();
            sockets.close();
        }
    } guard{endpoint.cancellation, sockets};
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(sockets.listener, &ready);
    timeval timeout{3, 0};
    const auto accepted = select(0, &ready, nullptr, nullptr, &timeout);
    if (accepted != 1)
    {
        endpoint.cancellation->cancel();
        FAIL() << "Client did not connect: " << download.get();
    }
    sockets.connection = accept(sockets.listener, nullptr, nullptr);
    ASSERT_NE(sockets.connection, INVALID_SOCKET);
    DWORD receive_timeout = 3000;
    setsockopt(sockets.connection, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&receive_timeout), sizeof(receive_timeout));
    char request[2048]{};
    const auto received = recv(sockets.connection, request, sizeof(request), 0);
    endpoint.cancellation->cancel();
    EXPECT_GT(received, 0);
    const auto completion = download.wait_for(std::chrono::seconds(3));
    EXPECT_EQ(completion, std::future_status::ready);
    if (completion != std::future_status::ready)
        sockets.close();
    EXPECT_EQ(download.get(), "HTTP request canceled");
}
