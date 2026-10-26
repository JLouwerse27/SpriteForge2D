cmake_minimum_required(VERSION 3.28)
project(SpriteForge2D LANGUAGES CXX)

# Static SFML and C++ runtime: exported games require no bundled SFML DLLs.
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
    GIT_TAG 3.0.2
    GIT_SHALLOW ON EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(sfml)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.6
    GIT_SHALLOW ON EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(imgui)
FetchContent_GetProperties(imgui SOURCE_DIR IMGUI_DIR)

set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)
FetchContent_Declare(imgui_sfml
    GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
    GIT_TAG v3.0
    GIT_SHALLOW ON EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(imgui_sfml)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

add_library(EngineCore STATIC src/EngineCore.cpp src/EngineCore.h
                              src/Behaviors.cpp src/Behaviors.h)
target_compile_features(EngineCore PUBLIC cxx_std_17)
target_include_directories(EngineCore PUBLIC src)
target_link_libraries(EngineCore PUBLIC SFML::Graphics nlohmann_json::nlohmann_json)

# Option A: compile user-authored C++ behavior classes directly into BOTH
# the editor (Play mode) and the exported GameRuntime executable.
# When you add a new game/*.cpp file, CMake detects it automatically on rebuild.
file(GLOB GAME_CPP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/game/*.cpp")
add_library(GameCode STATIC ${GAME_CPP} game/GameBehaviors.h)
target_include_directories(GameCode PUBLIC game)
target_link_libraries(GameCode PUBLIC EngineCore)

add_executable(GameRuntime WIN32 src/Runtime.cpp)
target_link_libraries(GameRuntime PRIVATE GameCode)

add_executable(Editor WIN32 src/Editor.cpp)
target_link_libraries(Editor PRIVATE GameCode ImGui-SFML::ImGui-SFML comdlg32 shell32 ole32)
add_dependencies(Editor GameRuntime)

# Copy sample project beside the editor (without overwriting user's work on future builds).
add_custom_command(TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DDEST=$<TARGET_FILE_DIR:Editor>/Project
            -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/sample_project
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install_sample.cmake
    VERBATIM)

if(WIN32 AND MINGW)
    foreach(target Editor GameRuntime)
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
    endforeach()
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Editor)
