//
// Created by maxim on 01.09.2024.
//
#pragma once
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <deque>
#include "sha1.hpp"
#include <thread>
#include <iostream>
#include <string>
#include <vector>
#include "Utilities.h"

using asio::ip::tcp;

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

    WebSocketSession(tcp::socket socket)
        : socket_(std::move(socket)), ssl_socket_(nullptr), use_ssl_(false), uuid(Utilities::generateUuid()) {}

    WebSocketSession(tcp::socket socket, asio::ssl::context& ssl_context)
        : socket_(std::move(socket)),
          ssl_socket_(new asio::ssl::stream<tcp::socket>(std::move(socket_), ssl_context)),
          use_ssl_(true),
          uuid(Utilities::generateUuid()) {}

    void start() {
        if (use_ssl_) {
            do_ssl_handshake();
        } else {
            do_handshake();
        }
    }

    void setMessageHandler(const message_handler &msg_handler) {
        on_message_ = msg_handler;
    }

    void send(const std::vector<uint8_t>& message, WebSocketOpCode opcode = WebSocketOpCode::Binary) {
        auto frame = create_websocket_frame(message, opcode);
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push_back(std::make_shared<std::vector<uint8_t>>(std::move(frame)));
        if (!write_in_progress) {
            do_write();
        }
    }

    void send(const std::string& message, WebSocketOpCode opcode = WebSocketOpCode::Text) {
        send(std::vector<uint8_t>(message.begin(), message.end()), opcode);
    }

    std::string getUuid() { return uuid; }

private:
    void do_ssl_handshake() {
        auto self(shared_from_this());
        ssl_socket_->async_handshake(asio::ssl::stream_base::server,
            [this, self](std::error_code ec) {
                if (!ec) {
                    do_handshake();
                } else {
                    std::cerr << "SSL handshake error: " << ec.message() << "\n";
                }
            });
    }

    void do_handshake() {
        auto self(shared_from_this());
        async_read_until(socket_, buffer_, "\r\n\r\n",
            [this, self](std::error_code ec, std::size_t length) {
                if (!ec) {
                    std::string request(asio::buffers_begin(buffer_.data()),
                                        asio::buffers_begin(buffer_.data()) + length);
                    buffer_.consume(length);

                    std::string key = extract_websocket_key(request);
                    if (key.empty()) {
                        std::cerr << "Invalid WebSocket handshake request\n";
                        return;
                    }

                    std::string accept = generate_websocket_accept(key);

                    std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                           "Upgrade: websocket\r\n"
                                           "Connection: Upgrade\r\n"
                                           "Sec-WebSocket-Accept: " + accept + "\r\n"
                                           "\r\n";
                    auto msg = std::make_shared<std::string>(response);
                    async_write(asio::buffer(*msg),
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
        read_buffer_->resize(65536);
        async_read_some(asio::buffer(*read_buffer_),
            [this, self](std::error_code ec, std::size_t length) {
                if (!ec) {
                    handle_frame(*read_buffer_, length);
                    do_read();
                } else {
                    std::cerr << "Read error: " << ec.message() << "\n";
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        if (!write_queue_.empty()) {
            async_write(asio::buffer(*write_queue_.front()),
                [this, self](std::error_code ec, std::size_t /*length*/) {
                    if (!ec) {
                        write_queue_.pop_front();
                        if (!write_queue_.empty()) {
                            do_write();
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

        // Handle different payload length scenarios
        if (payload_length == 126) {
            if (length < 4) return;
            payload_length = (static_cast<uint16_t>(buffer[2]) << 8) | buffer[3];
            header_length += 2;
        } else if (payload_length == 127) {
            if (length < 10) return;
            payload_length = 0;
            for (int i = 0; i < 8; ++i) {
                payload_length = (payload_length << 8) | buffer[2 + i];
            }
            header_length += 8;

            // Check for protocol violation: payload length exceeds maximum allowed
            if (payload_length > 0x7FFFFFFFFFFFFFFF) {
                // Handle error: payload too large
                std::cerr << "Error: Payload length exceeds maximum allowed by WebSocket protocol." << std::endl;
                return;
            }
        }

        // Sanity check: ensure payload length is reasonable
        const uint64_t MAX_REASONABLE_PAYLOAD = 1024 * 1024 * 100;  // 100 MB, adjust as needed
        if (payload_length > MAX_REASONABLE_PAYLOAD) {
            // Handle error: payload too large
            std::cerr << "Error: Payload length " << payload_length << " exceeds maximum reasonable size." << std::endl;
            return;
        }

        // Ensure we have enough data for the full frame
        if (length < header_length + payload_length) {
            // Handle error: incomplete frame
            std::cerr << "Error: Incomplete WebSocket frame." << std::endl;
            return;
        }

        if (masked) {
            if (length < header_length + 4) return;
            std::vector<unsigned char> mask(buffer.begin() + header_length, buffer.begin() + header_length + 4);
            header_length += 4;

            std::vector<unsigned char> payload(buffer.begin() + header_length, buffer.begin() + header_length + payload_length);
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask[i % 4];
            }

            on_message_(opcode, std::string(payload.begin(), payload.end()));
        } else {
            std::vector<unsigned char> payload(buffer.begin() + header_length, buffer.begin() + header_length + payload_length);
            on_message_(opcode, std::string(payload.begin(), payload.end()));
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
        SHA1Custom::SHA1 sha1;
        sha1.update(concatenated);
        std::string hash = sha1.final();

        return Base64Utilities::to_base_64(hash);
    }

    std::vector<uint8_t> create_websocket_frame(const std::vector<uint8_t>& message, WebSocketOpCode opcode) {
        std::vector<uint8_t> frame;
        frame.push_back(0x80 | static_cast<uint8_t>(opcode));  // FIN bit set, opcode

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
    template<typename AsyncReadStream, typename ReadHandler>
      void async_read_until(AsyncReadStream& s, asio::streambuf& b,
                            const std::string& delim, ReadHandler handler) {
        if (use_ssl_) {
            asio::async_read_until(*ssl_socket_, b, delim, handler);
        } else {
            asio::async_read_until(socket_, b, delim, handler);
        }
    }

    template<typename ConstBufferSequence, typename WriteHandler>
    void async_write(const ConstBufferSequence& buffers, WriteHandler handler) {
        if (use_ssl_) {
            asio::async_write(*ssl_socket_, buffers, handler);
        } else {
            asio::async_write(socket_, buffers, handler);
        }
    }

    template<typename MutableBufferSequence, typename ReadHandler>
    void async_read_some(const MutableBufferSequence& buffers, ReadHandler handler) {
        if (use_ssl_) {
            ssl_socket_->async_read_some(buffers, handler);
        } else {
            socket_.async_read_some(buffers, handler);
        }
    }

    std::string uuid;
    tcp::socket socket_;
    std::unique_ptr<asio::ssl::stream<tcp::socket>> ssl_socket_;
    bool use_ssl_;
    asio::streambuf buffer_;
    std::shared_ptr<std::vector<uint8_t>> read_buffer_;
    std::deque<std::shared_ptr<std::vector<uint8_t>>> write_queue_;
    message_handler on_message_;
};
