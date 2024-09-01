# server\RoomManager.h

```h
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

        // Add the new audio packet to the buffer
        audioBuffer_[senderId] = packet;

        // Mix audio for each client
        for (const auto& [clientId, client] : clients_) {
            std::vector<AudioPacket> packetsToMix;
            for (const auto& [bufferId, bufferPacket] : audioBuffer_) {
                if (bufferId != clientId) {  // Don't include client's own audio
                    packetsToMix.push_back(bufferPacket);
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
    std::unordered_map<std::string, AudioPacket> audioBuffer_;
    std::mutex mutex_;
};
```

# server\main_server.cpp

```cpp
#include <iostream>
#include <string>
#include <memory>

#include <asio.hpp>

#include "Config.h"
#include "RoomManager.h"



using asio::ip::udp;

class VoiceChatServer : public std::enable_shared_from_this<VoiceChatServer> {
public:
    VoiceChatServer(asio::io_context& io_context, short port)
        : socket_(io_context, udp::endpoint(udp::v4(), port)),
          io_context_(io_context) {}

    void start() {
        std::cout << "Voice Chat Server started. Waiting for clients..." << std::endl;
        start_receive();
    }

private:
    void start_receive() {
        socket_.async_receive_from(
            asio::buffer(recv_buffer_), remote_endpoint_,
            [this, self = shared_from_this()](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    handle_receive(bytes_recvd);
                } else {
                    std::cerr << "Receive error: " << ec.message() << std::endl;
                }
                start_receive();
            });
    }

    void handle_receive(std::size_t bytes_recvd) {
        std::string client_key = remote_endpoint_.address().to_string() + ":" +
                                 std::to_string(remote_endpoint_.port());

        if (room_manager_.getClient(client_key) == nullptr) {
            std::cout << "New client connected: " << client_key << std::endl;
            auto connection = std::make_shared<Connection>(remote_endpoint_);
            auto client = std::make_shared<Client>(connection, client_key);
            room_manager_.addClient(client);
        }

        AudioPacket packet(recv_buffer_.data(), bytes_recvd);
        room_manager_.processAudio(socket_, client_key, packet);
    }

    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    std::array<char, 32768> recv_buffer_{};
    asio::io_context& io_context_;
    RoomManager room_manager_;
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    Config config;
    config.load(argv[1]);

    try {
        asio::io_context io_context;
        auto server = std::make_shared<VoiceChatServer>(io_context, config.get<short>("port", 12345));
        server->start();
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
```

# server\Connection.h

```h
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
```

# server\Client.h

```h
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

```

# server\AudioMixer.h

```h
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "AudioPacket.h"

class AudioMixer {
public:
    static AudioPacket mix(const std::vector<AudioPacket>& packets) {
        if (packets.empty()) {
            return AudioPacket();
        }

        // Assume all packets have the same size and format (16-bit PCM)
        size_t sampleCount = packets[0].size() / sizeof(int16_t);
        std::vector<float> mixBuffer(sampleCount, 0.0f);

        // Mix all packets
        for (const auto& packet : packets) {
            const int16_t* samples = reinterpret_cast<const int16_t*>(packet.data());
            for (size_t i = 0; i < sampleCount; ++i) {
                mixBuffer[i] += static_cast<float>(samples[i]) / INT16_MAX;
            }
        }

        // Calculate the scaling factor
        float maxAmp = 0.0f;
        for (float sample : mixBuffer) {
            maxAmp = std::max(maxAmp, std::abs(sample));
        }
        float scaleFactor = maxAmp > 1.0f ? 1.0f / maxAmp : 1.0f;

        // Scale, clip, and convert back to int16_t
        std::vector<int16_t> outputBuffer(sampleCount);
        for (size_t i = 0; i < sampleCount; ++i) {
            float scaledSample = mixBuffer[i] * scaleFactor;
            scaledSample = std::clamp(scaledSample, -1.0f, 1.0f);
            outputBuffer[i] = static_cast<int16_t>(scaledSample * INT16_MAX);
        }

        return AudioPacket(reinterpret_cast<const char*>(outputBuffer.data()), outputBuffer.size() * sizeof(int16_t));
    }
};
```

