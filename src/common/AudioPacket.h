//
// Created by maxim on 31.08.2024.
//
#pragma once

#include <vector>


class AudioPacket {
public:
    AudioPacket() = default;
    AudioPacket(const uint8_t* data, size_t size) : data_(data, data + size) {}

    [[nodiscard]] const uint8_t* data() const { return data_.data(); }

    [[nodiscard]] const std::vector<uint8_t>& data_vector() const { return data_; }

    [[nodiscard]] size_t size() const { return data_.size(); }

    [[nodiscard]] bool empty() const { return data_.empty(); }

private:
    std::vector<uint8_t> data_;
};