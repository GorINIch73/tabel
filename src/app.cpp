#include "app.hpp"

#include "calendar.hpp"
#include "widgets.hpp"

#include <ImGuiFileDialog.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
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

double work_hours_for_day(const WorkNorm* norm, const std::string& date) {
    if (!norm) return 8.0;
    double hours = norm->hours_per_day;
    if (norm->short_friday) {
        hours += is_friday(date) ? -0.8 : 0.2;
    }
    return std::max(0.0, hours);
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

}

App::App() {
    settings_ = load_settings();
    context_.year = settings_.last_year;
    context_.month = settings_.last_month;
    apply_theme();
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

    if (settings_.theme == 1) {
        ImGui::StyleColorsLight();
    } else {
        ImGui::StyleColorsDark();
        auto& c = style.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.12f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.20f, 0.34f, 0.38f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.43f, 0.48f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.18f, 0.31f, 0.35f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.45f, 0.49f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.52f, 0.47f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.18f, 0.35f, 0.38f, 1.0f);
    }
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
    status_ = "Открыта база: " + path;
}

void App::create_database(const std::string& path) {
    open_database(path);
}

void App::refresh() {
    if (!db_.is_open()) return;
    persons_ = db_.persons();
    positions_ = db_.positions();
    employees_ = db_.employees();
    activities_ = db_.activities();
    periods_ = db_.activity_periods(context_.year, context_.month);
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
    if (selected_employee_id_ == 0 && !employees_.empty()) selected_employee_id_ = employees_.front().id;
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
        if (ImGui::BeginMenu(" Недавние базы", !settings_.recent_databases.empty())) {
            for (const auto& recent : settings_.recent_databases) {
                if (ImGui::MenuItem(recent.c_str())) open_database(recent);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
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
    if (ImGui::BeginMenu(" Прочее")) {
        ImGui::MenuItem(" Настройки", nullptr, &show_settings_);
        ImGui::MenuItem(" О программе", nullptr, &show_about_);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    ImGui::TextUnformatted(db_.is_open() ? db_.path().c_str() : "База не выбрана");
    ImGui::Separator();
    ImGui::TextUnformatted(status_.c_str());
    if (!last_error_.empty()) help_marker(last_error_.c_str());
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

    int active_employees = 0;
    for (const auto& e : employees_) {
        if (e.active) ++active_employees;
    }

    if (ImGui::BeginTable("dashboard_summary", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Физлица");
        ImGui::Text("%d", static_cast<int>(persons_.size()));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Сотрудники");
        ImGui::Text("%d / %d активных", active_employees, static_cast<int>(employees_.size()));
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("Документы месяца");
        ImGui::Text("%d", static_cast<int>(periods_.size()));
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("Период");
        ImGui::Text("%s %d", month_name(context_.month), context_.year);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button(" Активности сотрудников")) show_periods_ = true;
    ImGui::SameLine();
    if (ImGui::Button(" Список табелей")) show_timesheets_ = true;
    ImGui::SameLine();
    if (ImGui::Button(" Сотрудники")) show_employees_ = true;
    ImGui::SameLine();
    if (ImGui::Button(" Виды активности")) show_activities_ = true;
    ImGui::SameLine();
    if (ImGui::Button(" Создать/перезаполнить текущий месяц")) {
        TimesheetDocument document;
        document.year = context_.year;
        document.month = context_.month;
        document.title = "Табель";
        std::string error;
        if (db_.create_or_refill_timesheet_document(document, error)) {
            refresh();
            status_ = "Табель создан и заполнен по текущим данным";
            last_error_.clear();
        } else {
            autosave_status(false, error);
        }
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
        status_ = "Печать 0504421 будет добавлена следующим этапом";
    }
    ImGui::SameLine();
    if (ImGui::Button(" Отчет")) {
        status_ = "Отчеты будут добавлены следующим этапом";
    }
    ImGui::SameLine();
    if (ImGui::Button(" Экспорт")) {
        status_ = "Экспорт будет добавлен следующим этапом";
    }

    const float available_height = ImGui::GetContentRegionAvail().y;
    if (timesheets_list_height_ <= 0.0f) {
        timesheets_list_height_ = std::min(170.0f, available_height * 0.32f);
    }
    timesheets_list_height_ = std::clamp(timesheets_list_height_, 90.0f, std::max(90.0f, available_height - 320.0f));

    ImGui::BeginChild("timesheets_list", ImVec2(0, timesheets_list_height_), ImGuiChildFlags_Border);
    bool select_timesheet_document = false;
    TimesheetDocument selected_timesheet_document;
    if (ImGui::BeginTable("timesheets_documents_table", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Год", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("Месяц", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Наименование");
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
            ImGui::TextUnformatted(d.needs_refill ? "!" : "");
            ImGui::TableSetColumnIndex(5);
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
                auto cell = db_.timesheet_cell(e.id, day.date).value_or(TimesheetCell{e.id, day.date, day.mark.empty() ? "Я" : day.mark, day.weekend ? 0.0 : work_hours_for_day(norm, day.date), ""});
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
    for (auto& p : periods_) {
        const std::string row = p.date_from + " - " + p.date_to + "  " + p.employee_name + "  " + p.activity_code;
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
    changed |= input_text("Ответственный", edited_institution_.responsible);
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
    if (ImGui::BeginTable("calendar_table", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Дата");
        ImGui::TableSetupColumn("Выходной");
        ImGui::TableSetupColumn("Праздник");
        ImGui::TableSetupColumn("Сокр.");
        ImGui::TableSetupColumn("Код");
        ImGui::TableSetupColumn("Комментарий");
        ImGui::TableHeadersRow();
        for (auto& d : month_days_) {
            ImGui::TableNextRow();
            ImGui::PushID(d.date.c_str());
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(d.date.c_str());
            bool changed = false;
            ImGui::TableSetColumnIndex(1); changed |= ImGui::Checkbox("##weekend", &d.weekend);
            ImGui::TableSetColumnIndex(2); changed |= ImGui::Checkbox("##holiday", &d.holiday);
            ImGui::TableSetColumnIndex(3); changed |= ImGui::Checkbox("##short", &d.shortened);
            ImGui::TableSetColumnIndex(4); set_next_width(); changed |= input_text("##mark", d.mark);
            ImGui::TableSetColumnIndex(5); set_next_width(); changed |= input_text("##comment", d.comment);
            if (changed) {
                std::string error;
                autosave_status(db_.save_calendar_day(d, error), error);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::render_settings() {
    if (!ImGui::Begin(" Настройки", &show_settings_)) {
        ImGui::End();
        return;
    }
    int theme = settings_.theme;
    ImGui::RadioButton("Темная", &theme, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Светлая", &theme, 1);
    if (theme != settings_.theme) {
        settings_.theme = theme;
        apply_theme();
        save_settings(settings_);
    }
    ImGui::Checkbox("Показать ImGui demo", &show_demo_);
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
}
