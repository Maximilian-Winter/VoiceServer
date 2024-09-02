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

        // Find the maximum packet size
        size_t maxSampleCount = 0;
        for (const auto& packet : packets) {
            size_t packetSampleCount = packet.size() / sizeof(int16_t);
            maxSampleCount = std::max(maxSampleCount, packetSampleCount);
        }

        std::vector<int32_t> mixBuffer(maxSampleCount, 0);
        std::vector<int> sampleCounts(maxSampleCount, 0);

        // Mix all packets
        for (const auto& packet : packets) {
            const int16_t* samples = reinterpret_cast<const int16_t*>(packet.data());
            size_t packetSampleCount = packet.size() / sizeof(int16_t);

            for (size_t i = 0; i < packetSampleCount; ++i) {
                mixBuffer[i] += static_cast<int32_t>(samples[i]);
                sampleCounts[i]++;
            }
        }

        // Normalize and clip
        std::vector<int16_t> outputBuffer(maxSampleCount);
        for (size_t i = 0; i < maxSampleCount; ++i) {
            if (sampleCounts[i] > 0) {
                int32_t sample = mixBuffer[i] / static_cast<int32_t>(sampleCounts[i]);
                const float headroom_factor = 0.5f;  // Adjust as needed
                int32_t scaled_sample = static_cast<int32_t>(sample * headroom_factor);
                outputBuffer[i] = static_cast<int16_t>(std::clamp(scaled_sample, INT16_MIN, static_cast<int32_t>(INT16_MAX)));
            } else {
                outputBuffer[i] = 0;
            }
        }

        return AudioPacket(reinterpret_cast<const uint8_t*>(outputBuffer.data()), outputBuffer.size() * sizeof(int16_t));
    }
};