# common\Config.h

```h
//
// Created by maxim on 30.07.2024.
//

#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

class Config {
private:
    nlohmann::json config;

public:
    Config() = default;

    bool load(const std::string& filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Failed to open config file: " << filename << std::endl;
                return false;
            }
            file >> config;
            return true;
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            return false;
        }
    }

    template<typename T>
    T get(const std::string& key, const T& default_value) const {
        try {
            return config.at(key).get<T>();
        } catch (const nlohmann::json::out_of_range&) {
            std::cerr << "Key not found: " << key << ". Using default value." << std::endl;
            return default_value;
        } catch (const nlohmann::json::type_error&) {
            std::cerr << "Type error for key: " << key << ". Using default value." << std::endl;
            return default_value;
        }
    }

    bool set(const std::string& key, const nlohmann::json& value) {
        config[key] = value;
        return true;
    }

    bool save(const std::string& filename) const {
        try {
            std::ofstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Failed to open file for writing: " << filename << std::endl;
                return false;
            }
            file << std::setw(4) << config << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error saving config: " << e.what() << std::endl;
            return false;
        }
    }
};

```

# common\AudioPacket.h

```h
//
// Created by maxim on 31.08.2024.
//
#pragma once

#include <vector>


class AudioPacket {
public:
    AudioPacket() = default;
    AudioPacket(const char* data, size_t size) : data_(data, data + size) {}

    [[nodiscard]] const char* data() const { return data_.data(); }
    [[nodiscard]] size_t size() const { return data_.size(); }

    [[nodiscard]] bool empty() const { return data_.empty(); }

private:
    std::vector<char> data_;
};
```

# client\VoiceChatClient.h

```h
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

```

# client\NetworkManager.h

```h
#pragma once

#include <asio.hpp>
#include <AudioPacket.h>
#include <iostream>
#include <chrono>
#include <memory>

using asio::ip::udp;

class NetworkManager : public std::enable_shared_from_this<NetworkManager> {
public:
    NetworkManager(asio::io_context& io_context, const std::string& host, short port)
        : socket_(io_context, udp::endpoint(udp::v4(), 0)),
          resolver_(io_context),
          send_timer_(io_context),
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
    }

private:
    void start_receive() {
        auto self(shared_from_this());
        recv_buffer_.resize(32768);
        socket_.async_receive_from(
            asio::buffer(recv_buffer_), server_endpoint_,
            strand_.wrap([this, self](std::error_code ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    AudioPacket packet(recv_buffer_.data(), bytes_recvd);
                    receive_callback_(packet);
                } else if (ec != asio::error::operation_aborted) {
                    std::cerr << "Receive error: " << ec.message() << std::endl;
                }
                start_receive();
            }));
    }

    void start_send() {
        auto self(shared_from_this());
        send_timer_.expires_after(std::chrono::milliseconds(20)); // 50Hz send rate
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
    asio::io_context::strand strand_;
    std::vector<char> recv_buffer_;
    std::function<void(const AudioPacket&)> receive_callback_;
    std::function<AudioPacket()> send_callback_;
};
```

# client\main_client.cpp

```cpp
#include <iostream>

#include <Config.h>
#include <asio.hpp>

#include "VoiceChatClient.h"


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    Config config;
    if (!config.load(argv[1])) {
        std::cerr << "Failed to load configuration file." << std::endl;
        return 1;
    }

    try {
        asio::io_context io_context;
        VoiceChatClient client(
            io_context,
            config.get<std::string>("server_ip", "127.0.0.1"),
            config.get<short>("server_port", 12345)
        );

        if (client.start()) {
            std::cout << "Connected to voice chat server. Start speaking..." << std::endl;
            io_context.run();
        } else {
            std::cerr << "Failed to start the voice chat client." << std::endl;
            return 1;
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

```

# client\AudioManager.h

