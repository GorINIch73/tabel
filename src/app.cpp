#define IMGUI_DEFINE_MATH_OPERATORS

#include "app.hpp"

#include "calendar.hpp"
#include "fonts.hpp"
#include "widgets.hpp"

#include <ImGuiFileDialog.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <zip.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <set>

namespace {
const char* month_name(int month) {
    static constexpr const char* names[] = {
        "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
        "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"
    };
    return names[month - 1];
}

void set_next_width() {
    ImGui::SetNextItemWidth(-1.0f);
}

template <typename T>
T* find_by_id(std::vector<T>& rows, int id) {
    auto it = std::find_if(rows.begin(), rows.end(), [id](const T& row) { return row.id == id; });
    return it == rows.end() ? nullptr : &*it;
}

bool works_on_date(const Employee& employee, const std::string& date) {
    if (!employee.hire_date.empty() && date < employee.hire_date) return false;
    if (!employee.dismissal_date.empty() && date > employee.dismissal_date) return false;
    return true;
}

CalendarDay* find_day(std::vector<CalendarDay>& days, const std::string& date) {
    auto it = std::find_if(days.begin(), days.end(), [&date](const CalendarDay& day) { return day.date == date; });
    return it == days.end() ? nullptr : &*it;
}

int month_from_date(const std::string& date) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return 0;
    return month;
}

int first_weekday_monday0(int year, int month) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = 1;
    std::mktime(&t);
    return (t.tm_wday + 6) % 7;
}

bool is_friday(const std::string& date) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return false;
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    std::mktime(&t);
    return t.tm_wday == 5;
}

double work_hours_for_day(const WorkNorm* norm, const CalendarDay& day) {
    if (!norm) return 8.0;
    double hours = norm->hours_per_day;
    if (norm->short_friday) {
        hours += is_friday(day.date) ? -0.8 : 0.2;
    }
    if (day.shortened) {
        hours -= 1.0;
    }
    return std::max(0.0, hours);
}

std::string compact_database_label(const Database& db) {
    if (!db.is_open()) return "База не выбрана";
    const std::filesystem::path path(db.path());
    const std::string filename = path.filename().string();
    return filename.empty() ? db.path() : filename;
}

std::string html_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string format_hours(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

std::string format_int_or_decimal(double value) {
    if (std::abs(value - static_cast<int>(value)) < 0.0001) {
        return std::to_string(static_cast<int>(value));
    }
    return format_hours(value);
}

std::string format_hours_or_empty(double value) {
    if (std::abs(value) < 0.0001) return "";
    return format_int_or_decimal(value);
}

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out += "'";
    return out;
}

std::string current_month_key() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (std::tm* tm = std::localtime(&now)) local = *tm;
    std::ostringstream out;
    out << std::setw(4) << std::setfill('0') << (local.tm_year + 1900)
        << '-' << std::setw(2) << std::setfill('0') << (local.tm_mon + 1);
    return out.str();
}

std::filesystem::path monthly_backup_path(const std::string& database_path, const std::string& month_key) {
    const std::filesystem::path source = std::filesystem::absolute(database_path);
    const std::filesystem::path backup_dir = source.parent_path() / "backups";
    const std::string extension = source.extension().empty() ? ".sqlite3" : source.extension().string();
    return backup_dir / (source.stem().string() + "_backup_" + month_key + extension);
}

bool add_zip_text(zip_t* archive, const char* name, const std::string& text, bool store, std::string& error) {
    void* buffer = std::malloc(text.size());
    if (!buffer && !text.empty()) {
        error = "Не удалось выделить память для файла ODS: " + std::string(name);
        return false;
    }
    if (!text.empty()) std::memcpy(buffer, text.data(), text.size());
    zip_source_t* source = zip_source_buffer(archive, buffer, text.size(), 1);
    if (!source) {
        std::free(buffer);
        error = "Не удалось подготовить файл ODS: " + std::string(name);
        return false;
    }
    const zip_int64_t index = zip_file_add(archive, name, source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        error = "Не удалось добавить файл в ODS: " + std::string(name);
        return false;
    }
    if (store) {
        zip_set_file_compression(archive, static_cast<zip_uint64_t>(index), ZIP_CM_STORE, 0);
    }
    return true;
}

std::filesystem::path ots_template_path() {
    const std::filesystem::path relative = std::filesystem::path("assets") / "0504421.ots";
    if (std::filesystem::exists(relative)) return relative;

#ifdef TABEL0504421_DATA_DIR
    const std::filesystem::path installed = std::filesystem::path(TABEL0504421_DATA_DIR) / "assets" / "0504421.ots";
    if (std::filesystem::exists(installed)) return installed;
#endif

    std::error_code ec;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable.empty()) {
        const std::filesystem::path executable_dir = executable.parent_path();
        const std::filesystem::path near_executable = executable_dir / "assets" / "0504421.ots";
        if (std::filesystem::exists(near_executable)) return near_executable;
        const std::filesystem::path project_relative = executable_dir / ".." / "assets" / "0504421.ots";
        if (std::filesystem::exists(project_relative)) return project_relative;
    }

    return relative;
}

bool read_zip_text(zip_t* archive, const char* name, std::string& text, std::string& error) {
    zip_stat_t stat{};
    zip_stat_init(&stat);
    if (zip_stat(archive, name, 0, &stat) != 0) {
        error = "В OTS-шаблоне нет файла: " + std::string(name);
        return false;
    }
    zip_file_t* file = zip_fopen(archive, name, 0);
    if (!file) {
        error = "Не удалось открыть файл OTS-шаблона: " + std::string(name);
        return false;
    }
    text.assign(static_cast<size_t>(stat.size), '\0');
    zip_int64_t read = 0;
    if (!text.empty()) read = zip_fread(file, text.data(), text.size());
    zip_fclose(file);
    if (read < 0 || static_cast<zip_uint64_t>(read) != stat.size) {
        error = "Не удалось прочитать файл OTS-шаблона: " + std::string(name);
        return false;
    }
    return true;
}

