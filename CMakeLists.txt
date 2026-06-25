cmake_minimum_required(VERSION 3.24)

project(tabel0504421 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(GNUInstallDirs)
set(FETCHCONTENT_QUIET FALSE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(LIBCURL REQUIRED libcurl)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(LIBZIP REQUIRED libzip)
find_package(OpenGL REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
)

FetchContent_Declare(
    imguifiledialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG v0.6.7
)

FetchContent_MakeAvailable(imgui)
FetchContent_GetProperties(imguifiledialog)
if(NOT imguifiledialog_POPULATED)
    FetchContent_Populate(imguifiledialog)
endif()

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imguifiledialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)

target_include_directories(imgui_backend PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
    ${imgui_SOURCE_DIR}/misc/freetype
    ${imguifiledialog_SOURCE_DIR}
    ${GLFW3_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
)

target_compile_definitions(imgui_backend PUBLIC IMGUI_ENABLE_DOCKING IMGUI_USE_WCHAR32 IMGUI_ENABLE_FREETYPE)
target_link_directories(imgui_backend PUBLIC ${FREETYPE_LIBRARY_DIRS})
target_link_libraries(imgui_backend PUBLIC ${GLFW3_LIBRARIES} ${FREETYPE_LIBRARIES} OpenGL::GL)

add_executable(tabel0504421
    src/app.cpp
    src/calendar.cpp
    src/database.cpp
    src/fonts.cpp
    src/main.cpp
    src/settings.cpp
    src/widgets.cpp
)

target_include_directories(tabel0504421 PRIVATE src ${SQLITE3_INCLUDE_DIRS} ${LIBCURL_INCLUDE_DIRS} ${LIBZIP_INCLUDE_DIRS})
target_link_directories(tabel0504421 PRIVATE ${GLFW3_LIBRARY_DIRS} ${SQLITE3_LIBRARY_DIRS} ${LIBCURL_LIBRARY_DIRS} ${LIBZIP_LIBRARY_DIRS})
target_link_libraries(tabel0504421 PRIVATE imgui_backend ${SQLITE3_LIBRARIES} ${LIBCURL_LIBRARIES} ${LIBZIP_LIBRARIES})
target_compile_options(tabel0504421 PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tabel0504421 PRIVATE TABEL0504421_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/tabel0504421")

add_custom_command(TARGET tabel0504421 POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -Dsrc=${CMAKE_SOURCE_DIR}/assets
        -Ddst=$<TARGET_FILE_DIR:tabel0504421>/assets
        -P ${CMAKE_SOURCE_DIR}/cmake/copy_runtime_assets.cmake
    COMMENT "Copying runtime assets"
)

install(TARGETS tabel0504421
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY assets/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/tabel0504421/assets
)

install(FILES packaging/tabel0504421.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES packaging/tabel0504421.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
