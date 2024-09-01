#include <iostream>
#include <string>
#include <memory>

#include <asio.hpp>

#include "Config.h"
#include "RoomManager.h"



using asio::ip::udp;

class VoiceChatServer : public std::enable_shared_from_this<VoiceChatServer> {
public:
    VoiceChatServer(asio::io_context& io_context, short port)
    : io_context_(io_context), socket_(io_context, udp::endpoint(udp::v4(), port))
    {
        room_manager_ = std::make_shared<RoomManager>(io_context);
    }

    void start() {
        std::cout << "Voice Chat Server started. Waiting for clients..." << std::endl;
        start_receive();
    }

private:
    void start_receive() {
        socket_.async_receive_from(
            asio::buffer(recv_buffer_), remote_endpoint_,
            [this, self = shared_from_this()](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    handle_receive(bytes_recvd);
                } else {
                    std::cerr << "Receive error: " << ec.message() << std::endl;
                }
                start_receive();
            });
    }

    void handle_receive(std::size_t bytes_recvd) {
        std::string client_key = remote_endpoint_.address().to_string() + ":" +
                                 std::to_string(remote_endpoint_.port());

        if (room_manager_->getClient(client_key) == nullptr) {
            std::cout << "New client connected: " << client_key << std::endl;
            auto connection = std::make_shared<Connection>(remote_endpoint_);
            auto client = std::make_shared<Client>(connection, socket_, client_key);
            room_manager_->addClient(client);
        }

        AudioPacket packet(recv_buffer_.data(), bytes_recvd);
        room_manager_->processAudio(client_key, packet);
    }

    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    std::array<char, 32768> recv_buffer_{};
    asio::io_context& io_context_;
    std::shared_ptr<RoomManager> room_manager_;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    Config config;
    config.load(argv[1]);

    try {
        asio::io_context io_context;
        auto server = std::make_shared<VoiceChatServer>(io_context, config.get<short>("port", 12345));
        server->start();
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}