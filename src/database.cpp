#include "database.hpp"

#include "calendar.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <map>
#include <sstream>

namespace {
std::string text_column(sqlite3_stmt* stmt, int col) {
    const auto* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}

bool bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

struct Statement {
    sqlite3_stmt* stmt = nullptr;
    ~Statement() { if (stmt) sqlite3_finalize(stmt); }
};

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

double work_hours_for_day(const WorkNorm& norm, const CalendarDay& day) {
    double hours = norm.hours_per_day;
    if (norm.short_friday) {
        hours += is_friday(day.date) ? -0.8 : 0.2;
    }
    if (day.shortened) {
        hours -= 1.0;
    }
    return std::max(0.0, hours);
}

bool exists_row(sqlite3* db, const char* sql, int id, std::string& error) {
    Statement st;
    if (sqlite3_prepare_v2(db, sql, -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db); return false; }
    sqlite3_bind_int(st.stmt, 1, id);
    return sqlite3_step(st.stmt) == SQLITE_ROW;
}

}

Database::~Database() {
    close();
}

bool Database::open(const std::string& path, std::string& error) {
    close();
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        error = sqlite3_errmsg(db_);
        close();
        return false;
    }
    path_ = path;
    exec("PRAGMA foreign_keys = ON;", error);
    exec("PRAGMA journal_mode = WAL;", error);
    if (!initialize_schema(error) || !seed_defaults(error)) {
        close();
        return false;
    }
    return true;
}

void Database::close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    path_.clear();
}

bool Database::save_as(const std::string& path, std::string& error) const {
    if (!db_) {
        error = "База не открыта";
        return false;
    }
    sqlite3* target = nullptr;
    if (sqlite3_open(path.c_str(), &target) != SQLITE_OK) {
        error = target ? sqlite3_errmsg(target) : "Не удалось создать файл базы";
        if (target) sqlite3_close(target);
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(target, "main", db_, "main");
    if (!backup) {
        error = sqlite3_errmsg(target);
        sqlite3_close(target);
        return false;
    }
    const int step = sqlite3_backup_step(backup, -1);
    const int finish = sqlite3_backup_finish(backup);
    if (step != SQLITE_DONE || finish != SQLITE_OK) {
        error = sqlite3_errmsg(target);
        sqlite3_close(target);
        return false;
    }
    if (sqlite3_close(target) != SQLITE_OK) {
        error = "Не удалось закрыть сохраненную базу";
        return false;
    }
    return true;
}

bool Database::exec(const std::string& sql, std::string& error) const {
    char* raw_error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &raw_error) != SQLITE_OK) {
        error = raw_error ? raw_error : sqlite3_errmsg(db_);
        sqlite3_free(raw_error);
        return false;
    }
    return true;
}

