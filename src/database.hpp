#pragma once

#include "models.hpp"

#include <sqlite3.h>

#include <optional>
#include <string>
#include <vector>

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& path, std::string& error);
    void close();
    bool is_open() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }

    std::vector<Person> persons() const;
    std::vector<Position> positions() const;
    std::vector<Employee> employees() const;
    std::vector<ActivityKind> activities() const;
    std::vector<ActivityPeriod> activity_periods(int year, int month) const;
    std::vector<ActivityPeriod> all_activity_periods() const;
    std::vector<ActivityPeriod> upcoming_activity_periods(int limit) const;
    std::vector<WorkNorm> norms() const;
    Institution institution() const;
    std::vector<TimesheetDocument> timesheet_documents() const;
    std::vector<CalendarDay> calendar_days(int year, int month) const;
    std::optional<TimesheetCell> timesheet_cell(int employee_id, const std::string& date) const;

    bool save_person(Person& person, std::string& error);
    bool save_position(Position& position, std::string& error);
    bool save_employee(Employee& employee, std::string& error);
    bool save_activity(ActivityKind& activity, std::string& error);
    bool save_activity_period(ActivityPeriod& period, std::string& error);
    bool save_timesheet_document(TimesheetDocument& document, std::string& error);
    bool mark_timesheets_need_refill(const ActivityPeriod& period, std::string& error);
    bool create_or_refill_timesheet_document(TimesheetDocument& document, std::string& error);
    bool refill_timesheet(int year, int month, std::string& error);
    bool save_norm(WorkNorm& norm, std::string& error);
    bool save_institution(const Institution& institution, std::string& error);
    bool save_calendar_day(const CalendarDay& day, std::string& error);
    bool save_timesheet_cell(const TimesheetCell& cell, std::string& error);
    bool delete_person(int id, std::string& error);
    bool delete_position(int id, std::string& error);
    bool delete_employee(int id, std::string& error);
    bool delete_activity(int id, std::string& error);
    bool delete_activity_period(int id, std::string& error);
    bool delete_timesheet_document(const TimesheetDocument& document, std::string& error);
    bool delete_norm(int id, std::string& error);

private:
    bool exec(const std::string& sql, std::string& error) const;
    bool migrate(std::string& error);
    bool seed_defaults(std::string& error);

    sqlite3* db_ = nullptr;
    std::string path_;
};
