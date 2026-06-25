#include "calendar.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <ctime>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

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

struct CalendarToken {
    int day = 0;
    char marker = '\0';
};

std::vector<CalendarToken> parse_calendar_tokens(const std::string& text) {
    std::vector<CalendarToken> tokens;
    std::regex token("(\\d+)([+*]?)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), token); it != std::sregex_iterator(); ++it) {
        CalendarToken parsed;
        parsed.day = std::stoi((*it)[1].str());
        const std::string marker = (*it)[2].str();
        parsed.marker = marker.empty() ? '\0' : marker.front();
        tokens.push_back(parsed);
    }
    return tokens;
}

std::string transition_comment(const std::unordered_map<std::string, std::string>& transitions, const std::string& month_day) {
    if (const auto it = transitions.find(month_day); it != transitions.end()) {
        return "Перенос выходного дня: " + month_day + " -> " + it->second;
    }
    for (const auto& [from, to] : transitions) {
        if (to == month_day) return "Рабочий день по переносу: " + from + " -> " + to;
    }
    return "";
}

std::string month_day_key(int month, int day) {
    char buffer[8]{};
    std::snprintf(buffer, sizeof(buffer), "%02d.%02d", month, day);
    return buffer;
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
        d.holiday = false;
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
    std::unordered_map<std::string, std::string> transitions;
    const std::regex transition_re(R"json(\{\s*"from"\s*:\s*"([0-9]{2}\.[0-9]{2})"\s*,\s*"to"\s*:\s*"([0-9]{2}\.[0-9]{2})"\s*\})json");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), transition_re); it != std::sregex_iterator(); ++it) {
        transitions[(*it)[1].str()] = (*it)[2].str();
    }

    for (int month = 1; month <= 12; ++month) {
        auto month_days = build_local_calendar(year, month);
        const std::string pattern = "\\{[^\\{\\}]*\"month\"\\s*:\\s*" + std::to_string(month) + "[^\\{\\}]*\\}";
        std::regex month_re(pattern);
        std::smatch m;
        if (std::regex_search(body, m, month_re)) {
            const std::string block = m.str();
            std::smatch h;
            if (std::regex_search(block, h, std::regex("\"days\"\\s*:\\s*\"([^\"]*)\""))) {
                for (const auto& token : parse_calendar_tokens(h[1].str())) {
                    auto day_it = std::find_if(month_days.begin(), month_days.end(), [&token](const CalendarDay& day) {
                        return day.day == token.day;
                    });
                    if (day_it == month_days.end()) continue;

                    const std::string month_day = month_day_key(month, token.day);
                    const bool natural_weekend = is_weekend(year, month, token.day);
                    const std::string transfer = transition_comment(transitions, month_day);

                    if (token.marker == '*') {
                        day_it->weekend = false;
                        day_it->holiday = false;
                        day_it->shortened = true;
                        day_it->mark = "Я";
                        day_it->comment = transfer.empty() ? "Предпраздничный сокращенный день" : transfer + "; сокращенный день";
                    } else if (token.marker == '+') {
                        day_it->weekend = true;
                        day_it->holiday = !natural_weekend;
                        day_it->shortened = false;
                        day_it->mark = "В";
                        day_it->comment = transfer.empty() ? "Выходной по производственному календарю" : transfer;
                    } else {
                        day_it->weekend = true;
                        day_it->holiday = !natural_weekend;
                        day_it->shortened = false;
                        day_it->mark = "В";
                        day_it->comment = natural_weekend ? "" : "Праздничный нерабочий день";
                    }
                }
            } else {
                std::set<int> holidays;
                std::set<int> preholidays;
                if (std::regex_search(block, h, std::regex("\"holidays\"\\s*:\\s*\"([^\"]*)\""))) {
                    holidays = parse_days(h[1].str());
                }
                if (std::regex_search(block, h, std::regex("\"preholidays\"\\s*:\\s*\"([^\"]*)\""))) {
                    preholidays = parse_days(h[1].str());
                }
                for (auto& d : month_days) {
                    const bool natural_weekend = is_weekend(year, month, d.day);
                    if (holidays.contains(d.day)) {
                        d.weekend = true;
                        d.holiday = !natural_weekend;
                        d.mark = "В";
                        d.comment = natural_weekend ? "" : "Праздничный нерабочий день";
                    }
                    if (preholidays.contains(d.day)) {
                        d.weekend = false;
                        d.holiday = false;
                        d.shortened = true;
                        d.mark = "Я";
                        d.comment = "Предпраздничный сокращенный день";
                    }
                }
            }
        }
        days.insert(days.end(), month_days.begin(), month_days.end());
    }
    return true;
}
