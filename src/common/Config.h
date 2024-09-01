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
