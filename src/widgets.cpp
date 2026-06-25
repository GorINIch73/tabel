#include "widgets.hpp"

#include "calendar.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <unordered_map>

namespace {
std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string digits_only(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        if (std::isdigit(c)) out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string normalize_date_text(const std::string& value) {
    const std::string digits = digits_only(value);
    if (digits.size() == 8) {
        return digits.substr(0, 4) + "-" + digits.substr(4, 2) + "-" + digits.substr(6, 2);
    }
    return value;
}

bool parse_iso_date(const std::string& value, int& year, int& month, int& day) {
    return std::sscanf(value.c_str(), "%d-%d-%d", &year, &month, &day) == 3 &&
        year > 1900 && month >= 1 && month <= 12 && day >= 1 && day <= days_in_month(year, month);
}

int first_weekday_monday0(int year, int month) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    std::mktime(&t);
    return (t.tm_wday + 6) % 7;
}

struct DatePickerState {
    int year = 0;
    int month = 0;
};
}

bool splitter(const char* id, float* left_width, float min_left, float min_right) {
    ImGui::SameLine();
    ImGui::PushID(id);
    ImGui::InvisibleButton("splitter", ImVec2(7.0f, -1.0f));
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered() || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active) {
        *left_width += ImGui::GetIO().MouseDelta.x;
        const float available = ImGui::GetContentRegionAvail().x + *left_width;
        *left_width = std::clamp(*left_width, min_left, available - min_right);
    }
    ImGui::PopID();
    ImGui::SameLine();
    return active;
}

bool horizontal_splitter(const char* id, float* top_height, float min_top, float min_bottom) {
    ImGui::PushID(id);
    ImGui::InvisibleButton("splitter", ImVec2(-1.0f, 7.0f));
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered() || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (active) {
        *top_height += ImGui::GetIO().MouseDelta.y;
        const float available = ImGui::GetContentRegionAvail().y + *top_height;
        *top_height = std::clamp(*top_height, min_top, available - min_bottom);
    }
    ImGui::PopID();
    return active;
}

bool input_text(const char* label, std::string& value, float width) {
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    return ImGui::InputText(label, &value);
}

bool input_date(const char* label, std::string& value) {
    bool changed = false;
    ImGui::PushID(label);
    const ImGuiID popup_id = ImGui::GetID("date_popup");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputText("##date", &value)) {
        const std::string normalized = normalize_date_text(value);
        if (normalized != value) value = normalized;
        changed = true;
    }
    ImGui::SameLine();
    static std::unordered_map<ImGuiID, DatePickerState> picker_states;
    if (ImGui::Button("##open_date_picker")) {
        int open_year = 0;
        int open_month = 0;
        int open_day = 0;
        if (!parse_iso_date(value, open_year, open_month, open_day)) {
            std::time_t now = std::time(nullptr);
            std::tm* local = std::localtime(&now);
            open_year = local ? local->tm_year + 1900 : 2026;
            open_month = local ? local->tm_mon + 1 : 1;
        }
        picker_states[popup_id] = DatePickerState{open_year, open_month};
        ImGui::OpenPopup("date_popup");
    }

    if (ImGui::BeginPopup("date_popup")) {
        DatePickerState& picker = picker_states[popup_id];
        if (picker.year == 0 || picker.month == 0) {
            int selected_year = 0;
            int selected_month = 0;
            int selected_day = 0;
            if (parse_iso_date(value, selected_year, selected_month, selected_day)) {
                picker = DatePickerState{selected_year, selected_month};
            } else {
                std::time_t now = std::time(nullptr);
                std::tm* local = std::localtime(&now);
                picker = DatePickerState{local ? local->tm_year + 1900 : 2026, local ? local->tm_mon + 1 : 1};
            }
        }
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Год", &picker.year);
        picker.year = std::clamp(picker.year, 1901, 2200);
        ImGui::SameLine();
        if (ImGui::ArrowButton("prev_month", ImGuiDir_Left)) {
            --picker.month;
            if (picker.month < 1) { picker.month = 12; --picker.year; }
            picker.year = std::clamp(picker.year, 1901, 2200);
        }
        ImGui::SameLine();
        ImGui::Text("%02d", picker.month);
        ImGui::SameLine();
        if (ImGui::ArrowButton("next_month", ImGuiDir_Right)) {
            ++picker.month;
            if (picker.month > 12) { picker.month = 1; ++picker.year; }
            picker.year = std::clamp(picker.year, 1901, 2200);
        }
        ImGui::Separator();
        static constexpr const char* weekdays[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
        if (ImGui::BeginTable("calendar_days", 7, ImGuiTableFlags_SizingFixedFit)) {
            for (const char* weekday : weekdays) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(weekday);
            }
            const int offset = first_weekday_monday0(picker.year, picker.month);
            const int count = days_in_month(picker.year, picker.month);
            for (int cell = 0; cell < offset + count; ++cell) {
                ImGui::TableNextColumn();
                if (cell < offset) {
                    ImGui::TextUnformatted("");
                    continue;
                }
                const int pick_day = cell - offset + 1;
                int selected_year = 0;
                int selected_month = 0;
                int selected_day = 0;
                const bool selected = parse_iso_date(value, selected_year, selected_month, selected_day) &&
                    selected_year == picker.year && selected_month == picker.month && selected_day == pick_day;
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::SmallButton(std::to_string(pick_day).c_str())) {
                    value = iso_date(picker.year, picker.month, pick_day);
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (selected) ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool input_multiline_wrapped(const char* label, std::string& value, float height) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    const bool changed = ImGui::InputTextMultiline(label, &value, ImVec2(-1.0f, height));
    ImGui::PopTextWrapPos();
    return changed;
}

bool employee_filter_combo(const char* label, const std::vector<Employee>& employees, int& selected_id, std::string& filter) {
    const char* preview = "Выберите сотрудника";
    for (const auto& e : employees) {
        if (e.id == selected_id) {
            preview = e.full_name.c_str();
            break;
        }
    }

    bool changed = false;
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge)) {
        input_text("Фильтр", filter);
        const std::string needle = lower_ascii(filter);
        ImGui::Separator();
        for (const auto& e : employees) {
            const std::string hay = lower_ascii(e.full_name + " " + e.position + " " + e.department);
            if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
            const bool selected = e.id == selected_id;
            if (ImGui::Selectable(e.full_name.c_str(), selected)) {
                selected_id = e.id;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
