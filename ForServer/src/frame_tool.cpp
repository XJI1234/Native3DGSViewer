#include "gs_server/frame.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        if (argc < 3)
            throw std::runtime_error("Usage: gs-frame inspect file | decode file output.ppm");
        const auto packet = gs::server::read_bytes(argv[2]);
        const auto info = gs::server::inspect_frame(packet);
        if (std::string(argv[1]) == "inspect")
            std::cout << "{\"width\":" << info.width << ",\"height\":" << info.height
                      << ",\"profile\":\"" << gs::server::profile_name(info.profile)
                      << "\",\"packet_bytes\":" << packet.size()
                      << ",\"unpacked_bytes\":" << info.unpacked_bytes << "}\n";
        else if (std::string(argv[1]) == "decode" && argc == 4)
        {
            const auto frame = gs::server::decode_frame(packet);
            gs::server::write_ppm(argv[3], frame.rgba, frame.width, frame.height);
        }
        else
            throw std::runtime_error("Unknown command");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