```h
#pragma once

#include <AudioPacket.h>
#include <mutex>
#include <portaudio.h>
#include <deque>
#include <chrono>

class AudioManager {
public:
    AudioManager() : input_stream_(nullptr), output_stream_(nullptr) {}

    bool initialize() {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        err = Pa_OpenDefaultStream(&input_stream_, 1, 0, paInt16, SAMPLE_RATE, FRAMES_PER_BUFFER,
                                   inputCallback, this);
        if (err != paNoError) {
            std::cerr << "PortAudio input error: " << Pa_GetErrorText(err) << std::endl;
            Pa_Terminate();
            return false;
        }

        err = Pa_OpenDefaultStream(&output_stream_, 0, 1, paInt16, SAMPLE_RATE, FRAMES_PER_BUFFER,
                                   outputCallback, this);
        if (err != paNoError) {
            std::cerr << "PortAudio output error: " << Pa_GetErrorText(err) << std::endl;
            Pa_CloseStream(input_stream_);
            Pa_Terminate();
            return false;
        }

        return startStreams();
    }

    void addOutputData(const AudioPacket& packet) {
        std::lock_guard<std::mutex> lock(output_mutex_);
        output_buffer_.push_back(packet);
        while (output_buffer_.size() > MAX_BUFFER_SIZE) {
            output_buffer_.pop_front();
        }
    }

    AudioPacket getInputData() {
        std::lock_guard<std::mutex> lock(input_mutex_);
        AudioPacket packet(input_buffer_.data(), input_buffer_.size());
        input_buffer_.clear();
        return packet;
    }

    ~AudioManager() {
        if (input_stream_) {
            Pa_StopStream(input_stream_);
            Pa_CloseStream(input_stream_);
        }
        if (output_stream_) {
            Pa_StopStream(output_stream_);
            Pa_CloseStream(output_stream_);
        }
        Pa_Terminate();
    }

private:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int FRAMES_PER_BUFFER = 0;
    static constexpr int MAX_BUFFER_SIZE = 10;  // Maximum number of packets to buffer

    bool startStreams() {
        PaError err = Pa_StartStream(input_stream_);
        if (err != paNoError) {
            std::cerr << "PortAudio input start error: " << Pa_GetErrorText(err) << std::endl;
            Pa_CloseStream(input_stream_);
            Pa_CloseStream(output_stream_);
            Pa_Terminate();
            return false;
        }

        err = Pa_StartStream(output_stream_);
        if (err != paNoError) {
            std::cerr << "PortAudio output start error: " << Pa_GetErrorText(err) << std::endl;
            Pa_StopStream(input_stream_);
            Pa_CloseStream(input_stream_);
            Pa_CloseStream(output_stream_);
            Pa_Terminate();
            return false;
        }

        return true;
    }

    static int inputCallback(const void* inputBuffer, void* outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData) {
        AudioManager* manager = static_cast<AudioManager*>(userData);
        const int16_t* in = static_cast<const int16_t*>(inputBuffer);

        std::lock_guard<std::mutex> lock(manager->input_mutex_);
        manager->input_buffer_.insert(manager->input_buffer_.end(),
                                      reinterpret_cast<const char*>(in),
                                      reinterpret_cast<const char*>(in + framesPerBuffer));

        return paContinue;
    }

    static int outputCallback(const void* inputBuffer, void* outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo* timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void* userData) {
        AudioManager* manager = static_cast<AudioManager*>(userData);
        int16_t* out = static_cast<int16_t*>(outputBuffer);

        std::lock_guard<std::mutex> lock(manager->output_mutex_);
        if (!manager->output_buffer_.empty()) {
            const AudioPacket& packet = manager->output_buffer_.front();
            size_t bytesToCopy = std::min(packet.size(), framesPerBuffer * sizeof(int16_t));
            memcpy(out, packet.data(), bytesToCopy);
            if (bytesToCopy < framesPerBuffer * sizeof(int16_t)) {
                memset(reinterpret_cast<char*>(out) + bytesToCopy, 0, framesPerBuffer * sizeof(int16_t) - bytesToCopy);
            }
            manager->output_buffer_.pop_front();
        } else {
            memset(out, 0, framesPerBuffer * sizeof(int16_t));
        }

        return paContinue;
    }

    PaStream* input_stream_;
    PaStream* output_stream_;
    std::vector<char> input_buffer_;
    std::deque<AudioPacket> output_buffer_;
    std::mutex input_mutex_;
    std::mutex output_mutex_;
};
```

