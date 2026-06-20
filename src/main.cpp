#include "app.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {
void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void merge_icon_font_if_exists(ImGuiIO& io, const char* path) {
    if (!std::filesystem::exists(path)) return;
    static const ImWchar icon_ranges[] = {
        0xf000, 0xf2ff,
        0
    };
    ImFontConfig icon_config;
    icon_config.MergeMode = true;
    icon_config.PixelSnapH = true;
    icon_config.GlyphMinAdvanceX = 18.0f;
    io.Fonts->AddFontFromFileTTF(path, 18.0f, &icon_config, icon_ranges);
}

std::filesystem::path font_path(const char* argv0, const char* filename) {
    const std::filesystem::path relative = std::filesystem::path("assets") / "fonts" / filename;
    if (std::filesystem::exists(relative)) return relative;
    if (argv0 && *argv0) {
        const std::filesystem::path executable_relative = std::filesystem::absolute(argv0).parent_path() / ".." / "assets" / "fonts" / filename;
        if (std::filesystem::exists(executable_relative)) return executable_relative;
    }
    return relative;
}

void load_project_fonts(ImGuiIO& io, const char* argv0) {
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;

    const std::string text_font = font_path(argv0, "NotoSans-Regular.ttf").string();
    if (std::filesystem::exists(text_font)) {
        io.Fonts->AddFontFromFileTTF(text_font.c_str(), 18.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
    }

    const std::string icons = font_path(argv0, "fontawesome-webfont.ttf").string();
    merge_icon_font_if_exists(io, icons.c_str());
}
}

int main(int argc, char** argv) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1440, 900, "Табель 0504421", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    load_project_fonts(io, argc > 0 ? argv[0] : nullptr);
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    App app;
    while (!glfwWindowShouldClose(window) && !app.should_close()) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.render();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
