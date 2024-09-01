//
// Created by maxim on 01.09.2024.
//
#pragma once

#include <asio.hpp>
#include <AudioPacket.h>

#include "AudioManager.h"
#include "NetworkManager.h"
using asio::ip::udp;
class VoiceChatClient {
public:
    VoiceChatClient(asio::io_context& io_context, const std::string& host, short port)
        : network_manager_(std::make_shared<NetworkManager>(io_context, host, port)) {}

    bool start() {
        if (!audio_manager_.initialize()) {
            return false;
        }

        network_manager_->start(
            [this](const AudioPacket& packet) { audio_manager_.addOutputData(packet); },
            [this]() { return audio_manager_.getInputData(); }
        );

        return true;
    }

private:
    AudioManager audio_manager_;
    std::shared_ptr<NetworkManager> network_manager_;
};
