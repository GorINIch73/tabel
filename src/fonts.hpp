#pragma once

#include <imgui.h>

#include <span>

std::span<const float> font_size_options();
void load_project_fonts(ImGuiIO& io, float preferred_font_size);
void use_project_font_size(ImGuiIO& io, float font_size);