bool copy_zip_entry(zip_t* source_archive, zip_uint64_t index, zip_t* target_archive, std::string& error) {
    zip_stat_t stat{};
    zip_stat_init(&stat);
    if (zip_stat_index(source_archive, index, 0, &stat) != 0 || !stat.name) {
        error = "Не удалось прочитать элемент OTS-шаблона";
        return false;
    }
    const std::string name = stat.name;
    if (name == "content.xml" || name == "mimetype" || name == "META-INF/manifest.xml") return true;
    if (!name.empty() && name.back() == '/') {
        if (zip_dir_add(target_archive, name.c_str(), ZIP_FL_ENC_UTF_8) < 0) {
            error = "Не удалось добавить каталог в ODS: " + name;
            return false;
        }
        return true;
    }

    zip_file_t* file = zip_fopen_index(source_archive, index, 0);
    if (!file) {
        error = "Не удалось открыть элемент OTS-шаблона: " + name;
        return false;
    }
    std::string data(static_cast<size_t>(stat.size), '\0');
    zip_int64_t read = 0;
    if (!data.empty()) read = zip_fread(file, data.data(), data.size());
    zip_fclose(file);
    if (read < 0 || static_cast<zip_uint64_t>(read) != stat.size) {
        error = "Не удалось прочитать элемент OTS-шаблона: " + name;
        return false;
    }
    return add_zip_text(target_archive, name.c_str(), data, false, error);
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool has_employee_detail_markers(const std::string& row) {
    return row.find("_HOURS}}") != std::string::npos ||
           row.find("{{WORKED_HOURS}}") != std::string::npos ||
           row.find("{{MISSED_HOURS}}") != std::string::npos;
}

size_t employee_template_block_end(const std::string& content, size_t first_row_end) {
    size_t block_end = first_row_end;
    const size_t next_row_start = content.find("<table:table-row", first_row_end);
    if (next_row_start == std::string::npos) return block_end;

    const size_t next_row_end_tag = content.find("</table:table-row>", next_row_start);
    if (next_row_end_tag == std::string::npos) return block_end;

    const size_t next_row_end = next_row_end_tag + std::string("</table:table-row>").size();
    const std::string next_row = content.substr(next_row_start, next_row_end - next_row_start);
    if (has_employee_detail_markers(next_row)) {
        block_end = next_row_end;
    }

    return block_end;
}

void replace_day_marker(std::string& row, int day_number, const std::string& suffix, const std::string& value) {
    char day_key[3]{};
    std::snprintf(day_key, sizeof(day_key), "%02d", day_number);
    replace_all(row, std::string("{{D") + day_key + suffix + "}}", value);

    if (suffix == "_CODE") {
        replace_all(row, std::string("{{D") + day_key + "1_CODE}}", value);
        replace_all(row, std::string("{{D") + day_key + "CODE}}", value);
    }
}

void ods_string_cell(std::ostream& out, const std::string& value, const char* style = nullptr) {
    out << "<table:table-cell office:value-type=\"string\"";
    if (style) out << " table:style-name=\"" << style << "\"";
    out << "><text:p>" << html_escape(value) << "</text:p></table:table-cell>";
}

void ods_number_cell(std::ostream& out, double value, const char* style = nullptr) {
    out << "<table:table-cell office:value-type=\"float\" office:value=\"" << format_hours(value) << "\"";
    if (style) out << " table:style-name=\"" << style << "\"";
    out << "><text:p>" << format_int_or_decimal(value) << "</text:p></table:table-cell>";
}

void ods_empty_cell(std::ostream& out, const char* style = nullptr) {
    out << "<table:table-cell";
    if (style) out << " table:style-name=\"" << style << "\"";
    out << "/>";
}

void ods_covered_cells(std::ostream& out, int count) {
    for (int i = 0; i < count; ++i) out << "<table:covered-table-cell/>";
}

bool confirm_delete_button(const char* id, const char* target) {
    bool confirmed = false;
    const std::string button = std::string(" Удалить##") + id;
    const std::string popup = std::string("Подтверждение удаления##") + id;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.20f, 0.18f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.24f, 0.22f, 0.86f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.14f, 0.13f, 0.94f));
    if (ImGui::Button(button.c_str())) {
        ImGui::OpenPopup(popup.c_str());
    }
    ImGui::PopStyleColor(3);
    if (ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Удалить запись: %s?", target);
        ImGui::TextUnformatted("Действие нельзя отменить.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.20f, 0.18f, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.24f, 0.22f, 0.86f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.14f, 0.13f, 0.94f));
        if (ImGui::Button("Удалить", ImVec2(120.0f, 0.0f))) {
            confirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return confirmed;
}

struct CurrentMonthState {
    int year = 0;
    int month = 0;
    int documents = 0;
    bool accepted = false;
    bool needs_refill = false;
    int expected_cells = 0;
    int existing_cells = 0;
};

CurrentMonthState current_month_state(
    const Database& db,
    const std::vector<TimesheetDocument>& documents,
    const std::vector<Employee>& employees
) {
    CurrentMonthState state;
    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (std::tm* tm = std::localtime(&now)) local = *tm;
    state.year = local.tm_year + 1900;
    state.month = local.tm_mon + 1;

    for (const auto& document : documents) {
        if (document.year != state.year || document.month != state.month) continue;
        ++state.documents;
        state.accepted = state.accepted || document.accepted;
        state.needs_refill = state.needs_refill || document.needs_refill;
    }

    auto days = db.calendar_days(state.year, state.month);
    if (days.empty()) days = build_local_calendar(state.year, state.month);
    for (const auto& employee : employees) {
        if (!employee.active) continue;
        for (const auto& day : days) {
            if (!works_on_date(employee, day.date)) continue;
            ++state.expected_cells;
            if (db.timesheet_cell(employee.id, day.date).has_value()) {
                ++state.existing_cells;
            }
        }
    }
    return state;
}

void dashboard_metric(const char* label, const std::string& value, const std::string& hint, ImVec4 accent, ImVec4 bg) {
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.55f));
    ImGui::BeginChild(label, ImVec2(0.0f, 104.0f), ImGuiChildFlags_Border);
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(window_pos, ImVec2(window_pos.x + 5.0f, window_pos.y + window_size.y),
        ImGui::ColorConvertFloat4ToU32(accent), 2.0f);
    ImGui::Indent(12.0f);
    ImGui::TextDisabled("%s", label);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(accent.x, accent.y, accent.z, 1.0f));
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    if (!hint.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", hint.c_str());
    }
    ImGui::Unindent(12.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void shifted_month(int year, int month, int delta, int& out_year, int& out_month) {
    out_year = year;
    out_month = month + delta;
    while (out_month < 1) {
        out_month += 12;
        --out_year;
    }
    while (out_month > 12) {
        out_month -= 12;
        ++out_year;
    }
}

void dashboard_month_calendar(
    int year,
    int month,
    const std::vector<CalendarDay>& calendar_days,
    bool prominent
) {
    static constexpr const char* weekdays[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
    std::vector<CalendarDay> days = calendar_days.empty() ? build_local_calendar(year, month) : calendar_days;

    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (std::tm* tm = std::localtime(&now)) local = *tm;
    const std::string today = iso_date(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);

    ImGui::PushID(year * 100 + month);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, prominent ? ImVec4(0.11f, 0.56f, 0.70f, 0.10f) : ImVec4(0.12f, 0.14f, 0.15f, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_Border, prominent ? ImVec4(0.11f, 0.56f, 0.70f, 0.58f) : ImVec4(0.36f, 0.42f, 0.46f, 0.38f));
    ImGui::BeginChild("dashboard_month_calendar", ImVec2(288.0f, 286.0f), ImGuiChildFlags_Border);

    ImGui::Text("%s %d", month_name(month), year);
    ImGui::Separator();

    ImGui::TextDisabled("Рабочие дни");
    ImGui::SameLine();
    ImGui::ColorButton("##workday_color", ImVec4(0.20f, 0.34f, 0.38f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("Вых.");
    ImGui::SameLine();
    ImGui::ColorButton("##weekend_color", ImVec4(0.34f, 0.40f, 0.50f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("Пр.");
    ImGui::SameLine();
    ImGui::ColorButton("##holiday_color", ImVec4(0.58f, 0.18f, 0.18f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("Сокр.");
    ImGui::SameLine();
    ImGui::ColorButton("##shortened_color", ImVec4(0.76f, 0.52f, 0.12f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));

    ImGui::Spacing();
    const float cell_size = 28.0f;
    if (ImGui::BeginTable("dashboard_month_days", 7, ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoHostExtendX)) {
        for (int column = 0; column < 7; ++column) {
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, cell_size);
        }
        for (const char* weekday : weekdays) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", weekday);
        }

        const int offset = first_weekday_monday0(year, month);
        const int count = days_in_month(year, month);
        const int rows = (offset + count + 6) / 7;
        for (int cell = 0; cell < rows * 7; ++cell) {
            ImGui::TableNextColumn();
            if (cell < offset || cell >= offset + count) {
                ImGui::Dummy(ImVec2(cell_size, cell_size));
                continue;
            }

            const int day_number = cell - offset + 1;
            const std::string date = iso_date(year, month, day_number);
            const auto it = std::find_if(days.begin(), days.end(), [&date](const CalendarDay& day) { return day.date == date; });
            const CalendarDay* day = it == days.end() ? nullptr : &*it;

            ImVec4 color = ImVec4(0.20f, 0.34f, 0.38f, 0.94f);
            if (day && day->weekend) {
                color = ImVec4(0.34f, 0.40f, 0.50f, 0.94f);
            }
            if (day && day->holiday) {
                color = ImVec4(0.58f, 0.18f, 0.18f, 0.94f);
            }
            if (day && day->shortened) {
                color = ImVec4(0.76f, 0.52f, 0.12f, 0.94f);
            }
            if (!prominent) {
                color.w *= 0.44f;
            }

            ImGui::PushID(day_number);
            ImGui::Dummy(ImVec2(cell_size, cell_size));
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(color), 4.0f);
            const std::string day_label = std::to_string(day_number);
            const ImVec2 text_size = ImGui::CalcTextSize(day_label.c_str());
            draw_list->AddText(ImVec2(min.x + (cell_size - text_size.x) * 0.5f, min.y + (cell_size - text_size.y) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(prominent ? ImVec4(1.0f, 1.0f, 1.0f, 0.96f) : ImVec4(1.0f, 1.0f, 1.0f, 0.48f)), day_label.c_str());
            if (date == today) {
                ImGui::GetWindowDrawList()->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f)), 3.0f, 0, 2.0f);
            }
            if (day && ImGui::BeginItemTooltip()) {
                ImGui::Text("%s", date.c_str());
                if (day->holiday) ImGui::TextUnformatted("Праздник");
                else if (day->weekend) ImGui::TextUnformatted("Выходной");
                else ImGui::TextUnformatted("Рабочий день");
                if (day->shortened) ImGui::TextUnformatted("Сокращенный день");
                if (!day->comment.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", day->comment.c_str());
                }
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void warning_banner(const char* icon, const char* title, const char* detail) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.95f, 0.72f, 0.12f, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.72f, 0.12f, 0.46f));
    ImGui::BeginChild(title, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::Text("%s %s", icon, title);
    ImGui::TextWrapped("%s", detail);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

}

App::App() {
    settings_ = load_settings();
    context_.year = settings_.last_year;
    context_.month = settings_.last_month;
    apply_theme();
    apply_font_size();
    if (!settings_.last_database.empty() && std::filesystem::exists(settings_.last_database)) {
        open_database(settings_.last_database);
    }
}

App::~App() {
    settings_.last_year = context_.year;
    settings_.last_month = context_.month;
    save_settings(settings_);
}

void App::apply_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    switch (settings_.theme) {
        case 1:
            ImGui::StyleColorsLight();
            break;
        case 2:
            ImGui::StyleColorsClassic();
            break;
        case 3:
            ImGui::StyleColorsLight();
            break;
        case 5:
            ImGui::StyleColorsLight();
            break;
        default:
            ImGui::StyleColorsDark();
            break;
    }

    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding = ImVec2(9.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);

    auto& c = style.Colors;
    if (settings_.theme == 0) {
        c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.12f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.20f, 0.34f, 0.38f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.43f, 0.48f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.18f, 0.31f, 0.35f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.45f, 0.49f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.52f, 0.47f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.18f, 0.35f, 0.38f, 1.0f);
    } else if (settings_.theme == 2) {
        c[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.28f, 0.24f, 0.20f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.36f, 0.29f, 0.22f, 0.78f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.36f, 0.26f, 0.86f);
        c[ImGuiCol_Button] = ImVec4(0.34f, 0.29f, 0.24f, 0.82f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.46f, 0.36f, 0.27f, 0.92f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.39f, 0.24f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.34f, 0.29f, 0.24f, 1.0f);
    } else if (settings_.theme == 3) {
        c[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.97f, 0.97f, 1.0f);
        c[ImGuiCol_Text] = ImVec4(0.08f, 0.09f, 0.10f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.32f, 0.46f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.58f, 0.52f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.45f, 0.58f, 0.72f);
        c[ImGuiCol_Button] = ImVec4(0.16f, 0.39f, 0.52f, 0.82f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.13f, 0.48f, 0.64f, 0.92f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.56f, 0.70f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.17f, 0.43f, 0.56f, 1.0f);
    } else if (settings_.theme == 4) {
        c[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.21f, 0.24f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.28f, 0.34f, 0.40f, 0.82f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.36f, 0.44f, 0.52f, 0.92f);
        c[ImGuiCol_Button] = ImVec4(0.24f, 0.29f, 0.34f, 0.92f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.41f, 0.48f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.46f, 0.57f, 0.66f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.24f, 0.31f, 0.37f, 1.0f);
    } else if (settings_.theme == 5) {
        c[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.97f, 0.94f, 1.0f);
        c[ImGuiCol_Text] = ImVec4(0.08f, 0.12f, 0.09f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.38f, 0.24f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.26f, 0.52f, 0.34f, 0.48f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.52f, 0.34f, 0.68f);
        c[ImGuiCol_Button] = ImVec4(0.20f, 0.42f, 0.27f, 0.82f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.52f, 0.32f, 0.92f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.62f, 0.35f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.25f, 0.48f, 0.32f, 1.0f);
    } else if (settings_.theme == 6) {
        c[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.08f, 0.09f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.38f, 0.12f, 0.16f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.46f, 0.16f, 0.22f, 0.82f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.20f, 0.28f, 0.92f);
        c[ImGuiCol_Button] = ImVec4(0.38f, 0.14f, 0.20f, 0.92f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.52f, 0.18f, 0.26f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.68f, 0.22f, 0.32f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.42f, 0.15f, 0.22f, 1.0f);
    } else if (settings_.theme == 7) {
        c[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.0f);
        c[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 0.25f, 0.42f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.00f, 0.36f, 0.58f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.50f, 0.78f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.00f, 0.32f, 0.54f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.48f, 0.78f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.66f, 0.96f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.00f, 0.40f, 0.64f, 1.0f);
    }
}

void App::apply_font_size() {
    ImGuiIO& io = ImGui::GetIO();
    use_project_font_size(io, settings_.font_size);
}

void App::open_database(const std::string& path) {
    std::string error;
    if (!db_.open(path, error)) {
        status_ = "Ошибка открытия базы";
        last_error_ = error;
        return;
    }
    settings_.last_database = path;
    settings_.recent_databases.erase(std::remove(settings_.recent_databases.begin(), settings_.recent_databases.end(), path), settings_.recent_databases.end());
    settings_.recent_databases.insert(settings_.recent_databases.begin(), path);
    if (settings_.recent_databases.size() > 10) settings_.recent_databases.resize(10);
    save_settings(settings_);
    refresh();
    std::string backup_error;
    const bool backup_ok = auto_backup_database(backup_error);
    status_ = "Открыта база: " + path;
    last_error_ = backup_ok ? "" : backup_error;
}

void App::create_database(const std::string& path) {
    open_database(path);
}

bool App::auto_backup_database(std::string& error) {
    if (!db_.is_open() || db_.path().empty()) return true;
    if (!settings_.auto_backup_enabled) return true;

    const std::string month_key = current_month_key();
    const std::string database_key = std::filesystem::absolute(db_.path()).lexically_normal().string();
    if (settings_.last_backup_database == database_key && settings_.last_backup_month == month_key) {
        return true;
    }

    const std::filesystem::path backup = monthly_backup_path(db_.path(), month_key);
    std::error_code ec;
    std::filesystem::create_directories(backup.parent_path(), ec);
    if (ec) {
        error = "Автобэкап не создан: " + ec.message();
        return false;
    }

    if (!std::filesystem::exists(backup, ec)) {
        std::string backup_error;
        if (!db_.save_as(backup.string(), backup_error)) {
            error = "Автобэкап не создан: " + backup_error;
            return false;
        }
    }

    settings_.last_backup_database = database_key;
    settings_.last_backup_month = month_key;
    save_settings(settings_);
    return true;
}

bool App::save_pending_changes(std::string& error) {
    if (!db_.is_open()) {
        error = "База не открыта";
        return false;
    }
    ImGui::ClearActiveID();

    if (edited_person_id_ > 0 && !db_.save_person(edited_person_, error)) return false;
    if (edited_position_id_ > 0 && !db_.save_position(edited_position_, error)) return false;
    if (edited_employee_id_ > 0 && !db_.save_employee(edited_employee_, error)) return false;
    if (edited_activity_id_ > 0 && !db_.save_activity(edited_activity_, error)) return false;
    if (edited_period_id_ > 0) {
        if (!db_.save_activity_period(edited_period_, error)) return false;
    }
    if (edited_norm_id_ > 0 && !db_.save_norm(edited_norm_, error)) return false;
    if (edited_timesheet_document_id_ > 0 && !db_.save_timesheet_document(edited_timesheet_document_, error)) return false;
    if (!db_.save_institution(edited_institution_, error)) return false;

    return true;
}

void App::save_database_as(const std::string& path) {
    if (path.empty()) return;
    std::string error;
    if (!save_pending_changes(error)) {
        autosave_status(false, error);
        return;
    }

    const std::filesystem::path target = path;
    const std::filesystem::path current = db_.path();
    std::error_code compare_ec;
    if (std::filesystem::exists(target, compare_ec) && std::filesystem::equivalent(target, current, compare_ec)) {
        refresh();
        status_ = "Изменения сохранены";
        last_error_.clear();
        return;
    }
    if (!target.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            autosave_status(false, "Не удалось создать каталог: " + ec.message());
            return;
        }
    }

    if (!db_.save_as(path, error)) {
        autosave_status(false, error);
        return;
    }
    open_database(path);
    status_ = "База сохранена как: " + path;
    last_error_.clear();
}

void App::refresh() {
    if (!db_.is_open()) return;
    persons_ = db_.persons();
    positions_ = db_.positions();
    employees_ = db_.employees();
    activities_ = db_.activities();
    periods_ = db_.activity_periods(context_.year, context_.month);
    all_periods_ = db_.all_activity_periods();
    upcoming_periods_ = db_.upcoming_activity_periods(12);
    norms_ = db_.norms();
    timesheet_documents_ = db_.timesheet_documents();
    edited_institution_ = db_.institution();
    month_days_ = db_.calendar_days(context_.year, context_.month);
    if (month_days_.empty()) {
        month_days_ = build_local_calendar(context_.year, context_.month);
        std::string error;
        for (const auto& day : month_days_) db_.save_calendar_day(day, error);
    }
    load_calendar_year(context_.year);
    if (selected_employee_id_ == 0 && !employees_.empty()) selected_employee_id_ = employees_.front().id;
}

void App::load_calendar_year(int year) {
    if (!db_.is_open()) return;
    calendar_year_days_.clear();
    std::string error;
    for (int month = 1; month <= 12; ++month) {
        auto days = db_.calendar_days(year, month);
        if (days.empty()) {
            days = build_local_calendar(year, month);
            for (const auto& day : days) {
                if (!db_.save_calendar_day(day, error)) {
                    autosave_status(false, error);
                    break;
                }
            }
        }
        calendar_year_days_.insert(calendar_year_days_.end(), days.begin(), days.end());
    }
    std::sort(calendar_year_days_.begin(), calendar_year_days_.end(), [](const CalendarDay& a, const CalendarDay& b) {
        return a.date < b.date;
    });
    calendar_year_loaded_ = year;
    if (selected_calendar_date_.empty() || selected_calendar_date_.substr(0, 4) != std::to_string(year)) {
        selected_calendar_date_ = iso_date(year, 1, 1);
    }
}

void App::autosave_status(bool ok, const std::string& error) {
    if (ok) {
        status_ = "Изменения сохранены";
        last_error_.clear();
    } else {
        status_ = "Ошибка сохранения";
        last_error_ = error;
    }
}

bool App::print_timesheet_html(const TimesheetDocument& document, std::string& output_path, std::string& error) {
    if (!db_.is_open()) {
        error = "База не открыта";
        return false;
    }
    if (document.id <= 0 || document.year <= 0 || document.month < 1 || document.month > 12) {
        error = "Выберите табель для печати";
        return false;
    }

    auto days = db_.calendar_days(document.year, document.month);
    if (days.empty()) days = build_local_calendar(document.year, document.month);
    const auto employees = db_.employees();
    const auto activities = db_.activities();
    const Institution institution = db_.institution();

    std::set<std::string> work_codes;
    for (const auto& activity : activities) {
        if (activity.affects_norm) work_codes.insert(activity.code);
    }

    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("tabel0504421_" + std::to_string(document.id) + "_" + std::to_string(document.year) + "_" +
            (document.month < 10 ? "0" : "") + std::to_string(document.month) + ".html");

    std::ofstream out(path);
    if (!out) {
        error = "Не удалось создать файл печатной формы: " + path.string();
        return false;
    }

    const std::string period_title = std::string(month_name(document.month)) + " " + std::to_string(document.year);
    out << R"html(<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<title>Табель 0504421</title>
<style>
@page { size: A4 landscape; margin: 10mm; }
* { box-sizing: border-box; }
body { font-family: "DejaVu Sans", Arial, sans-serif; color: #111; margin: 0; font-size: 10px; }
.toolbar { position: sticky; top: 0; padding: 8px 0; background: white; border-bottom: 1px solid #ddd; margin-bottom: 10px; }
.toolbar button { font-size: 14px; padding: 6px 12px; }
.form-header { display: grid; grid-template-columns: 1fr 170px; gap: 14px; align-items: start; }
.approval { text-align: right; font-size: 9px; line-height: 1.25; color: #333; margin-bottom: 8px; }
.code-table { width: 100%; border-collapse: collapse; table-layout: fixed; font-size: 9px; }
.code-table th, .code-table td { border: 1px solid #222; padding: 2px 4px; text-align: center; }
.code-table th { background: #f0f0f0; }
h1 { text-align: center; font-size: 15px; margin: 4px 0 2px; letter-spacing: 0.5px; }
.form-number { text-align: center; font-size: 11px; margin-bottom: 4px; }
.org-name { text-align: center; border-bottom: 1px solid #222; min-height: 18px; font-size: 12px; padding: 2px 8px; margin: 4px 0 1px; }
.hint { text-align: center; font-size: 8px; color: #555; margin-bottom: 5px; }
.meta { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 4px 18px; margin: 8px 0 10px; font-size: 10px; }
.line { border-bottom: 1px solid #222; min-height: 14px; display: inline-block; min-width: 120px; padding: 0 4px; }
.timesheet-table { width: 100%; border-collapse: collapse; table-layout: fixed; }
.timesheet-table th, .timesheet-table td { border: 1px solid #222; padding: 2px 3px; text-align: center; vertical-align: middle; }
.timesheet-table th { font-weight: 700; background: #f0f0f0; }
.employee { text-align: left; width: 145px; }
.personnel { width: 34px; }
.position { width: 76px; overflow-wrap: anywhere; word-break: normal; }
.small { font-size: 8px; color: #444; }
.day { width: 19px; }
.weekend { background: #eceff3; }
.holiday { background: #f8d7da; }
.shortened { background: #fff3cd; }
.out { color: #777; background: #f7f7f7; }
.code { font-weight: 700; line-height: 1.05; }
.hours { font-size: 8px; line-height: 1.05; }
.totals { width: 30px; }
.signatures { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 18px; margin-top: 18px; font-size: 10px; }
.signature-line { border-bottom: 1px solid #222; height: 18px; margin-top: 12px; }
@media print {
  .toolbar { display: none; }
  body { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
}
</style>
</head>
<body>
<div class="toolbar"><button onclick="window.print()">Печать</button></div>
)html";
    out << "<div class=\"form-header\"><div>";
    out << "<div class=\"approval\">Унифицированная форма<br>для учреждений государственного сектора</div>\n";
    out << "<div class=\"form-number\">Форма 0504421</div>\n";
    out << "<h1>ТАБЕЛЬ<br>учета использования рабочего времени</h1>\n";
    out << "<div class=\"org-name\">" << html_escape(institution.title) << "</div>\n";
    out << "<div class=\"hint\">наименование учреждения</div>\n";
    out << "</div><div><table class=\"code-table\"><tr><th>Коды</th><th></th></tr>";
    out << "<tr><td>Форма по ОКУД</td><td>0504421</td></tr>";
    out << "<tr><td>по ОКПО</td><td>" << html_escape(institution.okpo) << "</td></tr>";
    out << "<tr><td>Код должности исполнителя</td><td>" << html_escape(institution.executor_position_code) << "</td></tr>";
    out << "</table></div></div>\n";
    out << "<div class=\"meta\">\n";
    out << "<div>Номер документа: <span class=\"line\">" << document.id << "</span></div>\n";
    out << "<div>Дата составления: <span class=\"line\">" << html_escape(document.created_at) << "</span></div>\n";
    out << "<div>Период: <span class=\"line\">" << html_escape(period_title) << "</span></div>\n";
    out << "<div>Ответственный: <span class=\"line\">" << html_escape(institution.responsible) << "</span></div>\n";
    out << "<div>Должность исполнителя: <span class=\"line\">" << html_escape(institution.executor_position) << "</span></div>\n";
    out << "<div>Структурное подразделение: <span class=\"line\">" << html_escape(institution.structural_unit) << "</span></div>\n";
    out << "<div>Документ: <span class=\"line\">" << html_escape(document.title) << "</span></div>\n";
    out << "</div>\n";

    out << "<table class=\"timesheet-table\"><colgroup>";
    out << "<col class=\"employee\"><col class=\"personnel\"><col class=\"position\">";
    for (size_t i = 0; i < days.size(); ++i) out << "<col class=\"day\">";
    out << "<col class=\"totals\"><col class=\"totals\">";
    out << "</colgroup><thead><tr>";
    out << "<th class=\"employee\">Сотрудник</th><th class=\"personnel\">Таб. N</th><th class=\"position\">Должность</th>";
    for (const auto& day : days) {
        std::string cls = "day";
        if (day.holiday) cls += " holiday";
        else if (day.weekend) cls += " weekend";
        if (day.shortened) cls += " shortened";
        out << "<th class=\"" << cls << "\">" << day.day << "<div class=\"small\">" << html_escape(day.mark) << "</div></th>";
    }
    out << "<th class=\"totals\">Отр. дн.</th><th class=\"totals\">Отр. ч.</th>";
    out << "</tr></thead><tbody>\n";

    for (const auto& employee : employees) {
        if (!employee.active) continue;
        double worked_hours = 0.0;
        int worked_days = 0;
        std::ostringstream cells;

        for (const auto& day : days) {
            std::string cls;
            if (day.holiday) cls += " holiday";
            else if (day.weekend) cls += " weekend";
            if (day.shortened) cls += " shortened";

            if (!works_on_date(employee, day.date)) {
                cells << "<td class=\"out\">-</td>";
                continue;
            }

            const auto cell = db_.timesheet_cell(employee.id, day.date);
            if (!cell.has_value()) {
                cells << "<td class=\"" << cls << "\"></td>";
                continue;
            }

            if (cell->hours > 0.0) {
                if (work_codes.contains(cell->code)) {
                    ++worked_days;
                    worked_hours += cell->hours;
                }
            }
            cells << "<td class=\"" << cls << "\"><div class=\"code\">" << html_escape(cell->code)
                  << "</div><div class=\"hours\">" << format_hours_or_empty(cell->hours) << "</div></td>";
        }

        out << "<tr>";
        out << "<td class=\"employee\">" << html_escape(employee.full_name)
            << "<div class=\"small\">" << html_escape(employee.department) << "</div></td>";
        out << "<td class=\"personnel\">" << html_escape(employee.personnel_no) << "</td>";
        out << "<td class=\"position\">" << html_escape(employee.position) << "</td>";
        out << cells.str();
        out << "<td>" << worked_days << "</td><td>" << format_hours(worked_hours) << "</td>";
        out << "</tr>\n";
    }

    out << "</tbody></table>\n";
    out << "<div class=\"signatures\">";
    out << "<div>Руководитель<div class=\"signature-line\"></div></div>";
    out << "<div>Ответственный исполнитель<div class=\"signature-line\"></div></div>";
    out << "<div>Дата<div class=\"signature-line\"></div></div>";
    out << "</div>\n";
    out << "</body></html>\n";

    output_path = path.string();
    return true;
}

bool App::export_timesheet_ods(const TimesheetDocument& document, std::string& output_path, std::string& error) {
    if (!db_.is_open()) {
        error = "База не открыта";
        return false;
    }
    if (document.id <= 0 || document.year <= 0 || document.month < 1 || document.month > 12) {
        error = "Выберите табель для экспорта";
        return false;
    }

    auto days = db_.calendar_days(document.year, document.month);
    if (days.empty()) days = build_local_calendar(document.year, document.month);
    const auto employees = db_.employees();
    const auto activities = db_.activities();
    const Institution institution = db_.institution();

    std::set<std::string> work_codes;
    for (const auto& activity : activities) {
        if (activity.affects_norm) work_codes.insert(activity.code);
    }

    const std::string period_title = std::string(month_name(document.month)) + " " + std::to_string(document.year);
    const int columns = 3 + static_cast<int>(days.size()) + 2;
    std::ostringstream content;
    content << R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document-content
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 office:version="1.2">
<office:automatic-styles>
<style:style style:name="header" style:family="table-cell"><style:text-properties fo:font-weight="bold"/><style:paragraph-properties fo:text-align="center"/><style:table-cell-properties fo:border="0.06pt solid #000000" fo:background-color="#f0f0f0"/></style:style>
<style:style style:name="center" style:family="table-cell"><style:paragraph-properties fo:text-align="center"/><style:table-cell-properties fo:border="0.06pt solid #000000"/></style:style>
<style:style style:name="left" style:family="table-cell"><style:paragraph-properties fo:text-align="start"/><style:table-cell-properties fo:border="0.06pt solid #000000"/></style:style>
<style:style style:name="title" style:family="table-cell"><style:text-properties fo:font-weight="bold" fo:font-size="14pt"/><style:paragraph-properties fo:text-align="center"/></style:style>
<style:style style:name="weekend" style:family="table-cell"><style:paragraph-properties fo:text-align="center"/><style:table-cell-properties fo:border="0.06pt solid #000000" fo:background-color="#eceff3"/></style:style>
<style:style style:name="holiday" style:family="table-cell"><style:paragraph-properties fo:text-align="center"/><style:table-cell-properties fo:border="0.06pt solid #000000" fo:background-color="#f8d7da"/></style:style>
<style:style style:name="shortened" style:family="table-cell"><style:paragraph-properties fo:text-align="center"/><style:table-cell-properties fo:border="0.06pt solid #000000" fo:background-color="#fff3cd"/></style:style>
</office:automatic-styles>
<office:body><office:spreadsheet>
)xml";
    content << "<table:table table:name=\"Табель 0504421\">";

    content << "<table:table-row>";
    content << "<table:table-cell table:number-columns-spanned=\"" << columns << "\" table:style-name=\"title\" office:value-type=\"string\"><text:p>ТАБЕЛЬ учета использования рабочего времени</text:p></table:table-cell>";
    ods_covered_cells(content, columns - 1);
    content << "</table:table-row>";

    content << "<table:table-row>";
    content << "<table:table-cell table:number-columns-spanned=\"" << columns - 2 << "\" office:value-type=\"string\"><text:p>" << html_escape(institution.title) << "</text:p></table:table-cell>";
    ods_covered_cells(content, columns - 3);
    ods_string_cell(content, "ОКУД");
    ods_string_cell(content, "0504421");
    content << "</table:table-row>";

    content << "<table:table-row>";
    ods_string_cell(content, "Период");
    ods_string_cell(content, period_title);
    ods_string_cell(content, "Документ");
    ods_string_cell(content, document.title + " #" + std::to_string(document.id));
    ods_string_cell(content, "ОКПО");
    ods_string_cell(content, institution.okpo);
    ods_string_cell(content, "Подразделение");
    ods_string_cell(content, institution.structural_unit);
    ods_string_cell(content, "Ответственный");
    ods_string_cell(content, institution.responsible);
    ods_string_cell(content, "Должность");
    ods_string_cell(content, institution.executor_position);
    ods_string_cell(content, "Код должности");
    ods_string_cell(content, institution.executor_position_code);
    for (int i = 14; i < columns; ++i) ods_empty_cell(content);
    content << "</table:table-row>";

    content << "<table:table-row>";
    ods_string_cell(content, "Сотрудник", "header");
    ods_string_cell(content, "Таб. N", "header");
    ods_string_cell(content, "Должность", "header");
    for (const auto& day : days) {
        const char* style = day.holiday ? "holiday" : (day.shortened ? "shortened" : (day.weekend ? "weekend" : "header"));
        ods_string_cell(content, std::to_string(day.day), style);
    }
    ods_string_cell(content, "Отр. дн.", "header");
    ods_string_cell(content, "Отр. ч.", "header");
    content << "</table:table-row>";

    for (const auto& employee : employees) {
        if (!employee.active) continue;
        double worked_hours = 0.0;
        int worked_days = 0;
        std::ostringstream cells;

        for (const auto& day : days) {
            const char* style = day.holiday ? "holiday" : (day.shortened ? "shortened" : (day.weekend ? "weekend" : "center"));
            if (!works_on_date(employee, day.date)) {
                ods_string_cell(cells, "-", style);
                continue;
            }
            const auto cell = db_.timesheet_cell(employee.id, day.date);
            if (!cell.has_value()) {
                ods_empty_cell(cells, style);
                continue;
            }
            if (cell->hours > 0.0) {
                if (work_codes.contains(cell->code)) {
                    ++worked_days;
                    worked_hours += cell->hours;
                }
            }
            const std::string cell_hours = format_hours_or_empty(cell->hours);
            ods_string_cell(cells, cell_hours.empty() ? cell->code : cell->code + "\n" + cell_hours, style);
        }

        content << "<table:table-row>";
        ods_string_cell(content, employee.full_name, "left");
        ods_string_cell(content, employee.personnel_no, "center");
        ods_string_cell(content, employee.position, "left");
        content << cells.str();
        ods_number_cell(content, worked_days, "center");
        ods_number_cell(content, worked_hours, "center");
        content << "</table:table-row>";
    }

    content << "<table:table-row>";
    content << "<table:table-cell table:number-columns-spanned=\"" << columns << "\" office:value-type=\"string\"><text:p>Руководитель ____________________  Ответственный исполнитель ____________________  Дата ____________________</text:p></table:table-cell>";
    ods_covered_cells(content, columns - 1);
    content << "</table:table-row>";
    content << "</table:table></office:spreadsheet></office:body></office:document-content>";

    const std::string styles = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document-styles
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 office:version="1.2"><office:styles/></office:document-styles>)xml";
    const std::string manifest = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="meta.xml" manifest:media-type="text/xml"/>
</manifest:manifest>)xml";
    const std::string meta = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document-meta xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:meta="urn:oasis:names:tc:opendocument:xmlns:meta:1.0" office:version="1.2"><office:meta><meta:generator>tabel0504421</meta:generator></office:meta></office:document-meta>)xml";

    const std::filesystem::path template_path = ots_template_path();
    bool use_template = false;
    std::string output_content = content.str();
    std::string output_manifest = manifest;
    if (!std::filesystem::exists(template_path)) {
        error = "Не найден OTS-шаблон: " + template_path.string();
        return false;
    }

    int template_error = 0;
    zip_t* template_archive = zip_open(template_path.string().c_str(), ZIP_RDONLY, &template_error);
    if (!template_archive) {
        error = "Не удалось открыть OTS-шаблон: " + template_path.string();
        return false;
    }
    std::string template_content;
    const bool template_read = read_zip_text(template_archive, "content.xml", template_content, error);
    if (template_read && template_content.find("{{") == std::string::npos) {
        error = "В OTS-шаблоне нет маркеров {{...}}. Добавьте маркеры из assets/0504421_TEMPLATE_RULES.md";
        zip_close(template_archive);
        return false;
    }
    if (!template_read) {
        zip_close(template_archive);
        return false;
    }
    if (template_read) {
        use_template = true;
        std::string template_manifest;
        if (read_zip_text(template_archive, "META-INF/manifest.xml", template_manifest, error)) {
            replace_all(template_manifest,
                "application/vnd.oasis.opendocument.spreadsheet-template",
                "application/vnd.oasis.opendocument.spreadsheet");
            output_manifest = template_manifest;
        } else {
            zip_close(template_archive);
            return false;
        }
                std::ostringstream year_short;
                year_short << std::setw(2) << std::setfill('0') << (document.year % 100);

                replace_all(template_content, "{{ORG_NAME}}", html_escape(institution.title));
                replace_all(template_content, "{{OKPO}}", html_escape(institution.okpo));
                replace_all(template_content, "{{STRUCTURAL_UNIT}}", html_escape(institution.structural_unit));
                replace_all(template_content, "{{RESPONSIBLE}}", html_escape(institution.responsible));
                replace_all(template_content, "{{EXECUTOR_POSITION}}", html_escape(institution.executor_position));
                replace_all(template_content, "{{EXECUTOR_POSITION_CODE}}", html_escape(institution.executor_position_code));
                replace_all(template_content, "{{MNEMONIC_CODE}}", html_escape(institution.executor_position_code));
                replace_all(template_content, "{{DOC_ID}}", std::to_string(document.id));
                replace_all(template_content, "{{DOC_TITLE}}", html_escape(document.title));
                replace_all(template_content, "{{DOC_DATE}}", html_escape(document.created_at));
                replace_all(template_content, "{{PERIOD}}", html_escape(period_title));
                replace_all(template_content, "{{YEAR}}", std::to_string(document.year));
                replace_all(template_content, "{{YEAR_SHORT}}", year_short.str());
                replace_all(template_content, "{{MONTH}}", std::to_string(document.month));
                replace_all(template_content, "{{MONTH_NAME}}", html_escape(month_name(document.month)));
                replace_all(template_content, "{{MONTH_LAST_DAY}}", std::to_string(days_in_month(document.year, document.month)));

                for (int day_number = 1; day_number <= 31; ++day_number) {
                    char day_key[3]{};
                    std::snprintf(day_key, sizeof(day_key), "%02d", day_number);
                    const auto day_it = std::find_if(days.begin(), days.end(), [day_number](const CalendarDay& day) {
                        return day.day == day_number;
                    });
                    replace_all(template_content, std::string("{{DAY") + day_key + "}}", day_it == days.end() ? "" : std::to_string(day_number));
                    replace_all(template_content, std::string("{{DAY") + day_key + "_MARK}}", day_it == days.end() ? "" : html_escape(day_it->mark));
                }

                const size_t employee_marker = template_content.find("{{EMPLOYEE_NAME}}");
                if (employee_marker != std::string::npos) {
                    const size_t row_start = template_content.rfind("<table:table-row", employee_marker);
                    const size_t row_end_tag = template_content.find("</table:table-row>", employee_marker);
                    if (row_start != std::string::npos && row_end_tag != std::string::npos) {
                        const size_t first_row_end = row_end_tag + std::string("</table:table-row>").size();
                        const size_t row_end = employee_template_block_end(template_content, first_row_end);
                        const std::string row_template = template_content.substr(row_start, row_end - row_start);
                        std::ostringstream rows;
                        for (const auto& employee : employees) {
                            if (!employee.active) continue;
                            double worked_hours = 0.0;
                            int worked_days = 0;
                            std::string row = row_template;
                            replace_all(row, "{{EMPLOYEE_NAME}}", html_escape(employee.full_name));
                            replace_all(row, "{{DEPARTMENT}}", html_escape(employee.department));
                            replace_all(row, "{{PERSONNEL_NO}}", html_escape(employee.personnel_no));
                            replace_all(row, "{{POSITION}}", html_escape(employee.position));

                            for (int day_number = 1; day_number <= 31; ++day_number) {
                                char day_key[3]{};
                                std::snprintf(day_key, sizeof(day_key), "%02d", day_number);
                                std::string code;
                                std::string hours;
                                std::string text;
                                const auto day_it = std::find_if(days.begin(), days.end(), [day_number](const CalendarDay& day) {
                                    return day.day == day_number;
                                });
                                if (day_it != days.end() && works_on_date(employee, day_it->date)) {
                                    const auto cell = db_.timesheet_cell(employee.id, day_it->date);
                                    if (cell.has_value()) {
                                        code = cell->code;
                                        hours = format_hours_or_empty(cell->hours);
                                        text = hours.empty() ? code : code + " " + hours;
                                        if (cell->hours > 0.0) {
                                            if (work_codes.contains(cell->code)) {
                                                ++worked_days;
                                                worked_hours += cell->hours;
                                            }
                                        }
                                    }
                                }
                                replace_day_marker(row, day_number, "_CODE", html_escape(code));
                                replace_day_marker(row, day_number, "_HOURS", html_escape(hours));
                                replace_day_marker(row, day_number, "_TEXT", html_escape(text));
                            }
                            replace_all(row, "{{WORKED_DAYS}}", std::to_string(worked_days));
                            replace_all(row, "{{WORKED_HOURS}}", format_hours(worked_hours));
                            replace_all(row, "{{MISSED_DAYS}}", "");
                            replace_all(row, "{{MISSED_HOURS}}", "");
                            rows << row;
                        }
                        template_content.replace(row_start, row_end - row_start, rows.str());
                    }
                }

                output_content = template_content;
    }
    zip_close(template_archive);

    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("tabel0504421_" + std::to_string(document.id) + "_" + std::to_string(document.year) + "_" +
            (document.month < 10 ? "0" : "") + std::to_string(document.month) + ".ods");

    int zip_error = 0;
    zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!archive) {
        error = "Не удалось создать ODS-файл: " + path.string();
        return false;
    }

    bool ok = add_zip_text(archive, "mimetype", "application/vnd.oasis.opendocument.spreadsheet", true, error);
    if (ok && use_template) {
        int template_error = 0;
        zip_t* template_archive = zip_open(template_path.string().c_str(), ZIP_RDONLY, &template_error);
        if (!template_archive) {
            ok = false;
            error = "Не удалось открыть OTS-шаблон: " + template_path.string();
        } else {
            const zip_int64_t entries = zip_get_num_entries(template_archive, 0);
            for (zip_uint64_t i = 0; ok && i < static_cast<zip_uint64_t>(entries); ++i) {
                ok = copy_zip_entry(template_archive, i, archive, error);
            }
            zip_close(template_archive);
        }
    }
    ok = ok &&
        add_zip_text(archive, "content.xml", output_content, false, error) &&
        add_zip_text(archive, "META-INF/manifest.xml", output_manifest, false, error);
    if (ok && !use_template) {
        ok = add_zip_text(archive, "styles.xml", styles, false, error) &&
            add_zip_text(archive, "meta.xml", meta, false, error);
    }

    if (ok && zip_close(archive) != 0) {
        error = "Не удалось завершить запись ODS-файла";
        ok = false;
    } else if (!ok) {
        zip_discard(archive);
    }
    if (!ok) return false;

    output_path = path.string();
    return true;
}

void App::render() {
    render_menu();
    render_dockspace();
    if (show_dashboard_) render_dashboard();
    if (show_timesheets_) render_timesheets();
    if (show_persons_) render_persons_window();
    if (show_positions_) render_positions_window();
    if (show_employees_) render_employees_window();
    if (show_activities_) render_activities_window();
    if (show_periods_) render_periods_window();
    if (show_norms_) render_norms_window();
    if (show_institution_) render_institution_window();
    if (show_calendar_) render_calendar();
    if (show_settings_) render_settings();
    if (show_about_) render_about();
    render_file_dialogs();
    if (show_demo_) ImGui::ShowDemoWindow(&show_demo_);
}

void App::render_menu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(" Файл")) {
        if (ImGui::MenuItem(" Выход")) should_close_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(" Работа")) {
        ImGui::MenuItem(" Дашборд", nullptr, &show_dashboard_);
        ImGui::MenuItem(" Табели", nullptr, &show_timesheets_);
        ImGui::MenuItem(" Периоды активности", nullptr, &show_periods_);
        ImGui::MenuItem(" Календарь", nullptr, &show_calendar_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(" Справочники")) {
        ImGui::MenuItem(" Виды активности", nullptr, &show_activities_);
        ImGui::Separator();
        ImGui::MenuItem(" Физлица", nullptr, &show_persons_);
        ImGui::MenuItem(" Сотрудники", nullptr, &show_employees_);
        ImGui::MenuItem(" Должности", nullptr, &show_positions_);
        ImGui::MenuItem(" Нормы времени", nullptr, &show_norms_);
        ImGui::MenuItem(" Учреждение", nullptr, &show_institution_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(" База")) {
        if (ImGui::MenuItem(" Открыть")) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            IGFD::FileDialog::Instance()->OpenDialog("OpenDb", "Открыть базу", ".db,.sqlite,.sqlite3", config);
        }
        if (ImGui::MenuItem(" Создать")) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            config.fileName = "tabel.sqlite3";
            IGFD::FileDialog::Instance()->OpenDialog("CreateDb", "Создать базу", ".db,.sqlite,.sqlite3", config);
        }
        if (ImGui::MenuItem(" Сохранить базу как...", nullptr, false, db_.is_open())) {
            std::string error;
            if (save_pending_changes(error)) {
                IGFD::FileDialogConfig config;
                config.path = db_.path().empty() ? "." : std::filesystem::path(db_.path()).parent_path().string();
                config.fileName = db_.path().empty() ? "tabel.sqlite3" : std::filesystem::path(db_.path()).filename().string();
                IGFD::FileDialog::Instance()->OpenDialog("SaveDbAs", "Сохранить базу как", ".db,.sqlite,.sqlite3", config);
            } else {
                autosave_status(false, error);
            }
        }
        if (ImGui::BeginMenu(" Недавние базы", !settings_.recent_databases.empty())) {
            for (const auto& recent : settings_.recent_databases) {
                if (ImGui::MenuItem(recent.c_str())) open_database(recent);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(" Прочее")) {
        ImGui::MenuItem(" Настройки", nullptr, &show_settings_);
        ImGui::MenuItem(" О программе", nullptr, &show_about_);
        ImGui::EndMenu();
    }
    const std::string database_label = compact_database_label(db_);
    const std::string top_status = database_label + "  |  " + status_;
    const float status_width = std::min(ImGui::CalcTextSize(top_status.c_str()).x, ImGui::GetMainViewport()->WorkSize.x * 0.42f);
    const float right_x = ImGui::GetWindowContentRegionMax().x - status_width;
    if (ImGui::GetCursorPosX() < right_x) {
        ImGui::SetCursorPosX(right_x);
    } else {
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(top_status.c_str());
    ImGui::PopStyleColor();
    if (ImGui::BeginItemTooltip()) {
        ImGui::Text("База: %s", db_.is_open() ? db_.path().c_str() : "не выбрана");
        ImGui::Text("Событие: %s", status_.c_str());
        if (!last_error_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", last_error_.c_str());
        }
        ImGui::EndTooltip();
    }
    ImGui::EndMainMenuBar();
}

void App::render_dockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Рабочая область", nullptr, flags);
    ImGui::PopStyleVar(2);
    const ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
    if (!dock_layout_built_) {
        dock_layout_built_ = true;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID left_id = dockspace_id;
        ImGuiID right_id = ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Right, 0.36f, nullptr, &left_id);
        ImGuiID right_bottom_id = ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Down, 0.42f, nullptr, &right_id);
        ImGuiID bottom_id = ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Down, 0.28f, nullptr, &left_id);

        ImGui::DockBuilderDockWindow(" Дашборд", left_id);
        ImGui::DockBuilderDockWindow(" Табели", left_id);
        ImGui::DockBuilderDockWindow(" Календарь", bottom_id);
        ImGui::DockBuilderDockWindow(" Физлица", right_id);
        ImGui::DockBuilderDockWindow(" Должности", right_id);
        ImGui::DockBuilderDockWindow(" Сотрудники", right_id);
        ImGui::DockBuilderDockWindow(" Виды активности", right_id);
        ImGui::DockBuilderDockWindow(" Периоды активности", right_id);
        ImGui::DockBuilderDockWindow(" Нормы времени", right_id);
        ImGui::DockBuilderDockWindow(" Учреждение", right_id);
        ImGui::DockBuilderDockWindow(" Настройки", right_bottom_id);
        ImGui::DockBuilderDockWindow(" О программе", right_bottom_id);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void App::render_dashboard() {
    if (!ImGui::Begin(" Дашборд", &show_dashboard_)) {
        ImGui::End();
        return;
    }

    if (!db_.is_open()) {
        ImGui::TextUnformatted("Откройте или создайте базу.");
        if (ImGui::Button(" Открыть базу")) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            IGFD::FileDialog::Instance()->OpenDialog("OpenDb", "Открыть базу", ".db,.sqlite,.sqlite3", config);
        }
        ImGui::SameLine();
        if (ImGui::Button(" Создать базу")) {
            IGFD::FileDialogConfig config;
            config.path = ".";
            config.fileName = "tabel.sqlite3";
            IGFD::FileDialog::Instance()->OpenDialog("CreateDb", "Создать базу", ".db,.sqlite,.sqlite3", config);
        }
        ImGui::End();
        return;
    }

    auto shift_dashboard_month = [this](int delta) {
        int month = context_.month + delta;
        while (month < 1) {
            month += 12;
            --context_.year;
        }
        while (month > 12) {
            month -= 12;
            ++context_.year;
        }
        context_.month = month;
        selected_calendar_date_ = iso_date(context_.year, context_.month, 1);
        refresh();
    };

    if (ImGui::ArrowButton("dashboard_prev_month", ImGuiDir_Left)) {
        shift_dashboard_month(-1);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##dashboard_month", month_name(context_.month))) {
        for (int month = 1; month <= 12; ++month) {
            const bool selected = context_.month == month;
            if (ImGui::Selectable(month_name(month), selected)) {
                context_.month = month;
                selected_calendar_date_ = iso_date(context_.year, context_.month, 1);
                refresh();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(92.0f);
    int dashboard_year = context_.year;
    if (ImGui::InputInt("##dashboard_year", &dashboard_year, 0, 0, ImGuiInputTextFlags_CharsDecimal) && dashboard_year > 1900) {
        context_.year = dashboard_year;
        selected_calendar_date_ = iso_date(context_.year, context_.month, 1);
        refresh();
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("dashboard_next_month", ImGuiDir_Right)) {
        shift_dashboard_month(1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Сегодня##dashboard_month")) {
        std::time_t now = std::time(nullptr);
        if (std::tm* tm = std::localtime(&now)) {
            context_.year = tm->tm_year + 1900;
            context_.month = tm->tm_mon + 1;
            selected_calendar_date_ = iso_date(context_.year, context_.month, tm->tm_mday);
            refresh();
        }
    }
    ImGui::Spacing();

    int active_employees = 0;
    for (const auto& e : employees_) {
        if (e.active) ++active_employees;
    }
    const int inactive_employees = static_cast<int>(employees_.size()) - active_employees;
    const int accepted_documents = static_cast<int>(std::count_if(timesheet_documents_.begin(), timesheet_documents_.end(), [](const TimesheetDocument& d) {
        return d.accepted;
    }));
    std::vector<TimesheetDocument> refill_documents;
    for (const auto& document : timesheet_documents_) {
        if (document.needs_refill && !document.accepted) refill_documents.push_back(document);
    }
    int context_documents = 0;
    for (const auto& document : timesheet_documents_) {
        if (document.year == context_.year && document.month == context_.month) ++context_documents;
    }
    const CurrentMonthState current = current_month_state(db_, timesheet_documents_, employees_);
    const int missing_current_cells = std::max(0, current.expected_cells - current.existing_cells);

    if (ImGui::BeginTable("dashboard_summary", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        dashboard_metric("Текущий период", std::string(month_name(context_.month)) + " " + std::to_string(context_.year),
            std::to_string(context_documents) + " таб.",
            ImVec4(0.11f, 0.56f, 0.70f, 1.0f), ImVec4(0.11f, 0.56f, 0.70f, 0.11f));
        dashboard_metric("Сотрудники", std::to_string(active_employees) + " активных",
            inactive_employees > 0 ? (std::to_string(inactive_employees) + " неактивных").c_str() : "",
            ImVec4(0.24f, 0.62f, 0.31f, 1.0f), ImVec4(0.24f, 0.62f, 0.31f, 0.11f));
        dashboard_metric("Табели", std::to_string(timesheet_documents_.size()) + " всего",
            std::to_string(accepted_documents) + " принятых",
            ImVec4(0.76f, 0.45f, 0.12f, 1.0f), ImVec4(0.76f, 0.45f, 0.12f, 0.12f));
        dashboard_metric("Активности", std::to_string(periods_.size()) + " в месяце",
            std::to_string(upcoming_periods_.size()) + " ближайших",
            ImVec4(0.56f, 0.37f, 0.72f, 1.0f), ImVec4(0.56f, 0.37f, 0.72f, 0.12f));
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Календарь");
    int prev_year = 0;
    int prev_month = 0;
    int next_year = 0;
    int next_month = 0;
    shifted_month(context_.year, context_.month, -1, prev_year, prev_month);
    shifted_month(context_.year, context_.month, 1, next_year, next_month);
    auto prev_days = db_.calendar_days(prev_year, prev_month);
    if (prev_days.empty()) prev_days = build_local_calendar(prev_year, prev_month);
    auto next_days = db_.calendar_days(next_year, next_month);
    if (next_days.empty()) next_days = build_local_calendar(next_year, next_month);
    if (ImGui::BeginTable("dashboard_three_months", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        dashboard_month_calendar(prev_year, prev_month, prev_days, false);
        ImGui::TableNextColumn();
        dashboard_month_calendar(context_.year, context_.month, month_days_, true);
        ImGui::TableNextColumn();
        dashboard_month_calendar(next_year, next_month, next_days, false);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (current.documents == 0) {
        warning_banner("", "Табель текущего месяца еще не создан",
            (std::string("Нет табеля за ") + month_name(current.month) + " " + std::to_string(current.year) + ". Создайте и заполните его перед закрытием месяца.").c_str());
    } else if (missing_current_cells > 0 && !current.accepted) {
        warning_banner("", "Табель текущего месяца заполнен не полностью",
            (std::to_string(missing_current_cells) + " ячеек из " + std::to_string(current.expected_cells) + " еще не заполнены. Перезаполнение создаст значения по календарю, нормам и периодам активности.").c_str());
    } else if (current.needs_refill && !current.accepted) {
        warning_banner("", "Табель текущего месяца требует перезаполнения",
            "После изменений в периодах активности данные текущего табеля могут быть устаревшими.");
    }
    if (!refill_documents.empty()) {
        ImGui::Spacing();
        warning_banner("", "Есть табели, требующие перезаполнения",
            (std::to_string(refill_documents.size()) + " таб. помечены как устаревшие после изменений в периодах активности.").c_str());
    }

    ImGui::Spacing();
    if (current.documents == 0 && ImGui::Button(" Заполнить текущий месяц")) {
        TimesheetDocument document;
        document.year = current.year;
        document.month = current.month;
        if (document.title.empty()) document.title = "Табель";
        std::string error;
        if (db_.create_or_refill_timesheet_document(document, error)) {
            edited_timesheet_document_id_ = document.id;
            edited_timesheet_document_ = document;
            context_.year = document.year;
            context_.month = document.month;
            refresh();
            show_timesheets_ = true;
            status_ = "Табель текущего месяца заполнен";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    if (current.documents == 0) ImGui::SameLine();
    if (!refill_documents.empty()) {
        const std::string refill_button = " Табели к обновлению (" + std::to_string(refill_documents.size()) + ")";
        if (ImGui::Button(refill_button.c_str())) {
            show_timesheets_ = true;
            edited_timesheet_document_ = refill_documents.front();
            edited_timesheet_document_id_ = edited_timesheet_document_.id;
            context_.year = edited_timesheet_document_.year;
            context_.month = edited_timesheet_document_.month;
            refresh();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(" Активности сотрудников")) show_periods_ = true;
    ImGui::SameLine();
    if (ImGui::Button(" Все табели")) show_timesheets_ = true;
    ImGui::SameLine();
    if (ImGui::Button(" Сотрудники")) show_employees_ = true;

    ImGui::SeparatorText("Табели");
    if (timesheet_documents_.empty()) {
        ImGui::TextDisabled("Табелей пока нет.");
    } else if (ImGui::BeginTable("dashboard_timesheets", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Период", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Документ");
        ImGui::TableSetupColumn("Статус", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Создан", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();
        int shown = 0;
        for (const auto& document : timesheet_documents_) {
            if (shown >= 7) break;
            ++shown;
            ImGui::TableNextRow();
            if (document.needs_refill && !document.accepted) {
                const ImU32 refill_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.72f, 0.12f, 0.24f));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, refill_bg);
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s %d", month_name(document.month), document.year);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(document.title.c_str());
            ImGui::TableSetColumnIndex(2);
            if (document.accepted) {
                ImGui::TextUnformatted("Принят");
            } else if (document.needs_refill) {
                ImGui::TextUnformatted("Обновить");
            } else {
                ImGui::TextUnformatted("В работе");
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(document.created_at.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(document.id);
            if (ImGui::SmallButton("Открыть")) {
                edited_timesheet_document_ = document;
                edited_timesheet_document_id_ = document.id;
                context_.year = document.year;
                context_.month = document.month;
                show_timesheets_ = true;
                refresh();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Ближайшие отсутствия и отклонения");
    if (upcoming_periods_.empty()) {
        ImGui::TextUnformatted("Ближайших отпусков, больничных и других периодов пока нет.");
    } else if (ImGui::BeginTable("upcoming_periods", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Сотрудник");
        ImGui::TableSetupColumn("Код");
        ImGui::TableSetupColumn("Вид");
        ImGui::TableSetupColumn("С");
        ImGui::TableSetupColumn("По");
        ImGui::TableSetupColumn("Основание");
        ImGui::TableHeadersRow();
        for (const auto& p : upcoming_periods_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(p.employee_name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(p.activity_code.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(p.activity_title.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(p.date_from.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(p.date_to.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextWrapped("%s", p.note.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

void App::render_timesheets() {
    if (!ImGui::Begin(" Табели", &show_timesheets_)) {
        ImGui::End();
        return;
    }
    if (!db_.is_open()) {
        ImGui::TextUnformatted("Откройте или создайте базу.");
        ImGui::End();
        return;
    }

    if (ImGui::Button(" Добавить")) {
        edited_timesheet_document_ = TimesheetDocument{};
        edited_timesheet_document_.year = context_.year;
        edited_timesheet_document_.month = context_.month;
        edited_timesheet_document_.title = "Табель";
        std::string error;
        if (db_.save_timesheet_document(edited_timesheet_document_, error)) {
            edited_timesheet_document_id_ = edited_timesheet_document_.id;
            refresh();
            if (auto* document = find_by_id(timesheet_documents_, edited_timesheet_document_id_)) {
                edited_timesheet_document_ = *document;
            }
            focus_first_editor_field_ = true;
            status_ = "Добавлен табель";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(" Печать 0504421")) {
        std::string output_path;
        std::string error;
        if (print_timesheet_html(edited_timesheet_document_, output_path, error)) {
            const std::string command = "xdg-open " + shell_quote(output_path) + " >/dev/null 2>&1 &";
            const int open_result = std::system(command.c_str());
            status_ = open_result == 0 ? "Печатная форма открыта: " + output_path : "Печатная форма создана: " + output_path;
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(" Отчет")) {
        status_ = "Отчеты будут добавлены следующим этапом";
    }
    ImGui::SameLine();
    if (ImGui::Button(" Экспорт")) {
        std::string output_path;
        std::string error;
        if (export_timesheet_ods(edited_timesheet_document_, output_path, error)) {
            const std::string command = "xdg-open " + shell_quote(output_path) + " >/dev/null 2>&1 &";
            const int open_result = std::system(command.c_str());
            status_ = open_result == 0 ? "ODS открыт: " + output_path : "ODS создан: " + output_path;
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }

    const float available_height = ImGui::GetContentRegionAvail().y;
    if (timesheets_list_height_ <= 0.0f) {
        timesheets_list_height_ = std::min(170.0f, available_height * 0.32f);
    }
    timesheets_list_height_ = std::clamp(timesheets_list_height_, 90.0f, std::max(90.0f, available_height - 320.0f));

    ImGui::BeginChild("timesheets_list", ImVec2(0, timesheets_list_height_), ImGuiChildFlags_Border);
    bool select_timesheet_document = false;
    TimesheetDocument selected_timesheet_document;
    if (ImGui::BeginTable("timesheets_documents_table", 7,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Год", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("Месяц", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Наименование");
        ImGui::TableSetupColumn("Создан", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Обновить", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Принят", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableHeadersRow();
        for (auto& d : timesheet_documents_) {
            ImGui::TableNextRow();
            if (d.accepted) {
                const ImU32 accepted_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.58f, 0.28f, 0.22f));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, accepted_bg);
            } else if (d.needs_refill) {
                const ImU32 refill_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.72f, 0.12f, 0.28f));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, refill_bg);
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(d.id);
            const bool selected = edited_timesheet_document_id_ == d.id;
            if (ImGui::Selectable(std::to_string(d.id).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selected_timesheet_document = d;
                select_timesheet_document = true;
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", d.year);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(month_name(d.month));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(d.title.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(d.created_at.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(d.needs_refill ? "!" : "");
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(d.accepted ? "Да" : "");
        }
        ImGui::EndTable();
    }
    if (select_timesheet_document) {
        edited_timesheet_document_ = selected_timesheet_document;
        edited_timesheet_document_id_ = selected_timesheet_document.id;
        context_.year = selected_timesheet_document.year;
        context_.month = selected_timesheet_document.month;
        refresh();
    }
    ImGui::EndChild();
    horizontal_splitter("timesheets_split", &timesheets_list_height_, 90.0f, 320.0f);
    ImGui::BeginChild("timesheet_document_editor", ImVec2(0, 0), ImGuiChildFlags_Border);
    if (edited_timesheet_document_id_ <= 0) {
        ImGui::TextDisabled("Выберите табель в списке или добавьте новый.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    bool save = false;
    if (focus_first_editor_field_) { ImGui::SetKeyboardFocusHere(); focus_first_editor_field_ = false; }
    if (edited_timesheet_document_.accepted) {
        ImGui::BeginDisabled();
    }
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Год", &edited_timesheet_document_.year); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    edited_timesheet_document_.month = std::clamp(edited_timesheet_document_.month, 1, 12);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("Месяц", month_name(edited_timesheet_document_.month))) {
        for (int month = 1; month <= 12; ++month) {
            const bool selected = edited_timesheet_document_.month == month;
            if (ImGui::Selectable(month_name(month), selected)) {
                edited_timesheet_document_.month = month;
                save = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (edited_timesheet_document_.accepted) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    if (edited_timesheet_document_.accepted) {
        ImGui::BeginDisabled();
    }
    if (edited_timesheet_document_.needs_refill) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.72f, 0.12f, 0.36f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.72f, 0.12f, 0.48f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.66f, 0.08f, 0.62f));
    }
    if (ImGui::Button(" Заполнить / перезаполнить")) {
        ImGui::OpenPopup("confirm_refill_timesheet");
    }
    if (edited_timesheet_document_.needs_refill) {
        ImGui::PopStyleColor(3);
    }
    if (ImGui::BeginPopupModal("confirm_refill_timesheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Перезаполнить табель?");
        ImGui::TextUnformatted("Текущие ячейки за месяц будут заменены автоматически рассчитанными значениями.");
        ImGui::Spacing();
        if (ImGui::Button("Да", ImVec2(110.0f, 0.0f))) {
        std::string error;
        if (db_.create_or_refill_timesheet_document(edited_timesheet_document_, error)) {
            edited_timesheet_document_id_ = edited_timesheet_document_.id;
            context_.year = edited_timesheet_document_.year;
            context_.month = edited_timesheet_document_.month;
            refresh();
            status_ = "Табель создан и автоматически заполнен";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(110.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (edited_timesheet_document_.accepted) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    save |= ImGui::Checkbox("Табель принят", &edited_timesheet_document_.accepted);
    if (edited_timesheet_document_.accepted) {
        ImGui::SameLine();
        ImGui::TextDisabled("Табель принят и защищен от изменений");
    }
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    if (edited_timesheet_document_.accepted) {
        ImGui::BeginDisabled();
    }
    if (confirm_delete_button("timesheet_document", edited_timesheet_document_.title.empty() ? "Табель" : edited_timesheet_document_.title.c_str())) {
        std::string error;
        if (db_.delete_timesheet_document(edited_timesheet_document_, error)) {
            edited_timesheet_document_ = TimesheetDocument{};
            edited_timesheet_document_id_ = 0;
            refresh();
            status_ = "Табель удален";
            last_error_.clear();
            ImGui::EndChild();
            ImGui::End();
            return;
        }
        autosave_status(false, error);
    }
    if (edited_timesheet_document_.accepted) {
        ImGui::EndDisabled();
    }
    input_text("Название", edited_timesheet_document_.title); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_text("Примечание", edited_timesheet_document_.note); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("Создан: %s", edited_timesheet_document_.created_at.empty() ? "-" : edited_timesheet_document_.created_at.c_str());

    if (save && edited_timesheet_document_.year > 0) {
        std::string error;
        autosave_status(db_.save_timesheet_document(edited_timesheet_document_, error), error);
        edited_timesheet_document_id_ = edited_timesheet_document_.id;
        context_.year = edited_timesheet_document_.year;
        context_.month = edited_timesheet_document_.month;
        refresh();
    }

    ImGui::Separator();
    render_timesheet_grid();
    ImGui::EndChild();
    ImGui::End();
}

void App::render_timesheet_grid() {
    ImGui::SeparatorText((std::string(month_name(context_.month)) + " " + std::to_string(context_.year)).c_str());
    const bool timesheet_locked = edited_timesheet_document_.accepted;

    if (ImGui::BeginTable("timesheet", static_cast<int>(month_days_.size()) + 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit)) {
        std::map<int, WorkNorm> norms_by_id;
        for (const auto& norm : norms_) {
            norms_by_id[norm.id] = norm;
        }
        std::set<std::string> work_codes;
        for (const auto& activity : activities_) {
            if (activity.affects_norm) work_codes.insert(activity.code);
        }
        ImGui::TableSetupColumn("Сотрудник", ImGuiTableColumnFlags_WidthFixed, 320.0f);
        ImGui::TableSetupColumn("Отр.", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("Не отр.", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        for (const auto& day : month_days_) {
            ImGui::TableSetupColumn(std::to_string(day.day).c_str(), ImGuiTableColumnFlags_WidthFixed, 48.0f);
        }
        ImGui::TableHeadersRow();
        for (const auto& e : employees_) {
            if (!e.active) continue;
            double worked_hours = 0.0;
            double missed_hours = 0.0;
            int worked_days = 0;
            int missed_days = 0;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(e.full_name.c_str());
            if (!e.position.empty() || !e.norm_title.empty()) {
                ImGui::TextDisabled("%s%s%s",
                    e.position.c_str(),
                    !e.position.empty() && !e.norm_title.empty() ? " / " : "",
                    e.norm_title.c_str());
            }
            for (int i = 0; i < static_cast<int>(month_days_.size()); ++i) {
                const auto& day = month_days_[i];
                ImGui::TableSetColumnIndex(i + 3);
                if (day.weekend) {
                    const ImU32 weekend_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.72f, 0.18f, 0.18f, 0.18f));
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, weekend_bg);
                }
                const bool employed = works_on_date(e, day.date);
                if (!employed) {
                    ImGui::TextDisabled("-");
                    if (ImGui::BeginItemTooltip()) {
                        ImGui::TextUnformatted(e.full_name.c_str());
                        ImGui::Text("%s: вне периода работы", day.date.c_str());
                        if (!e.hire_date.empty()) ImGui::Text("Прием: %s", e.hire_date.c_str());
                        if (!e.dismissal_date.empty()) ImGui::Text("Увольнение: %s", e.dismissal_date.c_str());
                        ImGui::EndTooltip();
                    }
                    continue;
                }
                const auto norm_it = norms_by_id.find(e.norm_id);
                const WorkNorm* norm = norm_it == norms_by_id.end() ? nullptr : &norm_it->second;
                const double default_hours = day.weekend ? 0.0 : (norm ? work_hours_for_day(norm, day) : (day.shortened ? 7.0 : 8.0));
                auto cell = db_.timesheet_cell(e.id, day.date).value_or(TimesheetCell{e.id, day.date, day.mark.empty() ? "Я" : day.mark, default_hours, ""});
                if (cell.hours > 0.0) {
                    if (work_codes.contains(cell.code)) {
                        ++worked_days;
                        worked_hours += cell.hours;
                    } else {
                        ++missed_days;
                        missed_hours += cell.hours;
                    }
                }
                ImGui::PushID((std::to_string(e.id) + day.date).c_str());
                if (timesheet_locked) ImGui::BeginDisabled();
                if (ImGui::Selectable(cell.code.c_str(), false, ImGuiSelectableFlags_DontClosePopups, ImVec2(40.0f, 0.0f))) {
                    ImGui::OpenPopup("activity_code_popup");
                }
                if (ImGui::BeginPopup("activity_code_popup")) {
                    for (const auto& a : activities_) {
                        const std::string row = a.code + "  " + a.title;
                        if (ImGui::Selectable(row.c_str(), a.code == cell.code)) {
                            cell.code = a.code;
                            std::string error;
                            autosave_status(db_.save_timesheet_cell(cell, error), error);
                        }
                    }
                    ImGui::EndPopup();
                }
                if (timesheet_locked) ImGui::EndDisabled();
                ImGui::Text("%.1f", cell.hours);
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted(e.full_name.c_str());
                    ImGui::Text("%s: %.1f ч.", day.date.c_str(), cell.hours);
                    if (timesheet_locked) ImGui::TextUnformatted("Табель принят");
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", worked_days);
            ImGui::Text("%.1f", worked_hours);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", missed_days);
            ImGui::Text("%.1f", missed_hours);
        }
        ImGui::EndTable();
    }
}

void App::render_persons_window() {
    if (!ImGui::Begin(" Физлица", &show_persons_)) { ImGui::End(); return; }
    if (!db_.is_open()) ImGui::TextUnformatted("Откройте базу."); else render_persons_tab();
    ImGui::End();
}

void App::render_positions_window() {
    if (!ImGui::Begin(" Должности", &show_positions_)) { ImGui::End(); return; }
    if (!db_.is_open()) ImGui::TextUnformatted("Откройте базу."); else render_positions_tab();
    ImGui::End();
}

void App::render_employees_window() {
    if (!ImGui::Begin(" Сотрудники", &show_employees_)) { ImGui::End(); return; }
    if (!db_.is_open()) ImGui::TextUnformatted("Откройте базу."); else render_employees_tab();
    ImGui::End();
}

void App::render_activities_window() {
    if (!ImGui::Begin(" Виды активности", &show_activities_)) { ImGui::End(); return; }
    if (!db_.is_open()) ImGui::TextUnformatted("Откройте базу."); else render_activities_tab();
    ImGui::End();
}

void App::render_periods_window() {
    if (!ImGui::Begin(" Периоды активности", &show_periods_)) { ImGui::End(); return; }
    if (!db_.is_open()) ImGui::TextUnformatted("Откройте базу."); else render_periods_tab();
    ImGui::End();
}

void App::render_norms_window() {
    if (!ImGui::Begin(" Нормы времени", &show_norms_)) { ImGui::End(); return; }
    if (!db_.is_open()) ImGui::TextUnformatted("Откройте базу."); else render_norms_tab();
    ImGui::End();
}

void App::render_institution_window() {
    if (!ImGui::Begin(" Учреждение", &show_institution_)) { ImGui::End(); return; }
    if (!db_.is_open()) ImGui::TextUnformatted("Откройте базу."); else render_institution_tab();
    ImGui::End();
}

void App::render_persons_tab() {
    ImGui::Columns(2, "persons_columns", true);
    if (ImGui::Button(" Добавить##person")) {
        edited_person_ = Person{};
        std::string error;
        if (db_.save_person(edited_person_, error)) {
            edited_person_id_ = edited_person_.id;
            refresh();
            focus_first_editor_field_ = true;
            status_ = "Добавлено физлицо";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    ImGui::Separator();
    for (auto& p : persons_) {
        const std::string row = (p.full_name.empty() ? "(новое физлицо)" : p.full_name) + std::string("##person_") + std::to_string(p.id);
        if (ImGui::Selectable(row.c_str(), edited_person_id_ == p.id)) {
            edited_person_ = p;
            edited_person_id_ = p.id;
        }
    }
    ImGui::NextColumn();
    if (edited_person_id_ <= 0) {
        ImGui::TextDisabled("Выберите физлицо в списке или добавьте новое.");
        ImGui::Columns(1);
        return;
    }
    bool save = false;
    if (focus_first_editor_field_) { ImGui::SetKeyboardFocusHere(); focus_first_editor_field_ = false; }
    input_text("ФИО", edited_person_.full_name); save |= ImGui::IsItemDeactivatedAfterEdit();
    save |= input_date("Дата рождения", edited_person_.birth_date);
    input_text("СНИЛС", edited_person_.snils); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_text("Телефон", edited_person_.phone); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_text("Email", edited_person_.email); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_multiline_wrapped("Примечание", edited_person_.note, 110.0f); save |= ImGui::IsItemDeactivatedAfterEdit();
    if (confirm_delete_button("person", edited_person_.full_name.empty() ? "(новое физлицо)" : edited_person_.full_name.c_str())) {
        std::string error;
        if (db_.delete_person(edited_person_id_, error)) {
            edited_person_ = Person{};
            edited_person_id_ = 0;
            refresh();
            status_ = "Физлицо удалено";
            last_error_.clear();
            ImGui::Columns(1);
            return;
        }
        autosave_status(false, error);
    }
    if (save) {
        std::string error;
        autosave_status(db_.save_person(edited_person_, error), error);
        edited_person_id_ = edited_person_.id;
        refresh();
    }
    ImGui::Columns(1);
}

void App::render_positions_tab() {
    ImGui::Columns(2, "positions_columns", true);
    if (ImGui::Button(" Добавить##position")) {
        edited_position_ = Position{};
        std::string error;
        if (db_.save_position(edited_position_, error)) {
            edited_position_id_ = edited_position_.id;
            refresh();
            focus_first_editor_field_ = true;
            status_ = "Добавлена должность";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    ImGui::Separator();
    for (auto& p : positions_) {
        const std::string row = (p.title.empty() ? "(новая должность)" : p.title) + std::string("##position_") + std::to_string(p.id);
        if (ImGui::Selectable(row.c_str(), edited_position_id_ == p.id)) {
            edited_position_ = p;
            edited_position_id_ = p.id;
        }
    }
    ImGui::NextColumn();
    if (edited_position_id_ <= 0) {
        ImGui::TextDisabled("Выберите должность в списке или добавьте новую.");
        ImGui::Columns(1);
        return;
    }
    bool save = false;
    if (focus_first_editor_field_) { ImGui::SetKeyboardFocusHere(); focus_first_editor_field_ = false; }
    input_text("Наименование", edited_position_.title); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_text("Категория", edited_position_.category); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_multiline_wrapped("Примечание", edited_position_.note, 110.0f); save |= ImGui::IsItemDeactivatedAfterEdit();
    if (confirm_delete_button("position", edited_position_.title.empty() ? "(новая должность)" : edited_position_.title.c_str())) {
        std::string error;
        if (db_.delete_position(edited_position_id_, error)) {
            edited_position_ = Position{};
            edited_position_id_ = 0;
            refresh();
            status_ = "Должность удалена";
            last_error_.clear();
            ImGui::Columns(1);
            return;
        }
        autosave_status(false, error);
    }
    if (save) {
        std::string error;
        autosave_status(db_.save_position(edited_position_, error), error);
        edited_position_id_ = edited_position_.id;
        refresh();
    }
    ImGui::Columns(1);
}

void App::render_employees_tab() {
    float left = ImGui::GetContentRegionAvail().x * settings_.left_split;
    ImGui::BeginChild("employees_list", ImVec2(left, 0), ImGuiChildFlags_Border);
    if (ImGui::Button(" Добавить")) {
        edited_employee_ = Employee{};
        edited_employee_.active = true;
        std::string error;
        if (db_.save_employee(edited_employee_, error)) {
            edited_employee_id_ = edited_employee_.id;
            selected_employee_id_ = edited_employee_.id;
            focus_first_editor_field_ = true;
            refresh();
            status_ = "Добавлен новый сотрудник";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    ImGui::Separator();
    for (auto& e : employees_) {
        const std::string title = e.full_name.empty() ? "(новый сотрудник)" : e.full_name;
        const std::string row = title + "##employee_" + std::to_string(e.id);
        if (ImGui::Selectable(row.c_str(), edited_employee_id_ == e.id)) {
            edited_employee_ = e;
            edited_employee_id_ = e.id;
        }
    }
    ImGui::EndChild();
    splitter("employees_split", &left, 220.0f, 360.0f);
    settings_.left_split = std::clamp(left / std::max(1.0f, ImGui::GetWindowWidth()), 0.20f, 0.70f);
    ImGui::BeginChild("employee_editor", ImVec2(0, 0), ImGuiChildFlags_Border);
    if (edited_employee_id_ <= 0) {
        ImGui::TextDisabled("Выберите сотрудника в списке или добавьте нового.");
        ImGui::EndChild();
        return;
    }
    bool save = false;
    const char* person_preview = edited_employee_.person_id == 0 ? "Выберите физлицо" : edited_employee_.full_name.c_str();
    if (ImGui::BeginCombo("Физлицо", person_preview)) {
        for (const auto& p : persons_) {
            const bool selected = p.id == edited_employee_.person_id;
            if (ImGui::Selectable(p.full_name.c_str(), selected)) {
                edited_employee_.person_id = p.id;
                edited_employee_.full_name = p.full_name;
                save = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (focus_first_editor_field_) { ImGui::SetKeyboardFocusHere(); focus_first_editor_field_ = false; }
    input_text("ФИО для печати", edited_employee_.full_name); save |= ImGui::IsItemDeactivatedAfterEdit();
    const char* position_preview = edited_employee_.position_id == 0 ? "Выберите должность" : edited_employee_.position.c_str();
    if (ImGui::BeginCombo("Должность", position_preview)) {
        for (const auto& p : positions_) {
            const bool selected = p.id == edited_employee_.position_id;
            if (ImGui::Selectable(p.title.c_str(), selected)) {
                edited_employee_.position_id = p.id;
                edited_employee_.position = p.title;
                save = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const char* norm_preview = edited_employee_.norm_id == 0 ? "Выберите норму времени" : edited_employee_.norm_title.c_str();
    if (ImGui::BeginCombo("Норма времени", norm_preview)) {
        for (const auto& n : norms_) {
            const bool selected = n.id == edited_employee_.norm_id;
            if (ImGui::Selectable(n.title.c_str(), selected)) {
                edited_employee_.norm_id = n.id;
                edited_employee_.norm_title = n.title;
                save = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    input_text("Подразделение", edited_employee_.department); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_text("Табельный номер", edited_employee_.personnel_no); save |= ImGui::IsItemDeactivatedAfterEdit();
    save |= input_date("Прием на работу", edited_employee_.hire_date);
    save |= input_date("Увольнение", edited_employee_.dismissal_date);
    save |= ImGui::Checkbox("Активен", &edited_employee_.active);
    input_multiline_wrapped("Примечание", edited_employee_.note, 110.0f); save |= ImGui::IsItemDeactivatedAfterEdit();
    if (confirm_delete_button("employee", edited_employee_.full_name.empty() ? "(новый сотрудник)" : edited_employee_.full_name.c_str())) {
        std::string error;
        if (db_.delete_employee(edited_employee_id_, error)) {
            edited_employee_ = Employee{};
            edited_employee_id_ = 0;
            selected_employee_id_ = 0;
            refresh();
            status_ = "Сотрудник удален";
            last_error_.clear();
            ImGui::EndChild();
            return;
        }
        autosave_status(false, error);
    }
    if (save) {
        std::string error;
        autosave_status(db_.save_employee(edited_employee_, error), error);
        edited_employee_id_ = edited_employee_.id;
        refresh();
    }
    ImGui::EndChild();
}

void App::render_activities_tab() {
    ImGui::Columns(2, "activities_columns", true);
    if (ImGui::Button(" Добавить##activity")) {
        edited_activity_ = ActivityKind{};
        std::string error;
        if (db_.save_activity(edited_activity_, error)) {
            edited_activity_id_ = edited_activity_.id;
            refresh();
            focus_first_editor_field_ = true;
            status_ = "Добавлен вид активности";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    for (auto& a : activities_) {
        const std::string row = (a.code.empty() && a.title.empty() ? "(новый вид активности)" : a.code + "  " + a.title) + "##activity_" + std::to_string(a.id);
        if (ImGui::Selectable(row.c_str(), edited_activity_id_ == a.id)) {
            edited_activity_ = a;
            edited_activity_id_ = a.id;
        }
    }
    ImGui::NextColumn();
    if (edited_activity_id_ <= 0) {
        ImGui::TextDisabled("Выберите вид активности в списке или добавьте новый.");
        ImGui::Columns(1);
        return;
    }
    bool save = false;
    if (focus_first_editor_field_) { ImGui::SetKeyboardFocusHere(); focus_first_editor_field_ = false; }
    input_text("Название", edited_activity_.title); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_text("Буква/код", edited_activity_.code); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputDouble("Часы по умолчанию", &edited_activity_.default_hours, 0.1, 1.0, "%.2f"); save |= ImGui::IsItemDeactivatedAfterEdit();
    save |= ImGui::Checkbox("Учитывать в норме", &edited_activity_.affects_norm);
    input_multiline_wrapped("Описание", edited_activity_.description, 110.0f); save |= ImGui::IsItemDeactivatedAfterEdit();
    if (confirm_delete_button("activity", edited_activity_.title.empty() ? "(новый вид активности)" : edited_activity_.title.c_str())) {
        std::string error;
        if (db_.delete_activity(edited_activity_id_, error)) {
            edited_activity_ = ActivityKind{};
            edited_activity_id_ = 0;
            refresh();
            status_ = "Вид активности удален";
            last_error_.clear();
            ImGui::Columns(1);
            return;
        }
        autosave_status(false, error);
    }
    if (save) {
        std::string error;
        autosave_status(db_.save_activity(edited_activity_, error), error);
        edited_activity_id_ = edited_activity_.id;
        refresh();
    }
    ImGui::Columns(1);
}

void App::render_periods_tab() {
    ImGui::Columns(2, "periods_columns", true);
    if (ImGui::Button(" Добавить##period")) {
        edited_period_ = ActivityPeriod{};
        edited_period_.date_from = iso_date(context_.year, context_.month, 1);
        edited_period_.date_to = edited_period_.date_from;
        if (!employees_.empty()) edited_period_.employee_id = employees_.front().id;
        if (!activities_.empty()) {
            const auto it = std::find_if(activities_.begin(), activities_.end(), [](const ActivityKind& a) {
                return a.code != "Я" && a.code != "В";
            });
            const ActivityKind& activity = it == activities_.end() ? activities_.front() : *it;
            edited_period_.activity_id = activity.id;
            edited_period_.hours = activity.default_hours;
        }
        if (edited_period_.employee_id > 0 && edited_period_.activity_id > 0) {
            std::string error;
            if (db_.save_activity_period(edited_period_, error)) {
                edited_period_id_ = edited_period_.id;
                refresh();
                focus_first_editor_field_ = true;
                status_ = "Добавлен период активности";
                last_error_.clear();
            } else {
                autosave_status(false, error);
            }
        } else {
            status_ = "Сначала добавьте сотрудника и вид активности";
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Показаны все периоды из базы. Для табеля учитываются периоды, пересекающие выбранный месяц.");
    ImGui::Separator();
    for (auto& p : all_periods_) {
        const std::string row = p.date_from + " - " + p.date_to + "  " + p.employee_name + "  " + p.activity_code +
            "  |  создан: " + (p.created_at.empty() ? "-" : p.created_at);
        if (ImGui::Selectable(row.c_str(), edited_period_id_ == p.id)) {
            edited_period_ = p;
            edited_period_id_ = p.id;
        }
    }
    ImGui::NextColumn();
    if (edited_period_id_ <= 0) {
        ImGui::TextDisabled("Выберите период в списке или добавьте новый.");
        ImGui::Columns(1);
        return;
    }

    bool save = false;
    const char* employee_preview = "Сотрудник";
    for (const auto& e : employees_) {
        if (e.id == edited_period_.employee_id) employee_preview = e.full_name.c_str();
    }
    if (ImGui::BeginCombo("Сотрудник", employee_preview)) {
        for (const auto& e : employees_) {
            const bool selected = e.id == edited_period_.employee_id;
            if (ImGui::Selectable(e.full_name.c_str(), selected)) {
                edited_period_.employee_id = e.id;
                save = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const char* activity_preview = "Вид активности";
    for (const auto& a : activities_) {
        if (a.id == edited_period_.activity_id) activity_preview = a.title.c_str();
    }
    if (ImGui::BeginCombo("Активность", activity_preview)) {
        for (const auto& a : activities_) {
            const std::string row = a.code + "  " + a.title;
            const bool selected = a.id == edited_period_.activity_id;
            if (ImGui::Selectable(row.c_str(), selected)) {
                edited_period_.activity_id = a.id;
                edited_period_.hours = a.default_hours;
                save = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (focus_first_editor_field_) { ImGui::SetKeyboardFocusHere(); focus_first_editor_field_ = false; }
    save |= input_date("Дата с", edited_period_.date_from);
    save |= input_date("Дата по", edited_period_.date_to);
    ImGui::InputDouble("Часов в день", &edited_period_.hours, 0.1, 1.0, "%.2f"); save |= ImGui::IsItemDeactivatedAfterEdit();
    input_multiline_wrapped("Основание / примечание", edited_period_.note, 110.0f); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("Создан: %s", edited_period_.created_at.empty() ? "-" : edited_period_.created_at.c_str());
    const std::string period_delete_title = edited_period_.date_from + " - " + edited_period_.date_to;
    if (confirm_delete_button("period", period_delete_title.c_str())) {
        std::string error;
        if (db_.delete_activity_period(edited_period_id_, error)) {
            const bool marked = db_.mark_timesheets_need_refill(edited_period_, error);
            edited_period_ = ActivityPeriod{};
            edited_period_id_ = 0;
            refresh();
            if (marked) {
                status_ = "Период активности удален";
                last_error_.clear();
            } else {
                autosave_status(false, error);
            }
            ImGui::Columns(1);
            return;
        }
        autosave_status(false, error);
    }
    if (save && edited_period_.employee_id > 0 && edited_period_.activity_id > 0 && !edited_period_.date_from.empty() && !edited_period_.date_to.empty()) {
        std::string error;
        const bool saved = db_.save_activity_period(edited_period_, error) && db_.mark_timesheets_need_refill(edited_period_, error);
        autosave_status(saved, error);
        edited_period_id_ = edited_period_.id;
        refresh();
    }
    ImGui::Columns(1);
}

void App::render_norms_tab() {
    ImGui::Columns(2, "norms_columns", true);
    if (ImGui::Button(" Добавить##norm")) {
        edited_norm_ = WorkNorm{};
        std::string error;
        if (db_.save_norm(edited_norm_, error)) {
            edited_norm_id_ = edited_norm_.id;
            refresh();
            focus_first_editor_field_ = true;
            status_ = "Добавлена норма времени";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
    }
    for (auto& n : norms_) {
        const std::string row = (n.title.empty() ? "(новая норма)" : n.title) + "  " + std::to_string(n.rate) + "##norm_" + std::to_string(n.id);
        if (ImGui::Selectable(row.c_str(), edited_norm_id_ == n.id)) {
            edited_norm_ = n;
            edited_norm_id_ = n.id;
        }
    }
    ImGui::NextColumn();
    if (edited_norm_id_ <= 0) {
        ImGui::TextDisabled("Выберите норму в списке или добавьте новую.");
        ImGui::Columns(1);
        return;
    }
    bool save = false;
    if (focus_first_editor_field_) { ImGui::SetKeyboardFocusHere(); focus_first_editor_field_ = false; }
    input_text("Название", edited_norm_.title); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputDouble("Ставка", &edited_norm_.rate, 0.1, 0.25, "%.2f"); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputDouble("Часов в день", &edited_norm_.hours_per_day, 0.1, 1.0, "%.2f"); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputDouble("Часов в неделю", &edited_norm_.hours_per_week, 1.0, 4.0, "%.1f"); save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::InputInt("Дней в неделю", &edited_norm_.days_per_week); save |= ImGui::IsItemDeactivatedAfterEdit();
    save |= ImGui::Checkbox("Укороченный обед: пятница на 1 час короче", &edited_norm_.short_friday);
    if (confirm_delete_button("norm", edited_norm_.title.empty() ? "(новая норма)" : edited_norm_.title.c_str())) {
        std::string error;
        if (db_.delete_norm(edited_norm_id_, error)) {
            edited_norm_ = WorkNorm{};
            edited_norm_id_ = 0;
            refresh();
            status_ = "Норма времени удалена";
            last_error_.clear();
            ImGui::Columns(1);
            return;
        }
        autosave_status(false, error);
    }
    if (save) {
        std::string error;
        autosave_status(db_.save_norm(edited_norm_, error), error);
        edited_norm_id_ = edited_norm_.id;
        refresh();
    }
    ImGui::Columns(1);
}

void App::render_institution_tab() {
    bool changed = false;
    changed |= input_text("Наименование", edited_institution_.title);
    changed |= input_text("ОКПО", edited_institution_.okpo);
    changed |= input_text("ИНН", edited_institution_.inn);
    changed |= input_text("Адрес", edited_institution_.address);
    changed |= input_text("Структурное подразделение", edited_institution_.structural_unit);
    changed |= input_text("Ответственный / исполнитель", edited_institution_.responsible);
    changed |= input_text("Должность исполнителя", edited_institution_.executor_position);
    changed |= input_text("Код должности исполнителя", edited_institution_.executor_position_code);
    if (changed) {
        std::string error;
        autosave_status(db_.save_institution(edited_institution_, error), error);
    }
}

void App::render_calendar() {
    if (!ImGui::Begin(" Календарь", &show_calendar_)) {
        ImGui::End();
        return;
    }
    if (!db_.is_open()) {
        ImGui::TextUnformatted("Откройте базу.");
        ImGui::End();
        return;
    }

    if (calendar_year_loaded_ != context_.year || calendar_year_days_.empty()) {
        load_calendar_year(context_.year);
    }

    ImGui::BeginGroup();
    if (ImGui::ArrowButton("prev_calendar_year", ImGuiDir_Left)) {
        --context_.year;
        context_.year = std::clamp(context_.year, 1901, 2200);
        load_calendar_year(context_.year);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(92.0f);
    int selected_year = context_.year;
    if (ImGui::InputInt("Год", &selected_year, 0, 0)) {
        selected_year = std::clamp(selected_year, 1901, 2200);
        if (selected_year != context_.year) {
            context_.year = selected_year;
            load_calendar_year(context_.year);
        }
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("next_calendar_year", ImGuiDir_Right)) {
        ++context_.year;
        context_.year = std::clamp(context_.year, 1901, 2200);
        load_calendar_year(context_.year);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Импорт праздников и переносов")) {
        std::vector<CalendarDay> imported;
        std::string error;
        if (download_ru_calendar(context_.year, imported, error)) {
            bool ok = true;
            for (const auto& day : imported) {
                if (!db_.save_calendar_day(day, error)) {
                    ok = false;
                    break;
                }
            }
            autosave_status(ok, ok ? "" : error);
            load_calendar_year(context_.year);
            month_days_ = db_.calendar_days(context_.year, context_.month);
            if (ok) status_ = "Производственный календарь загружен";
        } else {
            autosave_status(false, error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(" Локальный календарь")) {
        std::string error;
        bool ok = true;
        for (int month = 1; month <= 12; ++month) {
            for (const auto& day : build_local_calendar(context_.year, month)) {
                if (!db_.save_calendar_day(day, error)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) break;
        }
        autosave_status(ok, ok ? "" : error);
        load_calendar_year(context_.year);
        month_days_ = db_.calendar_days(context_.year, context_.month);
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.52f, 0.76f, 1.00f, 1.0f), "Рабочий");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.70f, 0.82f, 1.0f), "Выходной");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.00f, 0.42f, 0.38f, 1.0f), "Праздник");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.00f, 0.76f, 0.22f, 1.0f), "Сокращенный");
    ImGui::Separator();

    const float editor_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.28f, 260.0f, 390.0f);
    ImGui::BeginChild("calendar_year_grid", ImVec2(-editor_width - ImGui::GetStyle().ItemSpacing.x, 0.0f), ImGuiChildFlags_Border);
    static constexpr const char* quarter_names[] = {"I квартал", "II квартал", "III квартал", "IV квартал"};
    static constexpr const char* weekdays[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
    for (int quarter = 0; quarter < 4; ++quarter) {
        ImGui::PushID(quarter);
        ImGui::SeparatorText(quarter_names[quarter]);
        if (ImGui::BeginTable("quarter_months", 3, ImGuiTableFlags_SizingStretchSame)) {
            for (int offset_month = 0; offset_month < 3; ++offset_month) {
                const int month = quarter * 3 + offset_month + 1;
                ImGui::TableNextColumn();
                ImGui::PushID(month);
                ImGui::SeparatorText(month_name(month));
                if (ImGui::BeginTable("month_days", 7, ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoHostExtendX)) {
                    for (const char* weekday : weekdays) {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", weekday);
                    }
                    const int offset = first_weekday_monday0(context_.year, month);
                    const int count = days_in_month(context_.year, month);
                    for (int cell = 0; cell < offset + count; ++cell) {
                        ImGui::TableNextColumn();
                        if (cell < offset) {
                            ImGui::Dummy(ImVec2(26.0f, 24.0f));
                            continue;
                        }
                        const int day_number = cell - offset + 1;
                        const std::string date = iso_date(context_.year, month, day_number);
                        CalendarDay* day = find_day(calendar_year_days_, date);
                        const bool selected = selected_calendar_date_ == date;
                        ImVec4 color = ImVec4(0.20f, 0.34f, 0.38f, 1.0f);
                        ImVec4 hover = ImVec4(0.26f, 0.45f, 0.50f, 1.0f);
                        ImVec4 active = ImVec4(0.14f, 0.52f, 0.47f, 1.0f);
                        if (day && day->weekend) {
                            color = ImVec4(0.34f, 0.40f, 0.50f, 0.92f);
                            hover = ImVec4(0.42f, 0.49f, 0.61f, 0.98f);
                            active = ImVec4(0.48f, 0.56f, 0.70f, 1.0f);
                        }
                        if (day && day->holiday) {
                            color = ImVec4(0.58f, 0.18f, 0.18f, 0.92f);
                            hover = ImVec4(0.72f, 0.24f, 0.22f, 0.98f);
                            active = ImVec4(0.82f, 0.30f, 0.27f, 1.0f);
                        }
                        if (day && day->shortened) {
                            color = ImVec4(0.76f, 0.52f, 0.12f, 0.90f);
                            hover = ImVec4(0.90f, 0.64f, 0.16f, 0.98f);
                            active = ImVec4(0.96f, 0.70f, 0.18f, 1.0f);
                        }
                        if (selected) {
                            color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
                            hover = color;
                            active = color;
                        }
                        ImGui::PushStyleColor(ImGuiCol_Button, color);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
                        if (ImGui::Button((std::to_string(day_number) + "##day").c_str(), ImVec2(28.0f, 24.0f))) {
                            selected_calendar_date_ = date;
                            context_.month = month;
                            month_days_ = db_.calendar_days(context_.year, context_.month);
                        }
                        ImGui::PopStyleColor(3);
                        if (day && !day->comment.empty() && ImGui::BeginItemTooltip()) {
                            ImGui::TextUnformatted(day->comment.c_str());
                            ImGui::EndTooltip();
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("calendar_day_editor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border);
    ImGui::SeparatorText("Ручная правка");
    CalendarDay* selected_day = find_day(calendar_year_days_, selected_calendar_date_);
    if (!selected_day) {
        ImGui::TextUnformatted("Выберите день в календаре.");
    } else {
        ImGui::Text("%s", selected_day->date.c_str());
        bool changed = false;
        changed |= ImGui::Checkbox("Выходной", &selected_day->weekend);
        changed |= ImGui::Checkbox("Праздник", &selected_day->holiday);
        changed |= ImGui::Checkbox("Сокращенный день", &selected_day->shortened);
        set_next_width();
        changed |= input_text("Код", selected_day->mark);
        set_next_width();
        changed |= input_text("Комментарий", selected_day->comment);
        if (changed) {
            std::string error;
            const bool ok = db_.save_calendar_day(*selected_day, error);
            autosave_status(ok, error);
            if (ok && month_from_date(selected_day->date) == context_.month) {
                month_days_ = db_.calendar_days(context_.year, context_.month);
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("Рабочий день##quick_workday")) {
            selected_day->weekend = false;
            selected_day->holiday = false;
            selected_day->shortened = false;
            selected_day->mark = "Я";
            selected_day->comment.clear();
            std::string error;
            autosave_status(db_.save_calendar_day(*selected_day, error), error);
            month_days_ = db_.calendar_days(context_.year, context_.month);
        }
        ImGui::SameLine();
        if (ImGui::Button("Выходной##quick_weekend")) {
            selected_day->weekend = true;
            selected_day->holiday = false;
            selected_day->shortened = false;
            selected_day->mark = "В";
            std::string error;
            autosave_status(db_.save_calendar_day(*selected_day, error), error);
            month_days_ = db_.calendar_days(context_.year, context_.month);
        }
        if (ImGui::Button("Сокращенный##quick_shortened")) {
            selected_day->weekend = false;
            selected_day->holiday = false;
            selected_day->shortened = true;
            selected_day->mark = "Я";
            if (selected_day->comment.empty()) selected_day->comment = "Сокращенный день";
            std::string error;
            autosave_status(db_.save_calendar_day(*selected_day, error), error);
            month_days_ = db_.calendar_days(context_.year, context_.month);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void App::render_settings() {
    if (!ImGui::Begin(" Настройки", &show_settings_)) {
        ImGui::End();
        return;
    }
    static constexpr const char* theme_names[] = {
        "Темная",
        "Светлая",
        "Классическая",
        "Светлая синяя",
        "Графит",
        "Лес",
        "Бордовая",
        "Высокий контраст"
    };
    static constexpr int theme_count = static_cast<int>(std::size(theme_names));
    int selected_theme = std::clamp(settings_.theme, 0, theme_count - 1);
    ImGui::TextUnformatted("Тема");
    if (ImGui::BeginCombo("##theme", theme_names[selected_theme])) {
        for (int i = 0; i < theme_count; ++i) {
            const bool selected = selected_theme == i;
            if (ImGui::Selectable(theme_names[i], selected)) {
                selected_theme = i;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (selected_theme != settings_.theme) {
        settings_.theme = selected_theme;
        apply_theme();
        save_settings(settings_);
    }

    float selected_font_size = settings_.font_size;
    ImGui::Spacing();
    ImGui::TextUnformatted("Размер шрифта");
    bool first_font_size = true;
    for (float size : font_size_options()) {
        if (!first_font_size) ImGui::SameLine();
        first_font_size = false;
        const std::string label = std::to_string(static_cast<int>(size));
        if (ImGui::RadioButton(label.c_str(), std::abs(selected_font_size - size) < 0.1f)) {
            selected_font_size = size;
        }
    }
    if (std::abs(selected_font_size - settings_.font_size) >= 0.1f) {
        settings_.font_size = selected_font_size;
        apply_font_size();
        save_settings(settings_);
    }
    ImGui::Spacing();
    if (ImGui::Checkbox("Автоматический бэкап базы раз в месяц", &settings_.auto_backup_enabled)) {
        save_settings(settings_);
    }
    ImGui::Text("Файл настроек: %s", settings_path().c_str());
    ImGui::End();
}

void App::render_about() {
    if (!ImGui::Begin(" О программе", &show_about_)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Табель 0504421");
    ImGui::TextUnformatted("C++20, CMake, SQLite, Dear ImGui, ImGuiFileDialog.");
    ImGui::TextWrapped("Назначение: быстрый учет рабочего времени учреждения, подготовка данных для печатной формы 0504421, ведение справочников и производственного календаря.");
    ImGui::End();
}

void App::render_file_dialogs() {
    const ImVec2 min_size(640, 360);
    const ImVec2 max_size(1000, 700);
    if (IGFD::FileDialog::Instance()->Display("OpenDb", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
        if (IGFD::FileDialog::Instance()->IsOk()) open_database(IGFD::FileDialog::Instance()->GetFilePathName());
        IGFD::FileDialog::Instance()->Close();
    }
    if (IGFD::FileDialog::Instance()->Display("CreateDb", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
        if (IGFD::FileDialog::Instance()->IsOk()) create_database(IGFD::FileDialog::Instance()->GetFilePathName());
        IGFD::FileDialog::Instance()->Close();
    }
    if (IGFD::FileDialog::Instance()->Display("SaveDbAs", ImGuiWindowFlags_NoCollapse, min_size, max_size)) {
        if (IGFD::FileDialog::Instance()->IsOk()) save_database_as(IGFD::FileDialog::Instance()->GetFilePathName());
        IGFD::FileDialog::Instance()->Close();
    }
}
