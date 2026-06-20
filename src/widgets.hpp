#pragma once

#include "models.hpp"

#include <string>
#include <vector>

bool splitter(const char* id, float* left_width, float min_left, float min_right);
bool horizontal_splitter(const char* id, float* top_height, float min_top, float min_bottom);
bool input_text(const char* label, std::string& value, float width = -1.0f);
bool input_date(const char* label, std::string& value);
bool input_multiline_wrapped(const char* label, std::string& value, float height);
bool employee_filter_combo(const char* label, const std::vector<Employee>& employees, int& selected_id, std::string& filter);
void help_marker(const char* text);
