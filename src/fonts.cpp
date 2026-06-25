#include "fonts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

namespace {
constexpr std::array<float, 6> kFontSizes = {14.0f, 16.0f, 18.0f, 20.0f, 22.0f, 24.0f};
std::array<ImFont*, kFontSizes.size()> g_fonts{};

std::filesystem::path font_path(const char* filename) {
    const std::filesystem::path relative = std::filesystem::path("assets") / "fonts" / filename;
    if (std::filesystem::exists(relative)) return relative;

#ifdef TABEL0504421_DATA_DIR
    const std::filesystem::path installed = std::filesystem::path(TABEL0504421_DATA_DIR) / "assets" / "fonts" / filename;
    if (std::filesystem::exists(installed)) return installed;
#endif

    std::error_code ec;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable.empty()) {
        const std::filesystem::path executable_dir = executable.parent_path();
        const std::filesystem::path near_executable = executable_dir / "assets" / "fonts" / filename;
        if (std::filesystem::exists(near_executable)) return near_executable;
        const std::filesystem::path project_relative = executable_dir / ".." / "assets" / "fonts" / filename;
        if (std::filesystem::exists(project_relative)) return project_relative;
    }

    return relative;
}

size_t nearest_font_index(float font_size) {
    size_t best = 0;
    float best_delta = std::abs(kFontSizes[0] - font_size);
    for (size_t i = 1; i < kFontSizes.size(); ++i) {
        const float delta = std::abs(kFontSizes[i] - font_size);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

void merge_icon_font_if_exists(ImGuiIO& io, const std::filesystem::path& path, float font_size) {
    if (!std::filesystem::exists(path)) return;
    static const ImWchar icon_ranges[] = {
        0xf000, 0xf2ff,
        0
    };
    ImFontConfig icon_config;
    icon_config.MergeMode = true;
    icon_config.PixelSnapH = true;
    icon_config.GlyphMinAdvanceX = font_size;
    io.Fonts->AddFontFromFileTTF(path.string().c_str(), font_size, &icon_config, icon_ranges);
}
}

std::span<const float> font_size_options() {
    return kFontSizes;
}

void load_project_fonts(ImGuiIO& io, float preferred_font_size) {
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;

    const std::filesystem::path text_font = font_path("NotoSans-Regular.ttf");
    const std::filesystem::path icons = font_path("fontawesome-webfont.ttf");
    if (std::filesystem::exists(text_font)) {
        for (size_t i = 0; i < kFontSizes.size(); ++i) {
            g_fonts[i] = io.Fonts->AddFontFromFileTTF(
                text_font.string().c_str(),
                kFontSizes[i],
                &config,
                io.Fonts->GetGlyphRangesCyrillic()
            );
            merge_icon_font_if_exists(io, icons, kFontSizes[i]);
        }
    }

    if (io.Fonts->Fonts.empty()) {
        g_fonts[nearest_font_index(18.0f)] = io.Fonts->AddFontDefault();
    }
    use_project_font_size(io, preferred_font_size);
}

void use_project_font_size(ImGuiIO& io, float font_size) {
    io.FontGlobalScale = 1.0f;
    const size_t index = nearest_font_index(font_size);
    if (g_fonts[index]) {
        io.FontDefault = g_fonts[index];
    } else if (!io.Fonts->Fonts.empty()) {
        io.FontDefault = io.Fonts->Fonts.front();
    }
}
