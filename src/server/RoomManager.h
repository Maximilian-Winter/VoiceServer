#pragma once

#include "Client.h"
#include "AudioPacket.h"



#include <chrono>
#include <deque>
#include <unordered_map>
#include <asio.hpp>
#include "Client.h"
#include "AudioPacket.h"
#include "AudioMixer.h"

class RoomManager : public std::enable_shared_from_this<RoomManager> {
public:
    RoomManager(asio::io_context& io_context)
        : timer_(io_context), strand_(io_context) {

    }

    void addClient(std::shared_ptr<Client> client) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_[client->getId()] = client;
        if(clients_.size() == 1)
        {
            startMixingTimer();
        }
    }

    void removeClient(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(clientId);
        audioBuffers_.erase(clientId);
    }

    std::shared_ptr<Client> getClient(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(clientId);
        return (it != clients_.end()) ? it->second : nullptr;
    }

    void processAudio(const std::string& senderId, const AudioPacket& packet) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Add the new audio packet to the buffer for this client
        audioBuffers_[senderId].push_back(packet);

        // Limit the buffer size to prevent excessive memory usage
        if (audioBuffers_[senderId].size() > MAX_BUFFER_SIZE) {
            audioBuffers_[senderId].pop_front();
        }

        // Update last activity timestamp for this client
        clientLastActivity_[senderId] = std::chrono::steady_clock::now();
    }

private:
    static constexpr size_t MAX_BUFFER_SIZE = 50; // Adjust based on your needs
    static constexpr auto ACTIVITY_TIMEOUT = std::chrono::seconds(5);
    static constexpr auto MIX_INTERVAL = std::chrono::milliseconds(20); // Mix every 20ms

    std::unordered_map<std::string, std::shared_ptr<Client>> clients_;
    std::unordered_map<std::string, std::deque<AudioPacket>> audioBuffers_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> clientLastActivity_;
    std::mutex mutex_;
    asio::steady_timer timer_;
    asio::io_context::strand strand_;

    void startMixingTimer() {
        auto self(shared_from_this());
        timer_.expires_after(MIX_INTERVAL);
        timer_.async_wait(strand_.wrap([this, self](std::error_code ec) {
            if (!ec) {
                mixAndSendAudio();
                startMixingTimer(); // Reschedule the timer
            }
        }));
    }

    void mixAndSendAudio() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        for (const auto& [clientId, client] : clients_) {
            std::vector<AudioPacket> packetsToMix;

            for (const auto& [bufferId, buffer] : audioBuffers_) {
                auto lastActivity = clientLastActivity_[bufferId];
                if (bufferId != clientId && !buffer.empty()) {
                    if (now - lastActivity <= ACTIVITY_TIMEOUT) {
                        packetsToMix.insert(packetsToMix.end(), buffer.begin(), buffer.end());
                    }
                }
            }

            if (!packetsToMix.empty()) {
                AudioPacket mixedPacket = AudioMixer::mix(packetsToMix);
                client->send(mixedPacket);
            }
        }

        // Clear processed packets
        for (auto& [bufferId, buffer] : audioBuffers_) {
            buffer.clear();
        }

        // Clean up inactive clients
        for (auto it = clientLastActivity_.begin(); it != clientLastActivity_.end();) {
            if (now - it->second > ACTIVITY_TIMEOUT) {
                audioBuffers_.erase(it->first);
                it = clientLastActivity_.erase(it);
            } else {
                ++it;
            }
        }
    }
};