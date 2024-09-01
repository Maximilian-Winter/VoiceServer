#pragma once

#include <AudioPacket.h>
#include <mutex>
#include <portaudio.h>
#include <deque>
#include <chrono>
#include <cstring>

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
    static constexpr int FRAMES_PER_BUFFER = 4096;  // Fixed buffer size
    static constexpr int MAX_BUFFER_SIZE = 10;  // Maximum number of packets to buffer
    static constexpr float SMOOTHING_FACTOR = 0.5f;  // Smoothing factor for cross-fading

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

            // Apply smoothing to reduce clicking
            for (size_t i = 0; i < bytesToCopy / sizeof(int16_t); ++i) {
                float current = static_cast<float>(reinterpret_cast<const int16_t*>(packet.data())[i]);
                float prev = (i > 0) ? static_cast<float>(out[i-1]) : 0.0f;
                out[i] = static_cast<int16_t>(prev + SMOOTHING_FACTOR * (current - prev));
            }

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