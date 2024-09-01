//
// Created by maxim on 01.09.2024.
//
#pragma once

#include <asio.hpp>
#include <asio/ts/internet.hpp>

#include <iostream>
#include <string>
#include <vector>

#include <AsioThreadPool.h>
#include "WebSocketSession.h"

using asio::ip::tcp;


class WebSocketServer {
public:
    using new_client_handler = std::function<void(std::shared_ptr<WebSocketSession>)>;
    using message_handler = std::function<void(std::shared_ptr<WebSocketSession>, WebSocketOpCode, const std::string&)>;

    WebSocketServer(asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
          timer_(io_context) {
        do_accept();
    }

    void set_message_handler(message_handler handler) {
        message_handler_ = std::move(handler);
    }
    void set_new_client_handler(new_client_handler handler) {
        new_client_handler_ = std::move(handler);
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
                    if(new_client_handler_)
                    {
                        new_client_handler_(session);
                    }
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


    tcp::acceptor acceptor_;
    asio::steady_timer timer_;
    message_handler message_handler_;
    new_client_handler new_client_handler_;
    std::vector<std::shared_ptr<WebSocketSession>> sessions_;
};