bool Database::initialize_schema(std::string& error) {
    static constexpr const char* schema = R"sql(
CREATE TABLE IF NOT EXISTS institution (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    title TEXT NOT NULL DEFAULT '',
    okpo TEXT NOT NULL DEFAULT '',
    inn TEXT NOT NULL DEFAULT '',
    address TEXT NOT NULL DEFAULT '',
    structural_unit TEXT NOT NULL DEFAULT '',
    responsible TEXT NOT NULL DEFAULT '',
    executor_position TEXT NOT NULL DEFAULT '',
    executor_position_code TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS persons (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    full_name TEXT NOT NULL,
    birth_date TEXT NOT NULL DEFAULT '',
    snils TEXT NOT NULL DEFAULT '',
    phone TEXT NOT NULL DEFAULT '',
    email TEXT NOT NULL DEFAULT '',
    note TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS positions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    category TEXT NOT NULL DEFAULT '',
    note TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS employees (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    person_id INTEGER REFERENCES persons(id) ON DELETE SET NULL,
    position_id INTEGER REFERENCES positions(id) ON DELETE SET NULL,
    norm_id INTEGER REFERENCES work_norms(id) ON DELETE SET NULL,
    full_name TEXT NOT NULL,
    position TEXT NOT NULL DEFAULT '',
    department TEXT NOT NULL DEFAULT '',
    personnel_no TEXT NOT NULL DEFAULT '',
    hire_date TEXT NOT NULL DEFAULT '',
    dismissal_date TEXT NOT NULL DEFAULT '',
    note TEXT NOT NULL DEFAULT '',
    active INTEGER NOT NULL DEFAULT 1
);
CREATE TABLE IF NOT EXISTS activity_kinds (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    code TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    affects_norm INTEGER NOT NULL DEFAULT 1,
    default_hours REAL NOT NULL DEFAULT 0.0
);
CREATE TABLE IF NOT EXISTS activity_periods (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE CASCADE,
    activity_id INTEGER NOT NULL REFERENCES activity_kinds(id) ON DELETE RESTRICT,
    date_from TEXT NOT NULL,
    date_to TEXT NOT NULL,
    hours REAL NOT NULL DEFAULT 0.0,
    note TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS work_norms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    rate REAL NOT NULL DEFAULT 1.0,
    hours_per_day REAL NOT NULL DEFAULT 8.0,
    hours_per_week REAL NOT NULL DEFAULT 40.0,
    days_per_week INTEGER NOT NULL DEFAULT 5,
    short_friday INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS calendar_days (
    date TEXT PRIMARY KEY,
    day INTEGER NOT NULL,
    weekend INTEGER NOT NULL DEFAULT 0,
    holiday INTEGER NOT NULL DEFAULT 0,
    shortened INTEGER NOT NULL DEFAULT 0,
    mark TEXT NOT NULL DEFAULT '',
    comment TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS timesheet_documents (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    year INTEGER NOT NULL,
    month INTEGER NOT NULL,
    title TEXT NOT NULL DEFAULT '',
    accepted INTEGER NOT NULL DEFAULT 0,
    needs_refill INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    note TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS timesheet_cells (
    employee_id INTEGER NOT NULL REFERENCES employees(id) ON DELETE CASCADE,
    date TEXT NOT NULL,
    code TEXT NOT NULL DEFAULT 'Я',
    hours REAL NOT NULL DEFAULT 8.0,
    note TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (employee_id, date)
);
INSERT OR IGNORE INTO institution (id, title) VALUES (1, '');
PRAGMA user_version = 1;
)sql";
    return exec(schema, error);
}

bool Database::seed_defaults(std::string& error) {
    static constexpr std::array<const char*, 11> rows = {
        "INSERT OR IGNORE INTO activity_kinds (id, title, code, description, affects_norm, default_hours) VALUES (1, 'Работа', 'Я', 'Явка, рабочее время', 1, 8.0);",
        "INSERT OR IGNORE INTO activity_kinds (id, title, code, description, affects_norm, default_hours) VALUES (2, 'Выходной', 'В', 'Выходной или праздничный день', 0, 0.0);",
        "INSERT OR IGNORE INTO activity_kinds (id, title, code, description, affects_norm, default_hours) VALUES (3, 'Основной отпуск', 'ОТ', 'Ежегодный основной оплачиваемый отпуск', 0, 0.0);",
        "INSERT OR IGNORE INTO activity_kinds (id, title, code, description, affects_norm, default_hours) VALUES (4, 'Отпуск ЧАЭС', 'ЧАЭС', 'Дополнительный отпуск', 0, 0.0);",
        "INSERT OR IGNORE INTO activity_kinds (id, title, code, description, affects_norm, default_hours) VALUES (5, 'Больничный', 'Б', 'Временная нетрудоспособность', 0, 0.0);",
        "INSERT OR IGNORE INTO activity_kinds (id, title, code, description, affects_norm, default_hours) VALUES (6, 'За свой счет', 'ДО', 'Отпуск без сохранения зарплаты', 0, 0.0);",
        "INSERT OR IGNORE INTO positions (id, title, category, note) VALUES (1, 'Специалист', 'Основной персонал', 'Базовая должность для новой базы');",
        "INSERT OR IGNORE INTO positions (id, title, category, note) VALUES (2, 'Бухгалтер', 'Административный персонал', '');",
        "INSERT OR IGNORE INTO positions (id, title, category, note) VALUES (3, 'Руководитель', 'Руководители', '');",
        "INSERT OR IGNORE INTO work_norms (id, title, rate, hours_per_day, hours_per_week, days_per_week, short_friday) VALUES (1, 'Полная ставка', 1.0, 8.0, 40.0, 5, 0);",
        "INSERT OR IGNORE INTO work_norms (id, title, rate, hours_per_day, hours_per_week, days_per_week, short_friday) VALUES (2, 'Половина ставки', 0.5, 4.0, 20.0, 5, 0);"
    };
    for (const char* sql : rows) {
        if (!exec(sql, error)) return false;
    }
    return true;
}

std::vector<Person> Database::persons() const {
    std::vector<Person> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT id, full_name, birth_date, snils, phone, email, note FROM persons ORDER BY full_name;", -1, &st.stmt, nullptr);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        Person p;
        p.id = sqlite3_column_int(st.stmt, 0);
        p.full_name = text_column(st.stmt, 1);
        p.birth_date = text_column(st.stmt, 2);
        p.snils = text_column(st.stmt, 3);
        p.phone = text_column(st.stmt, 4);
        p.email = text_column(st.stmt, 5);
        p.note = text_column(st.stmt, 6);
        out.push_back(p);
    }
    return out;
}

std::vector<Position> Database::positions() const {
    std::vector<Position> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT id, title, category, note FROM positions ORDER BY title;", -1, &st.stmt, nullptr);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        Position p;
        p.id = sqlite3_column_int(st.stmt, 0);
        p.title = text_column(st.stmt, 1);
        p.category = text_column(st.stmt, 2);
        p.note = text_column(st.stmt, 3);
        out.push_back(p);
    }
    return out;
}

std::vector<Employee> Database::employees() const {
    std::vector<Employee> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT e.id, e.person_id, e.position_id, e.norm_id, COALESCE(NULLIF(p.full_name, ''), e.full_name), COALESCE(pos.title, e.position), COALESCE(wn.title, ''), e.department, e.personnel_no, e.hire_date, e.dismissal_date, e.note, e.active FROM employees e LEFT JOIN persons p ON p.id = e.person_id LEFT JOIN positions pos ON pos.id = e.position_id LEFT JOIN work_norms wn ON wn.id = e.norm_id ORDER BY e.active DESC, COALESCE(NULLIF(p.full_name, ''), e.full_name);", -1, &st.stmt, nullptr);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        Employee e;
        e.id = sqlite3_column_int(st.stmt, 0);
        e.person_id = sqlite3_column_int(st.stmt, 1);
        e.position_id = sqlite3_column_int(st.stmt, 2);
        e.norm_id = sqlite3_column_int(st.stmt, 3);
        e.full_name = text_column(st.stmt, 4);
        e.position = text_column(st.stmt, 5);
        e.norm_title = text_column(st.stmt, 6);
        e.department = text_column(st.stmt, 7);
        e.personnel_no = text_column(st.stmt, 8);
        e.hire_date = text_column(st.stmt, 9);
        e.dismissal_date = text_column(st.stmt, 10);
        e.note = text_column(st.stmt, 11);
        e.active = sqlite3_column_int(st.stmt, 12) != 0;
        out.push_back(e);
    }
    return out;
}

std::vector<ActivityKind> Database::activities() const {
    std::vector<ActivityKind> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT id, title, code, description, affects_norm, default_hours FROM activity_kinds ORDER BY id;", -1, &st.stmt, nullptr);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        ActivityKind a;
        a.id = sqlite3_column_int(st.stmt, 0);
        a.title = text_column(st.stmt, 1);
        a.code = text_column(st.stmt, 2);
        a.description = text_column(st.stmt, 3);
        a.affects_norm = sqlite3_column_int(st.stmt, 4) != 0;
        a.default_hours = sqlite3_column_double(st.stmt, 5);
        out.push_back(a);
    }
    return out;
}

