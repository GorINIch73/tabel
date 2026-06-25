#pragma once

#include "database.hpp"
#include "settings.hpp"

#include <string>
#include <vector>

class App {
public:
    App();
    ~App();

    void render();
    bool should_close() const { return should_close_; }

private:
    void apply_theme();
    void apply_font_size();
    void open_database(const std::string& path);
    void create_database(const std::string& path);
    bool auto_backup_database(std::string& error);
    bool save_pending_changes(std::string& error);
    void save_database_as(const std::string& path);
    void refresh();
    void autosave_status(bool ok, const std::string& error);
    void load_calendar_year(int year);
    bool print_timesheet_html(const TimesheetDocument& document, std::string& output_path, std::string& error);
    bool export_timesheet_ods(const TimesheetDocument& document, std::string& output_path, std::string& error);

    void render_menu();
    void render_dockspace();
    void render_dashboard();
    void render_timesheets();
    void render_timesheet_grid();
    void render_persons_window();
    void render_positions_window();
    void render_employees_window();
    void render_activities_window();
    void render_periods_window();
    void render_norms_window();
    void render_institution_window();
    void render_calendar();
    void render_settings();
    void render_about();
    void render_file_dialogs();

    void render_persons_tab();
    void render_positions_tab();
    void render_employees_tab();
    void render_activities_tab();
    void render_periods_tab();
    void render_norms_tab();
    void render_institution_tab();

    Database db_;
    UserSettings settings_;
    MonthContext context_;
    std::vector<Person> persons_;
    std::vector<Position> positions_;
    std::vector<Employee> employees_;
    std::vector<ActivityKind> activities_;
    std::vector<ActivityPeriod> periods_;
    std::vector<ActivityPeriod> all_periods_;
    std::vector<ActivityPeriod> upcoming_periods_;
    std::vector<WorkNorm> norms_;
    std::vector<TimesheetDocument> timesheet_documents_;
    std::vector<CalendarDay> month_days_;
    std::vector<CalendarDay> calendar_year_days_;

    Person edited_person_;
    Position edited_position_;
    Employee edited_employee_;
    ActivityKind edited_activity_;
    ActivityPeriod edited_period_;
    WorkNorm edited_norm_;
    TimesheetDocument edited_timesheet_document_;
    Institution edited_institution_;

    int edited_person_id_ = 0;
    int edited_position_id_ = 0;
    int selected_employee_id_ = 0;
    int edited_employee_id_ = 0;
    int edited_activity_id_ = 0;
    int edited_period_id_ = 0;
    int edited_norm_id_ = 0;
    int edited_timesheet_document_id_ = 0;
    int calendar_year_loaded_ = 0;
    std::string employee_filter_;
    std::string selected_calendar_date_;
    std::string status_ = "База не открыта";
    std::string last_error_;
    bool dock_layout_built_ = false;
    bool focus_first_editor_field_ = false;
    float timesheets_list_height_ = 0.0f;
    bool show_dashboard_ = true;
    bool show_timesheets_ = false;
    bool show_persons_ = false;
    bool show_positions_ = false;
    bool show_employees_ = false;
    bool show_activities_ = false;
    bool show_periods_ = false;
    bool show_norms_ = false;
    bool show_institution_ = false;
    bool show_calendar_ = false;
    bool show_settings_ = false;
    bool show_about_ = false;
    bool should_close_ = false;
    bool show_demo_ = false;
};
