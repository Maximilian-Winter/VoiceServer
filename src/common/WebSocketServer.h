//
// Created by maxim on 01.09.2024.
//
#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include "WebSocketSession.h"

using asio::ip::tcp;

class WebSocketServer {
public:
    using new_client_handler = std::function<void(std::shared_ptr<WebSocketSession>)>;
    using message_handler = std::function<void(std::shared_ptr<WebSocketSession>, WebSocketOpCode, const std::string&)>;

    WebSocketServer(asio::io_context& io_context, short port, bool use_ssl = false)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
          timer_(io_context),
          ssl_context_(asio::ssl::context::sslv23),
          use_ssl_(use_ssl) {
        if (use_ssl_) {
            configure_ssl();
        }
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
    void configure_ssl() {
        ssl_context_.set_options(
            asio::ssl::context::default_workarounds
            | asio::ssl::context::no_sslv2
            | asio::ssl::context::single_dh_use);
        ssl_context_.use_certificate_chain_file("/etc/letsencrypt/live/holistic-games.com/fullchain.pem");
        ssl_context_.use_private_key_file("/etc/letsencrypt/live/holistic-games.com/privkey.pem", asio::ssl::context::pem);
        ssl_context_.use_tmp_dh_file("dh2048.pem");
    }

    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::shared_ptr<WebSocketSession> session;
                    if (use_ssl_) {
                        session = std::make_shared<WebSocketSession>(std::move(socket), ssl_context_);
                    } else {
                        session = std::make_shared<WebSocketSession>(std::move(socket));
                    }

                    if (new_client_handler_) {
                        new_client_handler_(session);
                    }
                    session->setMessageHandler([this, session](WebSocketOpCode opcode, const std::string& message) {
                        if (session && message_handler_) {
                            message_handler_(session, opcode, message);
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
    asio::ssl::context ssl_context_;
    bool use_ssl_;
    message_handler message_handler_;
    new_client_handler new_client_handler_;
    std::vector<std::shared_ptr<WebSocketSession>> sessions_;
};