std::vector<ActivityPeriod> Database::activity_periods(int year, int month) const {
    std::vector<ActivityPeriod> out;
    const std::string from = iso_date(year, month, 1);
    const std::string to = iso_date(year, month, days_in_month(year, month));
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT ap.id, ap.employee_id, ap.activity_id, COALESCE(NULLIF(p.full_name, ''), e.full_name), ak.title, ak.code, ap.date_from, ap.date_to, ap.hours, ap.note, ap.created_at FROM activity_periods ap JOIN employees e ON e.id = ap.employee_id LEFT JOIN persons p ON p.id = e.person_id JOIN activity_kinds ak ON ak.id = ap.activity_id WHERE ap.date_from <= ? AND ap.date_to >= ? ORDER BY ap.date_from, ap.id;", -1, &st.stmt, nullptr);
    bind_text(st.stmt, 1, to);
    bind_text(st.stmt, 2, from);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        ActivityPeriod p;
        p.id = sqlite3_column_int(st.stmt, 0);
        p.employee_id = sqlite3_column_int(st.stmt, 1);
        p.activity_id = sqlite3_column_int(st.stmt, 2);
        p.employee_name = text_column(st.stmt, 3);
        p.activity_title = text_column(st.stmt, 4);
        p.activity_code = text_column(st.stmt, 5);
        p.date_from = text_column(st.stmt, 6);
        p.date_to = text_column(st.stmt, 7);
        p.hours = sqlite3_column_double(st.stmt, 8);
        p.note = text_column(st.stmt, 9);
        p.created_at = text_column(st.stmt, 10);
        out.push_back(p);
    }
    return out;
}

std::vector<ActivityPeriod> Database::all_activity_periods() const {
    std::vector<ActivityPeriod> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT ap.id, ap.employee_id, ap.activity_id, COALESCE(NULLIF(p.full_name, ''), e.full_name), ak.title, ak.code, ap.date_from, ap.date_to, ap.hours, ap.note, ap.created_at FROM activity_periods ap JOIN employees e ON e.id = ap.employee_id LEFT JOIN persons p ON p.id = e.person_id JOIN activity_kinds ak ON ak.id = ap.activity_id ORDER BY ap.date_from DESC, ap.date_to DESC, ap.id DESC;", -1, &st.stmt, nullptr);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        ActivityPeriod p;
        p.id = sqlite3_column_int(st.stmt, 0);
        p.employee_id = sqlite3_column_int(st.stmt, 1);
        p.activity_id = sqlite3_column_int(st.stmt, 2);
        p.employee_name = text_column(st.stmt, 3);
        p.activity_title = text_column(st.stmt, 4);
        p.activity_code = text_column(st.stmt, 5);
        p.date_from = text_column(st.stmt, 6);
        p.date_to = text_column(st.stmt, 7);
        p.hours = sqlite3_column_double(st.stmt, 8);
        p.note = text_column(st.stmt, 9);
        p.created_at = text_column(st.stmt, 10);
        out.push_back(p);
    }
    return out;
}

