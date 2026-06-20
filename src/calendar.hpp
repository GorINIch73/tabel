#pragma once

#include "models.hpp"

#include <string>
#include <vector>

std::vector<CalendarDay> build_local_calendar(int year, int month);
bool download_ru_calendar(int year, std::vector<CalendarDay>& days, std::string& error);
int days_in_month(int year, int month);
std::string iso_date(int year, int month, int day);

