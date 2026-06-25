#pragma once

#include <string>
#include <vector>

struct UserSettings {
    std::string last_database;
    std::string last_backup_database;
    std::string last_backup_month;
    std::vector<std::string> recent_databases;
    bool auto_backup_enabled = true;
    int theme = 0;
    float font_size = 18.0f;
    float left_split = 0.36f;
    int last_year = 2026;
    int last_month = 6;
};

std::string settings_path();
UserSettings load_settings();
void save_settings(const UserSettings& settings);
