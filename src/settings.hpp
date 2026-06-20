#pragma once

#include <string>
#include <vector>

struct UserSettings {
    std::string last_database;
    std::vector<std::string> recent_databases;
    int theme = 0;
    float left_split = 0.36f;
    int last_year = 2026;
    int last_month = 6;
};

std::string settings_path();
UserSettings load_settings();
void save_settings(const UserSettings& settings);
