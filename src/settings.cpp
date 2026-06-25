#include "settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
std::string config_home() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return xdg;
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.config";
    }
    return ".";
}

std::string value_after(const std::string& line, const std::string& key) {
    const std::string prefix = key + "=";
    if (line.rfind(prefix, 0) == 0) {
        return line.substr(prefix.size());
    }
    return {};
}

int parse_int_or(const std::string& value, int fallback) {
    try {
        size_t parsed = 0;
        const int result = std::stoi(value, &parsed);
        return parsed == value.size() ? result : fallback;
    } catch (...) {
        return fallback;
    }
}

float parse_float_or(const std::string& value, float fallback) {
    try {
        size_t parsed = 0;
        const double result = std::stod(value, &parsed);
        if (parsed != value.size()) return fallback;
        return static_cast<float>(result);
    } catch (...) {
        return fallback;
    }
}
}

std::string settings_path() {
    return config_home() + "/tabel0504421/settings.ini";
}

UserSettings load_settings() {
    UserSettings settings;
    std::ifstream in(settings_path());
    std::string line;
    while (std::getline(in, line)) {
        if (auto v = value_after(line, "last_database"); !v.empty()) settings.last_database = v;
        if (auto v = value_after(line, "last_backup_database"); !v.empty()) settings.last_backup_database = v;
        if (auto v = value_after(line, "last_backup_month"); !v.empty()) settings.last_backup_month = v;
        if (auto v = value_after(line, "recent_database"); !v.empty()) settings.recent_databases.push_back(v);
        if (auto v = value_after(line, "auto_backup_enabled"); !v.empty()) settings.auto_backup_enabled = parse_int_or(v, settings.auto_backup_enabled ? 1 : 0) != 0;
        if (auto v = value_after(line, "theme"); !v.empty()) settings.theme = parse_int_or(v, settings.theme);
        if (auto v = value_after(line, "font_size"); !v.empty()) settings.font_size = parse_float_or(v, settings.font_size);
        if (auto v = value_after(line, "left_split"); !v.empty()) settings.left_split = parse_float_or(v, settings.left_split);
        if (auto v = value_after(line, "last_year"); !v.empty()) settings.last_year = parse_int_or(v, settings.last_year);
        if (auto v = value_after(line, "last_month"); !v.empty()) settings.last_month = parse_int_or(v, settings.last_month);
    }
    settings.theme = std::clamp(settings.theme, 0, 7);
    settings.font_size = std::clamp(settings.font_size, 14.0f, 24.0f);
    settings.left_split = std::clamp(settings.left_split, 0.20f, 0.70f);
    settings.last_month = std::clamp(settings.last_month, 1, 12);
    if (!settings.last_database.empty() && std::find(settings.recent_databases.begin(), settings.recent_databases.end(), settings.last_database) == settings.recent_databases.end()) {
        settings.recent_databases.insert(settings.recent_databases.begin(), settings.last_database);
    }
    if (settings.recent_databases.size() > 10) settings.recent_databases.resize(10);
    return settings;
}

void save_settings(const UserSettings& settings) {
    const auto path = std::filesystem::path(settings_path());
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "last_database=" << settings.last_database << '\n';
    out << "last_backup_database=" << settings.last_backup_database << '\n';
    out << "last_backup_month=" << settings.last_backup_month << '\n';
    out << "auto_backup_enabled=" << (settings.auto_backup_enabled ? 1 : 0) << '\n';
    for (const auto& recent : settings.recent_databases) {
        if (!recent.empty()) out << "recent_database=" << recent << '\n';
    }
    out << "theme=" << settings.theme << '\n';
    out << "font_size=" << std::clamp(settings.font_size, 14.0f, 24.0f) << '\n';
    out << "left_split=" << std::clamp(settings.left_split, 0.20f, 0.70f) << '\n';
    out << "last_year=" << settings.last_year << '\n';
    out << "last_month=" << settings.last_month << '\n';
}
