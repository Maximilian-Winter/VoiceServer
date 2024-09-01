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
        std::vector<int32_t> mixBuffer(sampleCount, 0);

        // Mix all packets
        for (const auto& packet : packets) {
            const int16_t* samples = reinterpret_cast<const int16_t*>(packet.data());
            for (size_t i = 0; i < sampleCount; ++i) {
                mixBuffer[i] += static_cast<int32_t>(samples[i]);
            }
        }

        // Normalize and clip
        std::vector<int16_t> outputBuffer(sampleCount);
        for (size_t i = 0; i < sampleCount; ++i) {
            int32_t sample = mixBuffer[i] / static_cast<int32_t>(packets.size());
            outputBuffer[i] = static_cast<int16_t>(std::clamp(sample, -16384, 16383));
        }

        return AudioPacket(reinterpret_cast<const uint8_t*>(outputBuffer.data()), outputBuffer.size() * sizeof(int16_t));
    }
};