std::vector<ActivityPeriod> Database::upcoming_activity_periods(int limit) const {
    std::vector<ActivityPeriod> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT ap.id, ap.employee_id, ap.activity_id, COALESCE(NULLIF(p.full_name, ''), e.full_name), ak.title, ak.code, ap.date_from, ap.date_to, ap.hours, ap.note, ap.created_at FROM activity_periods ap JOIN employees e ON e.id = ap.employee_id LEFT JOIN persons p ON p.id = e.person_id JOIN activity_kinds ak ON ak.id = ap.activity_id WHERE ap.date_to >= date('now', 'localtime') ORDER BY CASE WHEN ap.date_from < date('now', 'localtime') THEN date('now', 'localtime') ELSE ap.date_from END, ap.date_to, ap.id LIMIT ?;", -1, &st.stmt, nullptr);
    sqlite3_bind_int(st.stmt, 1, limit);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        ActivityPeriod p;
        p.id = sqlite3_column_int(st.stmt, 0);
        p.employee_id = sqlite3_column_int(st.stmt, 1);
        p.activity_id = sqlite3_column_int(st.stmt, 2);
        p.employee_name = text_column(st.stmt, 3);
        p.activity_title = text_column(st.stmt, 4);
        p.activity_code = text_column(st.stmt, 5);
        p.date_from = text_column(st.stmt, 6);
        p.date_to = text_column(st.stmt, 7);
        p.hours = sqlite3_column_double(st.stmt, 8);
        p.note = text_column(st.stmt, 9);
        p.created_at = text_column(st.stmt, 10);
        out.push_back(p);
    }
    return out;
}

std::vector<WorkNorm> Database::norms() const {
    std::vector<WorkNorm> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT id, title, rate, hours_per_day, hours_per_week, days_per_week, short_friday FROM work_norms ORDER BY rate DESC, title;", -1, &st.stmt, nullptr);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        WorkNorm n;
        n.id = sqlite3_column_int(st.stmt, 0);
        n.title = text_column(st.stmt, 1);
        n.rate = sqlite3_column_double(st.stmt, 2);
        n.hours_per_day = sqlite3_column_double(st.stmt, 3);
        n.hours_per_week = sqlite3_column_double(st.stmt, 4);
        n.days_per_week = sqlite3_column_int(st.stmt, 5);
        n.short_friday = sqlite3_column_int(st.stmt, 6) != 0;
        out.push_back(n);
    }
    return out;
}

Institution Database::institution() const {
    Institution i;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT title, okpo, inn, address, structural_unit, responsible, executor_position, executor_position_code FROM institution WHERE id = 1;", -1, &st.stmt, nullptr);
    if (sqlite3_step(st.stmt) == SQLITE_ROW) {
        i.title = text_column(st.stmt, 0);
        i.okpo = text_column(st.stmt, 1);
        i.inn = text_column(st.stmt, 2);
        i.address = text_column(st.stmt, 3);
        i.structural_unit = text_column(st.stmt, 4);
        i.responsible = text_column(st.stmt, 5);
        i.executor_position = text_column(st.stmt, 6);
        i.executor_position_code = text_column(st.stmt, 7);
    }
    return i;
}

std::vector<TimesheetDocument> Database::timesheet_documents() const {
    std::vector<TimesheetDocument> out;
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT id, year, month, title, accepted, needs_refill, created_at, note FROM timesheet_documents ORDER BY year DESC, month DESC;", -1, &st.stmt, nullptr);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        TimesheetDocument d;
        d.id = sqlite3_column_int(st.stmt, 0);
        d.year = sqlite3_column_int(st.stmt, 1);
        d.month = sqlite3_column_int(st.stmt, 2);
        d.title = text_column(st.stmt, 3);
        d.accepted = sqlite3_column_int(st.stmt, 4) != 0;
        d.needs_refill = sqlite3_column_int(st.stmt, 5) != 0;
        d.created_at = text_column(st.stmt, 6);
        d.note = text_column(st.stmt, 7);
        out.push_back(d);
    }
    return out;
}

