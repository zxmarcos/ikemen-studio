# Ikemen GO debugger: a separate program that starts Ikemen GO with the
# ikdbg.lua mod and talks to it over stdin/stdout (see README.md).
# Windows only. Builds with MSVC or with MSYS2's gcc:
#   cmake -S tools/debugger -B tools/debugger/build && cmake --build tools/debugger/build --config Release
cmake_minimum_required(VERSION 3.20)
project(ikemen_debugger LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The Ikemen GO debugger only builds on Windows")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Dear ImGui 1.92.9b (docking branch), MIT licence: third_party/imgui/LICENSE.txt
set(_imgui "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
add_library(imgui STATIC
  "${_imgui}/imgui.cpp" "${_imgui}/imgui_draw.cpp" "${_imgui}/imgui_tables.cpp"
  "${_imgui}/imgui_widgets.cpp" "${_imgui}/imgui_demo.cpp"
  "${_imgui}/backends/imgui_impl_opengl2.cpp" "${_imgui}/misc/imgui_stdlib.cpp")
target_include_directories(imgui PUBLIC "${_imgui}" "${_imgui}/backends" "${_imgui}/misc")
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC opengl32)

# lodepng (commit e584a849), zlib licence: third_party/lodepng/LICENSE
add_library(lodepng STATIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lodepng/lodepng.cpp")
target_include_directories(lodepng PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lodepng")

add_executable(ikemen-debugger WIN32
  src/main.cpp
  src/app.cpp
  src/app_game.cpp
  src/app_anim.cpp
  src/app_sprites.cpp
  src/app_sounds.cpp
  src/app_code.cpp
  src/app_debug.cpp
  src/app_cmd.cpp
  src/app_stage.cpp
  src/app_screen.cpp
  src/app_roster.cpp
  src/app_fonts.cpp
  src/app_lab.cpp
  src/app_layout.cpp
  src/app_create.cpp
  src/app_model.cpp
  src/app_check.cpp
  src/cns.cpp
  src/make.cpp
  src/bridge.cpp
  src/frame.cpp
  src/editor.cpp
  src/air.cpp
  src/sff.cpp
  src/snd.cpp
  src/cmd.cpp
  src/ini.cpp
  src/roster.cpp
  src/fnt.cpp)
target_compile_definitions(ikemen-debugger PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
if(MSVC)
  target_compile_options(ikemen-debugger PRIVATE /W3 /utf-8 /EHsc)
else()
  target_compile_options(ikemen-debugger PRIVATE -Wall -Wno-unused-function)
  # wWinMain, and no MSYS2 DLLs needed next to the exe
  target_link_options(ikemen-debugger PRIVATE -municode -static)
endif()
target_link_libraries(ikemen-debugger PRIVATE imgui lodepng opengl32 user32 gdi32 comdlg32 winmm)
# No per-configuration folder: the exe looks for ikdbg.lua next to itself.
set_target_properties(ikemen-debugger PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}>")

# The mod goes next to the exe; the debugger copies it into the game's external/mods.
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ikdbg.lua"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/mod/ikdbg.lua" "${CMAKE_CURRENT_BINARY_DIR}/ikdbg.lua"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mod/ikdbg.lua"
  VERBATIM)
target_sources(ikemen-debugger PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ikdbg.lua")

# Tests of the parts that do not need a window: scripts/build.ps1 test
add_executable(air-test tests/air_test.cpp src/air.cpp)
target_include_directories(air-test PRIVATE src)
add_executable(sff-test tests/sff_test.cpp src/sff.cpp)
target_include_directories(sff-test PRIVATE src)
target_link_libraries(sff-test PRIVATE lodepng)
add_executable(snd-test tests/snd_test.cpp src/snd.cpp)
target_include_directories(snd-test PRIVATE src)
add_executable(cmd-test tests/cmd_test.cpp src/cmd.cpp)
target_include_directories(cmd-test PRIVATE src)
add_executable(ini-test tests/ini_test.cpp src/ini.cpp)
target_include_directories(ini-test PRIVATE src)
add_executable(roster-test tests/roster_test.cpp src/roster.cpp)
target_include_directories(roster-test PRIVATE src)
add_executable(fnt-test tests/fnt_test.cpp src/fnt.cpp)
target_include_directories(fnt-test PRIVATE src)
add_executable(cns-test tests/cns_test.cpp src/cns.cpp)
target_include_directories(cns-test PRIVATE src)
add_executable(make-test tests/make_test.cpp src/make.cpp src/cns.cpp src/air.cpp src/ini.cpp)
target_include_directories(make-test PRIVATE src)
