//
// Created by maxim on 31.08.2024.
//
#pragma once

#include "Connection.h"
#include "AudioPacket.h"
class Client {
public:
    Client(std::shared_ptr<Connection> connection, const std::string& id)
        : connection_(connection), id_(id) {}

    void send(udp::socket& socket, const AudioPacket& packet) {
        connection_->send(socket, packet);
    }

    std::string getId() const { return id_; }

private:
    std::shared_ptr<Connection> connection_;
    std::string id_;
};
