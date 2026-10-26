# Included by a game project's CMakeLists.txt. Builds ONLY its game executable;
# Editor.exe is NOT part of this build graph.
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
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

add_library(SpriteForgeCore STATIC
    "${SPRITEFORGE_SDK}/src/EngineCore.cpp"
    "${SPRITEFORGE_SDK}/src/Behaviors.cpp"
    "${SPRITEFORGE_SDK}/src/Physics.cpp"
    "${SPRITEFORGE_SDK}/src/BitmapFont.cpp")
target_compile_features(SpriteForgeCore PUBLIC cxx_std_17)
target_include_directories(SpriteForgeCore PUBLIC "${SPRITEFORGE_SDK}/src")
target_link_libraries(SpriteForgeCore PUBLIC SFML::Graphics nlohmann_json::nlohmann_json)

file(GLOB GAME_CPP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.cpp")
set(GENERATED "${CMAKE_CURRENT_SOURCE_DIR}/scripts/.generated/GameBehaviors.cpp")
if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR "Missing generated registry. Build this project from the SpriteForge editor first.")
endif()
add_executable(GameRuntime WIN32 "${SPRITEFORGE_SDK}/src/Runtime.cpp"
    "${SPRITEFORGE_SDK}/src/SteamIntegration.cpp" "${GENERATED}" ${GAME_CPP})
target_link_libraries(GameRuntime PRIVATE SpriteForgeCore shell32)
if(WIN32 AND MINGW)
    target_link_options(GameRuntime PRIVATE -static-libgcc -static-libstdc++)
endif()

# Steamworks is an opt-in GAME dependency; Editor.exe never links the SDK.
# The developer must supply their own official Steamworks SDK, which we do NOT redistribute.
if(SPRITEFORGE_STEAM_ENABLED)
    if(NOT SPRITEFORGE_STEAM_SDK OR NOT EXISTS "${SPRITEFORGE_STEAM_SDK}/public/steam/steam_api.h")
        message(FATAL_ERROR "Steam enabled: set SDK folder containing public/steam/steam_api.h in the SpriteForge Steam tab")
    endif()
    set(STEAM_LIB "${SPRITEFORGE_STEAM_SDK}/redistributable_bin/win64/steam_api64.lib")
    set(STEAM_DLL "${SPRITEFORGE_STEAM_SDK}/redistributable_bin/win64/steam_api64.dll")
    if(NOT EXISTS "${STEAM_LIB}" OR NOT EXISTS "${STEAM_DLL}")
        message(FATAL_ERROR "Steamworks SDK missing redistributable_bin/win64/steam_api64.lib or .dll")
    endif()
    target_compile_definitions(GameRuntime PRIVATE SPRITEFORGE_WITH_STEAM=1)
    target_include_directories(GameRuntime PRIVATE "${SPRITEFORGE_STEAM_SDK}/public")
    target_link_libraries(GameRuntime PRIVATE "${STEAM_LIB}")
    add_custom_command(TARGET GameRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAM_DLL}" "$<TARGET_FILE_DIR:GameRuntime>/steam_api64.dll"
        VERBATIM)
else()
    target_compile_definitions(GameRuntime PRIVATE SPRITEFORGE_WITH_STEAM=0)
endif()
