#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include "sha1.hpp"

using asio::ip::tcp;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    WebSocketSession(tcp::socket socket)
        : socket_(std::move(socket)) {}

    void start() {
        do_handshake();
    }

private:
    void do_handshake() {
        auto self(shared_from_this());
        asio::async_read_until(socket_, buffer_, "\r\n\r\n",
            [this, self](std::error_code ec, std::size_t length) {
                if (!ec) {
                    // Parse WebSocket handshake request
                    std::string request(asio::buffers_begin(buffer_.data()),
                                        asio::buffers_begin(buffer_.data()) + length);
                    buffer_.consume(length);

                    // Extract the Sec-WebSocket-Key
                    std::string key = extract_websocket_key(request);
                    if (key.empty()) {
                        std::cerr << "Invalid WebSocket handshake request\n";
                        return;
                    }

                    // Generate the Sec-WebSocket-Accept value
                    std::string accept = generate_websocket_accept(key);

                    // Generate WebSocket handshake response
                    std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                           "Upgrade: websocket\r\n"
                                           "Connection: Upgrade\r\n"
                                           "Sec-WebSocket-Accept: " + accept + "\r\n"
                                           "\r\n";
                    auto msg = std::make_shared<std::string>(response);
                    asio::async_write(socket_, asio::buffer(*msg),
                        [this, msg, self](std::error_code ec, std::size_t /*length*/) {
                            if (!ec) {
                                do_write();
                            } else {
                                std::cerr << "Handshake write error: " << ec.message() << "\n";
                            }
                        });
                } else {
                    std::cerr << "Handshake read error: " << ec.message() << "\n";
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        std::string message = "Hello from WebSocket server!";

        // Construct WebSocket frame
        auto frame = create_websocket_frame(message);

        // Store the frame in the write queue
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push_back(std::make_shared<std::vector<unsigned char>>(std::move(frame)));

        if (!write_in_progress) {
            do_write_frame();
        }
    }

    void do_write_frame() {
        auto self(shared_from_this());
        std::shared_ptr<std::vector<unsigned char>> msg = write_queue_.front();
        asio::async_write(socket_, asio::buffer(*msg),
            [this, msg, self](std::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_queue_.pop_front();
                    if (!write_queue_.empty()) {
                        do_write_frame();
                    } else {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        do_write();  // Schedule next message
                    }
                } else {
                    std::cerr << "Write error: " << ec.message() << "\n";
                }
            });
    }

    std::string extract_websocket_key(const std::string& request) {
        std::string key_header = "Sec-WebSocket-Key: ";
        auto key_pos = request.find(key_header);
        if (key_pos == std::string::npos) return "";
        auto key_end = request.find("\r\n", key_pos);
        if (key_end == std::string::npos) return "";
        return request.substr(key_pos + key_header.length(), key_end - (key_pos + key_header.length()));
    }

    std::string generate_websocket_accept(const std::string& key) {
        std::string magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string concatenated = key + magic_string;

        // Generate SHA1 hash
        SHA1 sha1;
        sha1.update(concatenated);
        std::string hash = sha1.final();

        // Convert hash to base64
        static const char* b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        unsigned int val = 0;
        int val_b = -6;
        for (int i = 0; i < hash.length(); i += 2) {
            unsigned char c = std::stoi(hash.substr(i, 2), nullptr, 16);
            val = (val << 8) + c;
            val_b += 8;
            while (val_b >= 0) {
                result.push_back(b64_table[(val >> val_b) & 0x3F]);
                val_b -= 6;
            }
        }
        if (val_b > -6) result.push_back(b64_table[((val << 8) >> (val_b + 8)) & 0x3F]);
        while (result.size() % 4) result.push_back('=');

        return result;
    }

    std::vector<unsigned char> create_websocket_frame(const std::string& message) {
        std::vector<unsigned char> frame;
        frame.push_back(0x81);  // FIN bit set, opcode for text frame

        if (message.size() <= 125) {
            frame.push_back(message.size());
        } else if (message.size() <= 65535) {
            frame.push_back(126);
            frame.push_back((message.size() >> 8) & 0xFF);
            frame.push_back(message.size() & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                frame.push_back((message.size() >> (i * 8)) & 0xFF);
            }
        }

        // Add masking key (server-to-client messages should not be masked)
        frame.insert(frame.end(), message.begin(), message.end());
        return frame;
    }

    tcp::socket socket_;
    asio::streambuf buffer_;
    std::deque<std::shared_ptr<std::vector<unsigned char>>> write_queue_;
};

class WebSocketServer {
public:
    WebSocketServer(asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<WebSocketSession>(std::move(socket))->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main() {
    try {
        asio::io_context io_context;
        WebSocketServer server(io_context, 8080);
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}