#include <utility>

#include "gs_server/renderer.h"
#include "gs_server/request.h"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace
{
using namespace gs::server;
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t stopping = 0;
void stop_handler(int)
{
    stopping = 1;
}
double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
template <class Operation>
boost::system::error_code transfer_with_deadline(boost::asio::io_context &context,
                                                 boost::asio::ip::tcp::socket &socket,
                                                 Operation operation)
{
    boost::asio::steady_timer deadline(context);
    deadline.expires_after(std::chrono::seconds(10));
    boost::system::error_code failure;
    bool expired = false;
    deadline.async_wait([&](boost::system::error_code error) {
        if (!error)
        {
            expired = true;
            boost::system::error_code ignored;
            socket.cancel(ignored);
        }
    });
    try
    {
        operation([&](boost::system::error_code error, size_t) {
            failure = error;
            deadline.cancel();
        });
    }
    catch (...)
    {
        deadline.cancel();
        context.restart();
        context.run();
        throw;
    }
    context.restart();
    context.run();
    return expired ? boost::asio::error::timed_out : failure;
}
template <class Body>
boost::system::error_code write_response(boost::asio::io_context &context,
                                         boost::asio::ip::tcp::socket &socket,
                                         boost::beast::http::response<Body> &response)
{
    return transfer_with_deadline(context, socket, [&](auto completion) {
        boost::beast::http::async_write(socket, response, completion);
    });
}
std::string stats_json(const RenderStats &stats, size_t bytes, double encode_ms, double total_ms)
{
    std::ostringstream output;
    output << std::setprecision(10) << "{\"source_count\":" << stats.source_count
           << ",\"visible_count\":" << stats.visible_count
           << ",\"tile_references\":" << stats.tile_references
           << ",\"resident_bytes\":" << stats.resident_bytes
           << ",\"peak_gpu_bytes\":" << stats.peak_gpu_bytes
           << ",\"project_ms\":" << stats.project_ms << ",\"sort_ms\":" << stats.sort_ms
           << ",\"gather_ms\":" << stats.gather_ms << ",\"draw_ms\":" << stats.draw_ms
           << ",\"readback_ms\":" << stats.readback_ms << ",\"encode_ms\":" << encode_ms
           << ",\"server_ms\":" << total_ms << ",\"packet_bytes\":" << bytes << '}';
    return output.str();
}
uint32_t number(const std::string &value, uint32_t maximum)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid unsigned integer");
    const auto result = std::stoull(value);
    if (result > maximum)
        throw std::runtime_error("Integer exceeds limit");
    return uint32_t(result);
}
using Catalog = std::map<std::string, std::string>;
Catalog read_catalog(const std::string &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot read catalog");
    Catalog catalog;
    std::string line;
    while (std::getline(input, line))
    {
        const auto separator = line.find('\t');
        if (separator == std::string::npos || line.size() > 8192 || catalog.size() >= 10000)
            throw std::runtime_error("Invalid catalog record");
        auto id = line.substr(0, separator);
        validate_request(FrameRequest{1, id});
        if (!catalog.emplace(id, line.substr(separator + 1)).second)
            throw std::runtime_error("Duplicate model id");
    }
    if (catalog.empty())
        throw std::runtime_error("Catalog is empty");
    return catalog;
}
void serve(const Catalog &catalog, int device, uint16_t port, const std::string &pinned_model)
{
    namespace asio = boost::asio;
    namespace http = boost::beast::http;
    using Tcp = asio::ip::tcp;
    asio::io_context context;
    Tcp::acceptor acceptor(context, {asio::ip::make_address("127.0.0.1"), port});
    acceptor.non_blocking(true);
    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);
    std::unique_ptr<CudaRenderer> renderer;
    std::string current_model;
    std::cerr << "Ready worker pid=" << getpid() << " device=" << device << " port=" << port
              << '\n';
    while (!stopping)
    {
        Tcp::socket socket(context);
        boost::system::error_code status;
        acceptor.accept(socket, status);
        if (status == asio::error::would_block || status == asio::error::try_again)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (status)
            throw std::runtime_error(status.message());
        try
        {
            boost::beast::flat_buffer buffer;
            http::request_parser<http::string_body> parser;
            parser.body_limit(1024);
            parser.header_limit(8192);
            const auto error = transfer_with_deadline(context, socket, [&](auto completion) {
                http::async_read(socket, buffer, parser, completion);
            });
            if (error)
                throw boost::system::system_error(error);
            const auto request = parser.release();
            if (request.method() == http::verb::get && request.target() == "/health")
            {
                http::response<http::string_body> response{http::status::ok, 11};
                response.set("X-GS-Worker-Pid", std::to_string(getpid()));
                response.body() = "ready";
                response.prepare_payload();
                write_response(context, socket, response);
                continue;
            }
            if (request.method() != http::verb::post || request.target() != "/frame")
                throw std::runtime_error("Expected POST /frame");
            const auto parsed = parse_request(request.body());
            if (!pinned_model.empty() && parsed.model_id != pinned_model)
                throw std::runtime_error("Instance model mismatch");
            if (!catalog.contains(parsed.model_id))
                throw std::runtime_error("Unknown model id");
            const auto start = Clock::now();
            double load_ms = 0;
            if (current_model != parsed.model_id)
            {
                renderer.reset();
                current_model.clear();
                const auto load_start = Clock::now();
                renderer =
                    std::make_unique<CudaRenderer>(load_scene(catalog.at(parsed.model_id)), device);
                current_model = parsed.model_id;
                load_ms = elapsed(load_start);
            }
            const auto render_start = Clock::now();
            const auto output =
                renderer->render({parsed.width, parsed.height, parsed.profile, parsed.yaw,
                                  parsed.pitch, parsed.zoom, false, parsed.flip_y});
            const auto encode_start = Clock::now();
            auto packet = encode_frame(output.frame);
            const auto encode_ms = elapsed(encode_start);
            auto stats = stats_json(output.stats, packet.size(), encode_ms, elapsed(render_start));
            stats.pop_back();
            stats += ",\"load_ms\":" + std::to_string(load_ms) +
                     ",\"worker_total_ms\":" + std::to_string(elapsed(start)) + "}";
            http::response<http::vector_body<uint8_t>> response{http::status::ok, 11};
            response.set(http::field::content_type, "application/x-native3dgs-frame");
            response.set("X-GS-Request-Id", std::to_string(parsed.request_id));
            response.set("X-GS-Model-Id", parsed.model_id);
            response.set("X-GS-Stats", stats);
            response.set(http::field::cache_control, "no-store");
            response.body() = std::move(packet);
            response.prepare_payload();
            if (write_response(context, socket, response))
                throw std::runtime_error("Frame write deadline");
            std::cout << stats << std::endl;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Worker error: " << error.what() << '\n';
            http::response<http::string_body> response{http::status::bad_request, 11};
            response.body() = "Invalid request or model render failed; see worker log";
            response.prepare_payload();
            write_response(context, socket, response);
        }
    }
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--help")
        {
            std::cout << "gs-server devices\ngs-server self-test --device 0\n"
                         "gs-server serve --catalog catalog.tsv --device 0 --port 19000\n"
                         "gs-server render --model file.spz --out frame.ngsf --width 1280 --height "
                         "720 --profile rgba --yaw 0 --pitch 14.036243 --zoom 1 --device 0 "
                         "--repeat 1 --reference image.ppm\n"
                         "Image profiles: rgba jpeg85..jpeg95. Workers bind loopback only.\n";
            return 0;
        }
        if (argc < 2)
            throw std::runtime_error("Command required; see --help");
        const std::string command = argv[1];
        const std::map<std::string, std::vector<std::string>> allowed{
            {"devices", {}},
            {"self-test", {"--device"}},
            {"serve", {"--catalog", "--device", "--port", "--instance-model"}},
            {"render",
             {"--model", "--out", "--width", "--height", "--profile", "--yaw", "--pitch", "--zoom",
              "--device", "--repeat", "--reference", "--flip-y"}}};
        if (!allowed.contains(command))
            throw std::runtime_error("Unknown command");
        std::map<std::string, std::string> options;
        for (int index = 2; index < argc; index += 2)
        {
            const std::string name = argv[index];
            if (index + 1 >= argc ||
                std::find(allowed.at(command).begin(), allowed.at(command).end(), name) ==
                    allowed.at(command).end() ||
                !options.emplace(name, argv[index + 1]).second)
                throw std::runtime_error("Unknown/duplicate/missing option: " + name);
        }
        const auto value = [&](const std::string &name, const std::string &fallback) {
            return options.contains(name) ? options.at(name) : fallback;
        };
        const int device = int(number(value("--device", "0"), 255));
        if (command == "devices")
        {
            std::cout << CudaRenderer::devices();
            return 0;
        }
        CudaRenderer::self_test(device);
        if (command == "self-test")
        {
            std::cout << "Stable GPU sort passed\n";
            return 0;
        }
        if (command == "serve")
        {
            const auto port = number(value("--port", "19000"), 65535);
            if (!port)
                throw std::runtime_error("Port must be nonzero");
            const auto catalog = read_catalog(value("--catalog", ""));
            const auto pinned_model = value("--instance-model", "");
            if (!pinned_model.empty() && !catalog.contains(pinned_model))
                throw std::runtime_error("Unknown pinned model");
            serve(catalog, device, uint16_t(port), pinned_model);
            return 0;
        }
        const auto request = parse_request(
            "NGSREQ3 1 cli " + value("--width", "640") + " " + value("--height", "360") + " " +
            value("--yaw", "0") + " " + value("--pitch", "14.0362434679") + " " +
            value("--zoom", "1") + " " + value("--profile", "rgba") + " " + value("--flip-y", "0"));
        const auto repeats = number(value("--repeat", "1"), 1000);
        if (!repeats)
            throw std::runtime_error("Repeat must be positive");
        const auto load_start = Clock::now();
        CudaRenderer renderer(load_scene(value("--model", "")), device);
        std::cerr << "Load/upload " << elapsed(load_start) << " ms\n";
        for (uint32_t repeat = 0; repeat < repeats; ++repeat)
        {
            const auto start = Clock::now();
            const auto output =
                renderer.render({request.width, request.height, request.profile, request.yaw,
                                 request.pitch, request.zoom, false, request.flip_y});
            const auto encode_start = Clock::now();
            const auto packet = encode_frame(output.frame);
            const auto encode_ms = elapsed(encode_start);
            write_bytes(value("--out", "frame.ngsf"), packet);
            if (options.contains("--reference"))
                write_ppm(options.at("--reference"), output.frame.rgba, request.width,
                          request.height);
            std::cout << stats_json(output.stats, packet.size(), encode_ms, elapsed(start)) << '\n';
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
