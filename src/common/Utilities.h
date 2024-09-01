//
// Created by maxim on 23.08.2024.
//
#pragma once

#include <array>
#include <random>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace Utilities
{
    std::string generateUuid() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<> dis(0, 255);

        std::array<unsigned char, 16> bytes;
        for (unsigned char& byte : bytes) {
            byte = dis(gen);
        }

        // Set version to 4
        bytes[6] = (bytes[6] & 0x0F) | 0x40;
        // Set variant to 1
        bytes[8] = (bytes[8] & 0x3F) | 0x80;

        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                ss << '-';
            }
            ss << std::setw(2) << static_cast<int>(bytes[i]);
        }

        std::string uuid = ss.str();

        return uuid;
    }
}

namespace Base64Utilities
{
    std::string from_base64(const std::string& input)
    {
        static const std::string b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static std::unordered_map<char, int> b64_index;
        if (b64_index.empty()) {
            for (int i = 0; i < 64; ++i) {
                b64_index[b64_chars[i]] = i;
            }
        }

        std::string result;
        int val = 0;
        int val_b = -8;
        for (char c : input) {
            if (c == '=') break;
            if (b64_index.find(c) == b64_index.end()) continue;

            val = (val << 6) + b64_index[c];
            val_b += 6;
            if (val_b >= 0) {
                result.push_back(char((val >> val_b) & 0xFF));
                val_b -= 8;
            }
        }

        return result;
    }

    std::string to_base_64(const std::string& input)
    {
        // Convert hash to base64
        static const char* b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        unsigned int val = 0;
        int val_b = -6;
        for (int i = 0; i < input.length(); i += 2) {
            unsigned char c = std::stoi(input.substr(i, 2), nullptr, 16);
            val = (val << 8) + c;
            val_b += 8;
            while (val_b >= 0) {
                result.push_back(b64_table[(val >> val_b) & 0x3F]);
                val_b -= 6;
            }
        }
        if (val_b > -6) result.push_back(b64_table[((val << 8) >> (val_b + 8)) & 0x3F]);
        while (result.size() % 4) result.push_back('=');

        return result;
    }
}

