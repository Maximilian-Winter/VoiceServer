#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <sha1.hpp>
#include <AsioThreadPool.h>
using asio::ip::tcp;

namespace Base64Utilities
{
    std::string from_base64(const std::string& input)
    {
        static const std::string b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static std::unordered_map<char, int> b64_index;
        if (b64_index.empty()) {
            for (int i = 0; i < 64; ++i) {
                b64_index[b64_chars[i]] = i;
            }
        }

        std::string result;
        int val = 0;
        int val_b = -8;
        for (char c : input) {
            if (c == '=') break;
            if (b64_index.find(c) == b64_index.end()) continue;

            val = (val << 6) + b64_index[c];
            val_b += 6;
            if (val_b >= 0) {
                result.push_back(char((val >> val_b) & 0xFF));
                val_b -= 8;
            }
        }

        return result;
    }

    std::string to_base_64(const std::string& input)
    {
        // Convert hash to base64
        static const char* b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        unsigned int val = 0;
        int val_b = -6;
        for (int i = 0; i < input.length(); i += 2) {
            unsigned char c = std::stoi(input.substr(i, 2), nullptr, 16);
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
}

enum class WebSocketOpCode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
};

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    using message_handler = std::function<void(WebSocketOpCode, const std::string&)>;

    explicit WebSocketSession(tcp::socket& socket)
        : socket_(std::move(socket)) {}

    void start() {
        do_handshake();
    }

    void setMessageHandler(const message_handler &msg_handler)
    {
        on_message_ = msg_handler;
    }
    void send(const std::string& message, WebSocketOpCode opcode = WebSocketOpCode::Text) {
        auto frame = create_websocket_frame(message, opcode);
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push_back(std::make_shared<std::vector<unsigned char>>(std::move(frame)));
        if (!write_in_progress) {
            do_write();
        }
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
                                do_read();
                            } else {
                                std::cerr << "Handshake write error: " << ec.message() << "\n";
                            }
                        });
                } else {
                    std::cerr << "Handshake read error: " << ec.message() << "\n";
                }
            });
    }

    void do_read() {
        auto self(shared_from_this());
        read_buffer_ = std::make_shared<std::vector<uint8_t>>();
        read_buffer_->resize(1024);
        socket_.async_read_some(asio::buffer(*read_buffer_),
            [this, self](std::error_code ec, std::size_t length) {
                if (!ec) {
                    handle_frame(*read_buffer_, length);
                    do_read(); // Continue reading
                } else {
                    std::cerr << "Read error: " << ec.message() << "\n";
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        if (!write_queue_.empty()) {
            asio::async_write(socket_, asio::buffer(*write_queue_.front()),
                [this, self](std::error_code ec, std::size_t /*length*/) {
                    if (!ec) {
                        write_queue_.pop_front();
                        if (!write_queue_.empty()) {
                            do_write(); // Continue writing if there are more messages
                        }
                    } else {
                        std::cerr << "Write error: " << ec.message() << "\n";
                    }
                });
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


    void handle_frame(const std::vector<unsigned char>& buffer, std::size_t length) {
        if (length < 2) return;

        bool fin = (buffer[0] & 0x80) != 0;
        WebSocketOpCode opcode = static_cast<WebSocketOpCode>(buffer[0] & 0x0F);
        bool masked = (buffer[1] & 0x80) != 0;
        uint64_t payload_length = buffer[1] & 0x7F;
        size_t header_length = 2;

        if (payload_length == 126) {
            if (length < 4) return;
            payload_length = (buffer[2] << 8) | buffer[3];
            header_length += 2;
        } else if (payload_length == 127) {
            if (length < 10) return;
            payload_length = 0;
            for (int i = 0; i < 8; ++i) {
                payload_length = (payload_length << 8) | buffer[2 + i];
            }
            header_length += 8;
        }

        if (masked) {
            if (length < header_length + 4) return;
            std::vector<unsigned char> mask(buffer.begin() + header_length, buffer.begin() + header_length + 4);
            header_length += 4;

            std::string payload(buffer.begin() + header_length, buffer.begin() + header_length + payload_length);
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask[i % 4];
            }

            on_message_(opcode, payload);
        } else {
            std::string payload(buffer.begin() + header_length, buffer.begin() + header_length + payload_length);
            on_message_(opcode, payload);
        }
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

        return Base64Utilities::to_base_64(hash);
    }

    std::vector<unsigned char> create_websocket_frame(const std::string& message, WebSocketOpCode opcode) {
        std::vector<unsigned char> frame;
        frame.push_back(0x80 | static_cast<unsigned char>(opcode));  // FIN bit set, opcode

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

        frame.insert(frame.end(), message.begin(), message.end());
        return frame;
    }

    tcp::socket socket_;
    asio::streambuf buffer_;
    std::shared_ptr<std::vector<uint8_t>> read_buffer_;
    std::deque<std::shared_ptr<std::vector<uint8_t>>> write_queue_;
    message_handler on_message_;
};

class WebSocketServer {
public:
    WebSocketServer(asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
          timer_(io_context) {
        do_accept();
        start_hello_world_timer();
    }

    void set_message_handler(std::function<void(std::shared_ptr<WebSocketSession>, WebSocketOpCode, const std::string&)> handler) {
        message_handler_ = std::move(handler);
    }

    void broadcast(const std::string& message, WebSocketOpCode opcode = WebSocketOpCode::Text) {
        for (auto& session : sessions_) {
            session->send(message, opcode);
        }
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto session = std::make_shared<WebSocketSession>(socket);
                    session->setMessageHandler([this, session](WebSocketOpCode opcode, const std::string& message) mutable {
                        if (session) {
                            if (message_handler_) {
                                message_handler_(session, opcode, message);
                            }
                        }
                    });
                    sessions_.push_back(session);
                    session->start();
                }
                do_accept();
            });
    }

    void start_hello_world_timer() {
        timer_.expires_after(std::chrono::seconds(1));
        timer_.async_wait([this](const asio::error_code& ec) {
            if (!ec) {
                broadcast("Hello World");
                start_hello_world_timer(); // Reschedule the timer
            }
        });
    }

    tcp::acceptor acceptor_;
    asio::steady_timer timer_;
    std::function<void(std::shared_ptr<WebSocketSession>, WebSocketOpCode, const std::string&)> message_handler_;
    std::vector<std::shared_ptr<WebSocketSession>> sessions_;
};

int main() {
    try {
        AsioThreadPool thread_pool;
        WebSocketServer server(thread_pool.get_io_context(), 8080);

        server.set_message_handler([&server](std::shared_ptr<WebSocketSession> session, WebSocketOpCode opcode, const std::string& message) {
            std::cout << "Received message: " << message << std::endl;
            // Echo the message back to the client
            session->send(message, opcode);
        });

        thread_pool.run();
        std::cin.get();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}