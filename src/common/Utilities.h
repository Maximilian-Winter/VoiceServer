//
// Created by maxim on 23.08.2024.
//
#pragma once

#include <array>
#include <random>
#include <sstream>
#include <iomanip>


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

