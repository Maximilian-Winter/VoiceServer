#include <iostream>
#include <string>
#include <memory>

#include <asio.hpp>
#include <utility>
#include <WebSocketServer.h>

#include "AsioThreadPool.h"
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

    void add_websocket_user(const std::shared_ptr<WebSocketSession> &connection) {
        auto client = std::make_shared<WebSocketClient>(connection, socket_, connection->getUuid());
        room_manager_->addClient(client);
        std::cout << "New client connected: " << client->getId() << std::endl;
    }

    void handle_receive_websocket(const std::string &client_key, const std::vector<uint8_t> &bytes) const {
        const AudioPacket packet(bytes.data(), bytes.size());
        room_manager_->processAudio(client_key, packet);
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
            auto client = std::make_shared<UDPClient>(connection, socket_, client_key);
            room_manager_->addClient(client);
        }

        AudioPacket packet(recv_buffer_.data(), bytes_recvd);
        room_manager_->processAudio(client_key, packet);
    }

    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    std::array<uint8_t, 16384> recv_buffer_{};
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
        AsioThreadPool thread_pool(1);

        auto server = std::make_shared<VoiceChatServer>(thread_pool.get_io_context(), config.get<short>("port", 12345));

        const auto web_socket_server = std::make_shared<WebSocketServer>(thread_pool.get_io_context(), 8080, true);

        web_socket_server->set_new_client_handler([server](const std::shared_ptr<WebSocketSession> &session) {
            server->add_websocket_user(session);
        });
        web_socket_server->set_message_handler(
            [server](const std::shared_ptr<WebSocketSession> &session, const WebSocketOpCode opcode,
                     const std::string &message) {
                if (opcode == WebSocketOpCode::Binary) {
                    const std::vector<uint8_t> data(message.begin(), message.end());
                    server->handle_receive_websocket(session->getUuid(), data);
            }

        });

        server->start();
        thread_pool.run();

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}