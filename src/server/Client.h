//
// Created by maxim on 31.08.2024.
//
#pragma once

#include <WebSocketSession.h>

#include <utility>

#include "Connection.h"
#include "AudioPacket.h"
enum class ClientType: uint8_t
{
    WEB_SOCKET,
    UDP
};


class Client {
public:
    virtual ~Client() = default;

    virtual void send(const AudioPacket& packet) = 0;

    virtual std::string getId() = 0;

};

class UDPClient: public Client{
public:
    std::string getId() override
    {
        return id_;
    }

    UDPClient(std::shared_ptr<Connection> connection, udp::socket &socket, std::string id)
        : connection_(std::move(connection)), id_(std::move(id)), socket_(socket) {
    }

    void send(const AudioPacket &packet) override {
        connection_->send(socket_, packet);
    }

    [[nodiscard]] std::string getId() const { return id_; }

private:
    std::shared_ptr<Connection> connection_;
    std::string id_;
    udp::socket& socket_;
};


class WebSocketClient: public Client{
public:
    WebSocketClient(std::shared_ptr<WebSocketSession> connection, udp::socket& socket, std::string id)
        : Client(), connection_(std::move(connection)), id_(std::move(id)), socket_(socket)
    {
    }

    void send(const AudioPacket& packet) override {
        connection_->send(packet.data_vector(), WebSocketOpCode::Binary);
    }

    [[nodiscard]] std::string getId() override { return id_; }

private:
    std::shared_ptr<WebSocketSession> connection_;
    std::string id_;
    udp::socket& socket_;
};