#pragma once

#include <asio.hpp>
#include <AudioPacket.h>
#include <iostream>
#include <chrono>
#include <memory>
#include <queue>

using asio::ip::udp;

class NetworkManager : public std::enable_shared_from_this<NetworkManager> {
public:
    NetworkManager(asio::io_context& io_context, const std::string& host, short port)
        : socket_(io_context, udp::endpoint(udp::v4(), 0)),
          resolver_(io_context),
          send_timer_(io_context),
          jitter_buffer_timer_(io_context),
          strand_(io_context) {
        auto endpoints = resolver_.resolve(udp::v4(), host, std::to_string(port));
        server_endpoint_ = *endpoints.begin();
    }

    void start(std::function<void(const AudioPacket&)> receive_callback,
               std::function<AudioPacket()> send_callback) {
        receive_callback_ = std::move(receive_callback);
        send_callback_ = std::move(send_callback);
        start_receive();
        start_send();
        start_jitter_buffer();
    }

private:
    static constexpr int JITTER_BUFFER_SIZE = 3;  // Number of packets to buffer
    static constexpr int PACKET_INTERVAL = 20;    // Milliseconds between packets

    void start_receive() {
        auto self(shared_from_this());
        recv_buffer_.resize(16384);
        socket_.async_receive_from(
            asio::buffer(recv_buffer_), server_endpoint_,
            strand_.wrap([this, self](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    AudioPacket packet(recv_buffer_.data(), bytes_recvd);
                    jitter_buffer_.push(packet);
                } else if (ec != asio::error::operation_aborted) {
                    std::cerr << "Receive error: " << ec.message() << std::endl;
                }
                start_receive();
            }));
    }

    void start_send() {
        auto self(shared_from_this());
        send_timer_.expires_after(std::chrono::milliseconds(PACKET_INTERVAL));
        send_timer_.async_wait(strand_.wrap([this, self](std::error_code ec) {
            if (!ec) {
                AudioPacket packet = send_callback_();
                if (!packet.empty()) {
                    send(packet);
                }
                start_send();
            }
        }));
    }

    void start_jitter_buffer() {
        auto self(shared_from_this());
        jitter_buffer_timer_.expires_after(std::chrono::milliseconds(PACKET_INTERVAL));
        jitter_buffer_timer_.async_wait(strand_.wrap([this, self](std::error_code ec) {
            if (!ec) {
                if (!jitter_buffer_.empty()) {
                    receive_callback_(jitter_buffer_.front());
                    jitter_buffer_.pop();
                }
                start_jitter_buffer();
            }
        }));
    }

    void send(const AudioPacket& packet) {
        auto self(shared_from_this());
        socket_.async_send_to(
            asio::buffer(packet.data(), packet.size()), server_endpoint_,
            strand_.wrap([this, self](std::error_code ec, std::size_t bytes_sent) {
                if (ec && ec != asio::error::operation_aborted) {
                    std::cerr << "Send error: " << ec.message() << std::endl;
                }
            }));
    }

    udp::socket socket_;
    udp::resolver resolver_;
    udp::endpoint server_endpoint_;
    asio::steady_timer send_timer_;
    asio::steady_timer jitter_buffer_timer_;
    asio::io_context::strand strand_;
    std::vector<uint8_t> recv_buffer_;
    std::function<void(const AudioPacket&)> receive_callback_;
    std::function<AudioPacket()> send_callback_;
    std::queue<AudioPacket> jitter_buffer_;
};