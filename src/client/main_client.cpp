#include <iostream>

#include <Config.h>
#include <asio.hpp>

#include "VoiceChatClient.h"


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    Config config;
    if (!config.load(argv[1])) {
        std::cerr << "Failed to load configuration file." << std::endl;
        return 1;
    }

    try {
        asio::io_context io_context;
        VoiceChatClient client(
            io_context,
            config.get<std::string>("server_ip", "127.0.0.1"),
            config.get<short>("server_port", 12345)
        );

        if (client.start()) {
            std::cout << "Connected to voice chat server. Start speaking..." << std::endl;
            io_context.run();
        } else {
            std::cerr << "Failed to start the voice chat client." << std::endl;
            return 1;
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
