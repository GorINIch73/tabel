#include "calendar.hpp"

#include <curl/curl.h>

#include <ctime>
#include <regex>
#include <set>
#include <sstream>

namespace {
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* text = static_cast<std::string*>(userdata);
    text->append(ptr, size * nmemb);
    return size * nmemb;
}

bool is_weekend(int year, int month, int day) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    std::mktime(&t);
    return t.tm_wday == 0 || t.tm_wday == 6;
}

std::set<int> parse_days(const std::string& text) {
    std::set<int> days;
    std::regex number("(\\d+)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), number); it != std::sregex_iterator(); ++it) {
        days.insert(std::stoi((*it)[1].str()));
    }
    return days;
}
}

int days_in_month(int year, int month) {
    static constexpr int normal[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return normal[month - 1];
}

std::string iso_date(int year, int month, int day) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
    return buffer;
}

std::vector<CalendarDay> build_local_calendar(int year, int month) {
    std::vector<CalendarDay> out;
    for (int day = 1; day <= days_in_month(year, month); ++day) {
        CalendarDay d;
        d.date = iso_date(year, month, day);
        d.day = day;
        d.weekend = is_weekend(year, month, day);
        d.holiday = d.weekend;
        d.shortened = false;
        d.mark = d.weekend ? "В" : "Я";
        out.push_back(d);
    }
    return out;
}

bool download_ru_calendar(int year, std::vector<CalendarDay>& days, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "Не удалось инициализировать libcurl";
        return false;
    }

    std::string body;
    const std::string url = "https://xmlcalendar.ru/data/ru/" + std::to_string(year) + "/calendar.json";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tabel0504421/0.1");

    const CURLcode code = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK || http_code >= 400) {
        error = "Не удалось загрузить календарь: " + std::string(curl_easy_strerror(code)) + ", HTTP " + std::to_string(http_code);
        return false;
    }

    days.clear();
    for (int month = 1; month <= 12; ++month) {
        auto month_days = build_local_calendar(year, month);
        const std::string pattern = "\\{[^\\{\\}]*\"month\"\\s*:\\s*" + std::to_string(month) + "[^\\{\\}]*\\}";
        std::regex month_re(pattern);
        std::smatch m;
        if (std::regex_search(body, m, month_re)) {
            const std::string block = m.str();
            std::smatch h;
            std::set<int> holidays;
            std::set<int> preholidays;
            if (std::regex_search(block, h, std::regex("\"holidays\"\\s*:\\s*\"([^\"]*)\""))) {
                holidays = parse_days(h[1].str());
            }
            if (std::regex_search(block, h, std::regex("\"preholidays\"\\s*:\\s*\"([^\"]*)\""))) {
                preholidays = parse_days(h[1].str());
            }
            for (auto& d : month_days) {
                if (holidays.contains(d.day)) {
                    d.weekend = true;
                    d.holiday = true;
                    d.mark = "В";
                    d.comment = "Загружено из производственного календаря";
                }
                if (preholidays.contains(d.day)) {
                    d.shortened = true;
                    d.comment = "Предпраздничный сокращенный день";
                }
            }
        }
        days.insert(days.end(), month_days.begin(), month_days.end());
    }
    return true;
}

