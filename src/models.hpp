#pragma once

#include <string>
#include <vector>

struct Person {
    int id = 0;
    std::string full_name;
    std::string birth_date;
    std::string snils;
    std::string phone;
    std::string email;
    std::string note;
};

struct Position {
    int id = 0;
    std::string title;
    std::string category;
    std::string note;
};

struct Employee {
    int id = 0;
    int person_id = 0;
    int position_id = 0;
    int norm_id = 0;
    std::string full_name;
    std::string position;
    std::string norm_title;
    std::string department;
    std::string personnel_no;
    std::string hire_date;
    std::string dismissal_date;
    std::string note;
    bool active = true;
};

struct ActivityKind {
    int id = 0;
    std::string title;
    std::string code;
    std::string description;
    bool affects_norm = true;
    double default_hours = 0.0;
};

struct WorkNorm {
    int id = 0;
    std::string title;
    double rate = 1.0;
    double hours_per_day = 8.0;
    double hours_per_week = 40.0;
    int days_per_week = 5;
    bool short_friday = false;
};

struct Institution {
    std::string title;
    std::string okpo;
    std::string inn;
    std::string address;
    std::string structural_unit;
    std::string responsible;
    std::string executor_position;
    std::string executor_position_code;
};

struct CalendarDay {
    std::string date;
    int day = 1;
    bool weekend = false;
    bool holiday = false;
    bool shortened = false;
    std::string mark;
    std::string comment;
};

struct TimesheetCell {
    int employee_id = 0;
    std::string date;
    std::string code;
    double hours = 8.0;
    std::string note;
};

struct TimesheetDocument {
    int id = 0;
    int year = 2026;
    int month = 6;
    std::string title;
    bool accepted = false;
    bool needs_refill = false;
    std::string created_at;
    std::string note;
};

struct ActivityPeriod {
    int id = 0;
    int employee_id = 0;
    int activity_id = 0;
    std::string employee_name;
    std::string activity_title;
    std::string activity_code;
    std::string date_from;
    std::string date_to;
    double hours = 0.0;
    std::string note;
    std::string created_at;
};

struct MonthContext {
    int year = 2026;
    int month = 6;
    Institution institution;
};
