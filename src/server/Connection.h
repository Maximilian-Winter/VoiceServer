//
// Created by maxim on 31.08.2024.
//
#pragma once

#include <asio.hpp>
#include <iostream>
#include <string>

#include "AudioPacket.h"


using asio::ip::udp;
class Connection {
public:
    Connection(const udp::endpoint& endpoint)
        : endpoint_(endpoint) {}

    void send(udp::socket& socket, const AudioPacket& packet) {
        socket.async_send_to(
            asio::buffer(packet.data(), packet.size()), endpoint_,
            [](std::error_code ec, std::size_t bytes_sent) {
                if (ec) {
                    std::cerr << "Send error: " << ec.message() << std::endl;
                }
            });
    }

    const udp::endpoint& endpoint() const { return endpoint_; }

private:
    udp::endpoint endpoint_;
};