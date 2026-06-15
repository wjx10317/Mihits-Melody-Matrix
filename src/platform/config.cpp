#include "config.h"
#include "util/logger.h"
#include "util/error_codes.h"

#include <fstream>
#include <sstream>

namespace melody_matrix::platform {

util::Result<void> Config::load(const std::string& path) {
    s_path = path;
    s_data.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        // Config file doesn't exist yet — use defaults
        MM_LOG_INFO("Config", "Config file not found, using defaults: " + path);
        s_loaded = true;
        return util::success();
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        // Trim whitespace
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        s_data[key] = value;
    }

    s_loaded = true;
    MM_LOG_INFO("Config", "Config loaded: " + path);
    return util::success();
}

util::Result<void> Config::save() {
    std::ofstream file(s_path);
    if (!file.is_open()) {
        return util::failure<void>(static_cast<int32_t>(util::ErrorCode::ERROR_IO),
                                    "Failed to save config: " + s_path);
    }

    file << "# Melody Matrix Configuration\n";
    file << "# Auto-generated — edit at your own risk\n\n";
    for (const auto& [key, value] : s_data) {
        file << key << "=" << value << "\n";
    }
    file.flush();

    MM_LOG_INFO("Config", "Config saved: " + s_path);
    return util::success();
}

int32_t Config::getInt(const std::string& key, int32_t defaultValue) {
    auto it = s_data.find(key);
    if (it == s_data.end()) return defaultValue;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

float Config::getFloat(const std::string& key, float defaultValue) {
    auto it = s_data.find(key);
    if (it == s_data.end()) return defaultValue;
    try {
        return std::stof(it->second);
    } catch (...) {
        return defaultValue;
    }
}

std::string Config::getString(const std::string& key, const std::string& defaultValue) {
    auto it = s_data.find(key);
    return (it != s_data.end()) ? it->second : defaultValue;
}

void Config::setInt(const std::string& key, int32_t value) {
    s_data[key] = std::to_string(value);
}

void Config::setFloat(const std::string& key, float value) {
    s_data[key] = std::to_string(value);
}

void Config::setString(const std::string& key, const std::string& value) {
    s_data[key] = value;
}

} // namespace melody_matrix::platform