std::vector<CalendarDay> Database::calendar_days(int year, int month) const {
    std::vector<CalendarDay> out;
    char prefix[16]{};
    std::snprintf(prefix, sizeof(prefix), "%04d-%02d-%%", year, month);
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT date, day, weekend, holiday, shortened, mark, comment FROM calendar_days WHERE date LIKE ? ORDER BY date;", -1, &st.stmt, nullptr);
    bind_text(st.stmt, 1, prefix);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        CalendarDay d;
        d.date = text_column(st.stmt, 0);
        d.day = sqlite3_column_int(st.stmt, 1);
        d.weekend = sqlite3_column_int(st.stmt, 2) != 0;
        d.holiday = sqlite3_column_int(st.stmt, 3) != 0;
        d.shortened = sqlite3_column_int(st.stmt, 4) != 0;
        d.mark = text_column(st.stmt, 5);
        d.comment = text_column(st.stmt, 6);
        out.push_back(d);
    }
    return out;
}

std::optional<TimesheetCell> Database::timesheet_cell(int employee_id, const std::string& date) const {
    Statement st;
    sqlite3_prepare_v2(db_, "SELECT employee_id, date, code, hours, note FROM timesheet_cells WHERE employee_id = ? AND date = ?;", -1, &st.stmt, nullptr);
    sqlite3_bind_int(st.stmt, 1, employee_id);
    bind_text(st.stmt, 2, date);
    if (sqlite3_step(st.stmt) != SQLITE_ROW) return std::nullopt;
    TimesheetCell c;
    c.employee_id = sqlite3_column_int(st.stmt, 0);
    c.date = text_column(st.stmt, 1);
    c.code = text_column(st.stmt, 2);
    c.hours = sqlite3_column_double(st.stmt, 3);
    c.note = text_column(st.stmt, 4);
    return c;
}

