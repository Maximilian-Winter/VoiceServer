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