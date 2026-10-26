cmake_minimum_required(VERSION 3.28)
project(SpriteForge2D LANGUAGES CXX)

# Editor is a stable tool. User scripts are compiled only in per-project GameRuntime builds.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(FetchContent)
FetchContent_Declare(sfml
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.2 GIT_SHALLOW ON EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(sfml)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.6 GIT_SHALLOW ON EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(imgui)
FetchContent_GetProperties(imgui SOURCE_DIR IMGUI_DIR)
set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)
FetchContent_Declare(imgui_sfml
    GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
    GIT_TAG v3.0 GIT_SHALLOW ON EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(imgui_sfml)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

add_library(EngineCore STATIC src/EngineCore.cpp src/Behaviors.cpp)
target_compile_features(EngineCore PUBLIC cxx_std_17)
target_include_directories(EngineCore PUBLIC src)
target_link_libraries(EngineCore PUBLIC SFML::Graphics nlohmann_json::nlohmann_json)

add_executable(Editor WIN32 src/Editor.cpp src/ProjectWorkspace.cpp src/ProjectWorkspace.h src/BuildRunner.cpp src/BuildRunner.h)
target_link_libraries(Editor PRIVATE EngineCore ImGui-SFML::ImGui-SFML comdlg32 shell32 ole32)

add_custom_command(TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DDEST=$<TARGET_FILE_DIR:Editor>/Project
            -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/sample_project
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install_sample.cmake
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Editor>/sdk/src
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Editor>/sdk/cmake
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Runtime.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/EngineCore.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/EngineCore.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Behaviors.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Behaviors.h
            $<TARGET_FILE_DIR:Editor>/sdk/src
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpriteForgeGame.cmake
            $<TARGET_FILE_DIR:Editor>/sdk/cmake
    VERBATIM)

if(WIN32 AND MINGW)
    target_link_options(Editor PRIVATE -static-libgcc -static-libstdc++)
endif()
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Editor)