bool Database::save_person(Person& p, std::string& error) {
    Statement st;
    const char* sql = p.id == 0
        ? "INSERT INTO persons (full_name, birth_date, snils, phone, email, note) VALUES (?, ?, ?, ?, ?, ?);"
        : "UPDATE persons SET full_name=?, birth_date=?, snils=?, phone=?, email=?, note=? WHERE id=?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    bind_text(st.stmt, 1, p.full_name);
    bind_text(st.stmt, 2, p.birth_date);
    bind_text(st.stmt, 3, p.snils);
    bind_text(st.stmt, 4, p.phone);
    bind_text(st.stmt, 5, p.email);
    bind_text(st.stmt, 6, p.note);
    if (p.id != 0) sqlite3_bind_int(st.stmt, 7, p.id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    if (p.id == 0) p.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool Database::save_position(Position& p, std::string& error) {
    Statement st;
    const char* sql = p.id == 0
        ? "INSERT INTO positions (title, category, note) VALUES (?, ?, ?);"
        : "UPDATE positions SET title=?, category=?, note=? WHERE id=?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    bind_text(st.stmt, 1, p.title);
    bind_text(st.stmt, 2, p.category);
    bind_text(st.stmt, 3, p.note);
    if (p.id != 0) sqlite3_bind_int(st.stmt, 4, p.id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    if (p.id == 0) p.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool Database::save_employee(Employee& e, std::string& error) {
    Statement st;
    const char* sql = e.id == 0
        ? "INSERT INTO employees (person_id, position_id, norm_id, full_name, position, department, personnel_no, hire_date, dismissal_date, note, active) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
        : "UPDATE employees SET person_id=?, position_id=?, norm_id=?, full_name=?, position=?, department=?, personnel_no=?, hire_date=?, dismissal_date=?, note=?, active=? WHERE id=?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    if (e.person_id > 0) sqlite3_bind_int(st.stmt, 1, e.person_id); else sqlite3_bind_null(st.stmt, 1);
    int index = 2;
    if (e.position_id > 0) sqlite3_bind_int(st.stmt, index, e.position_id); else sqlite3_bind_null(st.stmt, index);
    ++index;
    if (e.norm_id > 0) sqlite3_bind_int(st.stmt, index, e.norm_id); else sqlite3_bind_null(st.stmt, index);
    ++index;
    bind_text(st.stmt, index++, e.full_name);
    bind_text(st.stmt, index++, e.position);
    bind_text(st.stmt, index++, e.department);
    bind_text(st.stmt, index++, e.personnel_no);
    bind_text(st.stmt, index++, e.hire_date);
    bind_text(st.stmt, index++, e.dismissal_date);
    bind_text(st.stmt, index++, e.note);
    sqlite3_bind_int(st.stmt, index++, e.active ? 1 : 0);
    if (e.id != 0) sqlite3_bind_int(st.stmt, index, e.id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    if (e.id == 0) e.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool Database::save_activity(ActivityKind& a, std::string& error) {
    Statement st;
    const char* sql = a.id == 0
        ? "INSERT INTO activity_kinds (title, code, description, affects_norm, default_hours) VALUES (?, ?, ?, ?, ?);"
        : "UPDATE activity_kinds SET title=?, code=?, description=?, affects_norm=?, default_hours=? WHERE id=?;";
    sqlite3_prepare_v2(db_, sql, -1, &st.stmt, nullptr);
    bind_text(st.stmt, 1, a.title);
    bind_text(st.stmt, 2, a.code);
    bind_text(st.stmt, 3, a.description);
    sqlite3_bind_int(st.stmt, 4, a.affects_norm ? 1 : 0);
    sqlite3_bind_double(st.stmt, 5, a.default_hours);
    if (a.id != 0) sqlite3_bind_int(st.stmt, 6, a.id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    if (a.id == 0) a.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool Database::save_activity_period(ActivityPeriod& p, std::string& error) {
    if (p.date_to < p.date_from) std::swap(p.date_from, p.date_to);
    Statement st;
    const char* sql = p.id == 0
        ? "INSERT INTO activity_periods (employee_id, activity_id, date_from, date_to, hours, note, created_at) VALUES (?, ?, ?, ?, ?, ?, datetime('now', 'localtime')) RETURNING id, created_at;"
        : "UPDATE activity_periods SET employee_id=?, activity_id=?, date_from=?, date_to=?, hours=?, note=? WHERE id=? RETURNING id, created_at;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, p.employee_id);
    sqlite3_bind_int(st.stmt, 2, p.activity_id);
    bind_text(st.stmt, 3, p.date_from);
    bind_text(st.stmt, 4, p.date_to);
    sqlite3_bind_double(st.stmt, 5, p.hours);
    bind_text(st.stmt, 6, p.note);
    if (p.id != 0) sqlite3_bind_int(st.stmt, 7, p.id);
    if (sqlite3_step(st.stmt) != SQLITE_ROW) { error = sqlite3_errmsg(db_); return false; }
    p.id = sqlite3_column_int(st.stmt, 0);
    p.created_at = text_column(st.stmt, 1);
    return true;
}

bool Database::save_timesheet_document(TimesheetDocument& d, std::string& error) {
    Statement st;
    if (d.title.empty()) d.title = "Табель";
    const char* sql = d.id == 0
        ? "INSERT INTO timesheet_documents (year, month, title, accepted, needs_refill, note) VALUES (?, ?, ?, ?, ?, ?) RETURNING id, created_at;"
        : "UPDATE timesheet_documents SET year=?, month=?, title=?, accepted=?, needs_refill=?, note=? WHERE id=? RETURNING id, created_at;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, d.year);
    sqlite3_bind_int(st.stmt, 2, d.month);
    bind_text(st.stmt, 3, d.title);
    sqlite3_bind_int(st.stmt, 4, d.accepted ? 1 : 0);
    sqlite3_bind_int(st.stmt, 5, d.needs_refill ? 1 : 0);
    bind_text(st.stmt, 6, d.note);
    if (d.id != 0) sqlite3_bind_int(st.stmt, 7, d.id);
    if (sqlite3_step(st.stmt) != SQLITE_ROW) { error = sqlite3_errmsg(db_); return false; }
    d.id = sqlite3_column_int(st.stmt, 0);
    d.created_at = text_column(st.stmt, 1);
    return true;
}

bool Database::mark_timesheets_need_refill(const ActivityPeriod& period, std::string& error) {
    Statement st;
    if (sqlite3_prepare_v2(db_, R"sql(
UPDATE timesheet_documents
SET needs_refill = 1
WHERE accepted = 0
  AND ? <= date(printf('%04d-%02d-01', year, month), '+1 month', '-1 day')
  AND ? >= printf('%04d-%02d-01', year, month);
)sql", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    bind_text(st.stmt, 1, period.date_from);
    bind_text(st.stmt, 2, period.date_to);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::create_or_refill_timesheet_document(TimesheetDocument& d, std::string& error) {
    if (d.accepted) {
        error = "Табель принят и защищен от изменений";
        return false;
    }
    if (!save_timesheet_document(d, error)) return false;
    if (!refill_timesheet(d.year, d.month, error)) return false;
    d.needs_refill = false;
    return save_timesheet_document(d, error);
}

bool Database::save_norm(WorkNorm& n, std::string& error) {
    Statement st;
    const char* sql = n.id == 0
        ? "INSERT INTO work_norms (title, rate, hours_per_day, hours_per_week, days_per_week, short_friday) VALUES (?, ?, ?, ?, ?, ?);"
        : "UPDATE work_norms SET title=?, rate=?, hours_per_day=?, hours_per_week=?, days_per_week=?, short_friday=? WHERE id=?;";
    sqlite3_prepare_v2(db_, sql, -1, &st.stmt, nullptr);
    bind_text(st.stmt, 1, n.title);
    sqlite3_bind_double(st.stmt, 2, n.rate);
    int index = 3;
    sqlite3_bind_double(st.stmt, index++, n.hours_per_day);
    sqlite3_bind_double(st.stmt, index++, n.hours_per_week);
    sqlite3_bind_int(st.stmt, index++, n.days_per_week);
    sqlite3_bind_int(st.stmt, index++, n.short_friday ? 1 : 0);
    if (n.id != 0) sqlite3_bind_int(st.stmt, index, n.id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    if (n.id == 0) n.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool Database::save_institution(const Institution& i, std::string& error) {
    Statement st;
    sqlite3_prepare_v2(db_, "UPDATE institution SET title=?, okpo=?, inn=?, address=?, structural_unit=?, responsible=?, executor_position=?, executor_position_code=? WHERE id=1;", -1, &st.stmt, nullptr);
    bind_text(st.stmt, 1, i.title);
    bind_text(st.stmt, 2, i.okpo);
    bind_text(st.stmt, 3, i.inn);
    bind_text(st.stmt, 4, i.address);
    bind_text(st.stmt, 5, i.structural_unit);
    bind_text(st.stmt, 6, i.responsible);
    bind_text(st.stmt, 7, i.executor_position);
    bind_text(st.stmt, 8, i.executor_position_code);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::save_calendar_day(const CalendarDay& d, std::string& error) {
    Statement st;
    sqlite3_prepare_v2(db_, "INSERT INTO calendar_days (date, day, weekend, holiday, shortened, mark, comment) VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(date) DO UPDATE SET day=excluded.day, weekend=excluded.weekend, holiday=excluded.holiday, shortened=excluded.shortened, mark=excluded.mark, comment=excluded.comment;", -1, &st.stmt, nullptr);
    bind_text(st.stmt, 1, d.date);
    sqlite3_bind_int(st.stmt, 2, d.day);
    sqlite3_bind_int(st.stmt, 3, d.weekend ? 1 : 0);
    sqlite3_bind_int(st.stmt, 4, d.holiday ? 1 : 0);
    sqlite3_bind_int(st.stmt, 5, d.shortened ? 1 : 0);
    bind_text(st.stmt, 6, d.mark);
    bind_text(st.stmt, 7, d.comment);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::save_timesheet_cell(const TimesheetCell& c, std::string& error) {
    Statement st;
    sqlite3_prepare_v2(db_, "INSERT INTO timesheet_cells (employee_id, date, code, hours, note) VALUES (?, ?, ?, ?, ?) ON CONFLICT(employee_id, date) DO UPDATE SET code=excluded.code, hours=excluded.hours, note=excluded.note;", -1, &st.stmt, nullptr);
    sqlite3_bind_int(st.stmt, 1, c.employee_id);
    bind_text(st.stmt, 2, c.date);
    bind_text(st.stmt, 3, c.code);
    sqlite3_bind_double(st.stmt, 4, c.hours);
    bind_text(st.stmt, 5, c.note);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::delete_person(int id, std::string& error) {
    if (exists_row(db_, R"sql(
SELECT 1
FROM employees e
JOIN timesheet_cells tc ON tc.employee_id = e.id
JOIN timesheet_documents td
  ON td.accepted = 1
 AND td.year = CAST(substr(tc.date, 1, 4) AS INTEGER)
 AND td.month = CAST(substr(tc.date, 6, 2) AS INTEGER)
WHERE e.person_id = ?
LIMIT 1;
)sql", id, error)) {
        error = "Нельзя удалить физлицо: оно связано с сотрудником в принятом табеле";
        return false;
    }
    Statement st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM persons WHERE id = ?;", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::delete_position(int id, std::string& error) {
    if (exists_row(db_, R"sql(
SELECT 1
FROM employees e
JOIN timesheet_cells tc ON tc.employee_id = e.id
JOIN timesheet_documents td
  ON td.accepted = 1
 AND td.year = CAST(substr(tc.date, 1, 4) AS INTEGER)
 AND td.month = CAST(substr(tc.date, 6, 2) AS INTEGER)
WHERE e.position_id = ?
LIMIT 1;
)sql", id, error)) {
        error = "Нельзя удалить должность: она используется в принятом табеле";
        return false;
    }
    Statement st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM positions WHERE id = ?;", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::delete_employee(int id, std::string& error) {
    Statement check;
    if (sqlite3_prepare_v2(db_, R"sql(
SELECT 1
FROM timesheet_cells tc
JOIN timesheet_documents td
  ON td.accepted = 1
 AND td.year = CAST(substr(tc.date, 1, 4) AS INTEGER)
 AND td.month = CAST(substr(tc.date, 6, 2) AS INTEGER)
WHERE tc.employee_id = ?
LIMIT 1;
)sql", -1, &check.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(check.stmt, 1, id);
    if (sqlite3_step(check.stmt) == SQLITE_ROW) {
        error = "Нельзя удалить сотрудника: он есть в принятом табеле";
        return false;
    }

    Statement st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM employees WHERE id = ?;", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::delete_activity(int id, std::string& error) {
    if (exists_row(db_, R"sql(
SELECT 1
FROM activity_kinds ak
JOIN timesheet_cells tc ON tc.code = ak.code
JOIN timesheet_documents td
  ON td.accepted = 1
 AND td.year = CAST(substr(tc.date, 1, 4) AS INTEGER)
 AND td.month = CAST(substr(tc.date, 6, 2) AS INTEGER)
WHERE ak.id = ?
LIMIT 1;
)sql", id, error)) {
        error = "Нельзя удалить вид активности: он используется в принятом табеле";
        return false;
    }
    Statement st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM activity_kinds WHERE id = ?;", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::delete_activity_period(int id, std::string& error) {
    if (exists_row(db_, R"sql(
SELECT 1
FROM activity_periods ap
JOIN timesheet_documents td
  ON td.accepted = 1
 AND ap.date_from <= date(printf('%04d-%02d-01', td.year, td.month), '+1 month', '-1 day')
 AND ap.date_to >= printf('%04d-%02d-01', td.year, td.month)
WHERE ap.id = ?
LIMIT 1;
)sql", id, error)) {
        error = "Нельзя удалить период активности: он относится к принятому табелю";
        return false;
    }
    Statement st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM activity_periods WHERE id = ?;", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::delete_timesheet_document(const TimesheetDocument& document, std::string& error) {
    Statement st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM timesheet_documents WHERE id = ?;", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, document.id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::delete_norm(int id, std::string& error) {
    if (exists_row(db_, R"sql(
SELECT 1
FROM employees e
JOIN timesheet_cells tc ON tc.employee_id = e.id
JOIN timesheet_documents td
  ON td.accepted = 1
 AND td.year = CAST(substr(tc.date, 1, 4) AS INTEGER)
 AND td.month = CAST(substr(tc.date, 6, 2) AS INTEGER)
WHERE e.norm_id = ?
LIMIT 1;
)sql", id, error)) {
        error = "Нельзя удалить норму времени: она используется в принятом табеле";
        return false;
    }
    Statement st;
    if (sqlite3_prepare_v2(db_, "DELETE FROM work_norms WHERE id = ?;", -1, &st.stmt, nullptr) != SQLITE_OK) { error = sqlite3_errmsg(db_); return false; }
    sqlite3_bind_int(st.stmt, 1, id);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); return false; }
    return true;
}

bool Database::refill_timesheet(int year, int month, std::string& error) {
    const std::string from = iso_date(year, month, 1);
    const std::string to = iso_date(year, month, days_in_month(year, month));
    if (!exec("BEGIN IMMEDIATE;", error)) return false;

    Statement del;
    sqlite3_prepare_v2(db_, "DELETE FROM timesheet_cells WHERE date >= ? AND date <= ?;", -1, &del.stmt, nullptr);
    bind_text(del.stmt, 1, from);
    bind_text(del.stmt, 2, to);
    if (sqlite3_step(del.stmt) != SQLITE_DONE) { error = sqlite3_errmsg(db_); exec("ROLLBACK;", error); return false; }

    auto days = calendar_days(year, month);
    if (days.empty()) {
        days = build_local_calendar(year, month);
        for (const auto& day : days) {
            if (!save_calendar_day(day, error)) { exec("ROLLBACK;", error); return false; }
        }
    }
    const auto emps = employees();
    const auto periods = activity_periods(year, month);
    std::map<int, WorkNorm> norms_by_id;
    for (const auto& norm : norms()) {
        norms_by_id[norm.id] = norm;
    }

    for (const auto& e : emps) {
        if (!e.active) continue;
        for (const auto& d : days) {
            if (!e.hire_date.empty() && d.date < e.hire_date) continue;
            if (!e.dismissal_date.empty() && d.date > e.dismissal_date) continue;

            double day_hours = 8.0;
            if (const auto it = norms_by_id.find(e.norm_id); it != norms_by_id.end()) {
                day_hours = work_hours_for_day(it->second, d);
            } else if (d.shortened) {
                day_hours = 7.0;
            }
            TimesheetCell cell;
            cell.employee_id = e.id;
            cell.date = d.date;
            cell.code = d.weekend ? "В" : "Я";
            cell.hours = d.weekend ? 0.0 : day_hours;
            cell.note = "Автозаполнение";

            for (const auto& p : periods) {
                if (p.employee_id != e.id) continue;
                if (p.date_from <= d.date && d.date <= p.date_to) {
                    cell.code = p.activity_code;
                    cell.hours = p.hours;
                    cell.note = p.note.empty() ? p.activity_title : p.note;
                }
            }
            if (!save_timesheet_cell(cell, error)) { exec("ROLLBACK;", error); return false; }
        }
    }
    return exec("COMMIT;", error);
}
