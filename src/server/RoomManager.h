#pragma once

#include <deque>

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

        // Add the new audio packet to the buffer
        audioBuffers_[senderId].push_back(packet);

        // Keep only the last N packets (e.g., 5 packets for 100ms of audio at 20ms per packet)
        while (audioBuffers_[senderId].size() > 5) {
            audioBuffers_[senderId].pop_front();
        }

        // Mix audio for each client
        for (const auto& [clientId, client] : clients_) {
            std::vector<AudioPacket> packetsToMix;
            for (const auto& [bufferId, bufferPackets] : audioBuffers_) {
                if (bufferId != clientId) {  // Don't include client's own audio
                    // Add the most recent packet from each client to mix
                    if (!bufferPackets.empty()) {
                        packetsToMix.push_back(bufferPackets.back());
                    }
                }
            }

            if (!packetsToMix.empty()) {
                AudioPacket mixedPacket = AudioMixer::mix(packetsToMix);
                client->send(socket, mixedPacket);
            }
        }
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Client>> clients_;
    std::unordered_map<std::string, std::deque<AudioPacket>> audioBuffers_;
    std::mutex mutex_;
};