#pragma once

#include "Client.h"
#include "AudioPacket.h"
#include "AudioMixer.h"


class RoomManager {
public:
    void addClient(std::shared_ptr<Client> client) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_[client->getId()] = client;
    }

    void removeClient(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(clientId);
    }

    std::shared_ptr<Client> getClient(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(clientId);
        return (it != clients_.end()) ? it->second : nullptr;
    }

    void processAudio(udp::socket& socket, const std::string& senderId, const AudioPacket& packet) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Update the timestamp for the sender
        auto now = std::chrono::steady_clock::now();
        clientLastActivity_[senderId] = now;

        // Add the new audio packet to the buffer
        audioBuffer_[senderId] = packet;

        // Mix audio for each client
        for (const auto& [clientId, client] : clients_) {
            std::vector<AudioPacket> packetsToMix;
            for (const auto& [bufferId, bufferPacket] : audioBuffer_) {
                if (bufferId != clientId) {  // Don't include client's own audio
                    auto lastActivity = clientLastActivity_[bufferId];
                    // Only include audio from clients who have been active in the last second
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity).count() < 1) {
                        packetsToMix.push_back(bufferPacket);
                    }
                }
            }

            if (!packetsToMix.empty()) {
                AudioPacket mixedPacket = AudioMixer::mix(packetsToMix);
                client->send(socket, mixedPacket);
            }
        }

        // Clean up old entries
        for (auto it = clientLastActivity_.begin(); it != clientLastActivity_.end();) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > 5) {
                audioBuffer_.erase(it->first);
                it = clientLastActivity_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Client>> clients_;
    std::unordered_map<std::string, AudioPacket> audioBuffer_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> clientLastActivity_;
    std::mutex mutex_;
};