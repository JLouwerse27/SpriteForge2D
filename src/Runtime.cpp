#include "EngineCore.h"
#include "Behaviors.h"
#include "SteamIntegration.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <string>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        // --scene <path> is used by the editor for a game preview. Exported games
        // run without arguments and load scene.json beside their executable.
        forge::fs::path sceneFile = forge::executableDirectory() / "scene.json";
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            if (argc >= 3 && std::wstring(argv[1]) == L"--scene") sceneFile = argv[2];
            LocalFree(argv);
        }
        const auto folder = sceneFile.parent_path();
        forge::Scene scene;
        std::string error;
        if (!forge::loadScene(scene, sceneFile, error)) {
            MessageBoxA(nullptr, error.c_str(), "Unable to load game", MB_OK | MB_ICONERROR);
            return 1;
        }
        // SteamAPI_Init must precede SFML's OpenGL context creation for overlay hook.
        struct SteamLifetime {
            SteamLifetime() { forge::steam::initialize(); }
            ~SteamLifetime() { forge::steam::shutdown(); }
        } steamLifetime;
        // Invited players may be launched by Steam with +connect_lobby <id>.
        argc = 0;
        argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i + 1 < argc; ++i) {
                if (std::wstring(argv[i]) == L"+connect_lobby") {
                    try { forge::steam::joinLobby(std::stoull(argv[i + 1])); }
                    catch (...) {} // Ignore malformed/inapplicable invite IDs.
                }
            }
            LocalFree(argv);
        }
        forge::BehaviorRegistry registry;
        extern void registerGameBehaviors(forge::BehaviorRegistry&);
        registerGameBehaviors(registry); // This PROJECT'S C++ code is statically linked here.
        forge::BehaviorSystem behaviors(registry);
        behaviors.start(scene);
        sf::RenderWindow window(sf::VideoMode({scene.width, scene.height}), scene.title);
        window.setVerticalSyncEnabled(false);
        window.setFramerateLimit(static_cast<unsigned>(scene.physics.maxRenderFps));
        forge::TextureCache textures;
        sf::Clock clock;
        double accumulator = 0.0;
        const double fixedDt = 1.0 / static_cast<double>(scene.physics.fixedHz);
        while (window.isOpen()) {
            while (auto event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) window.close();
                if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                    if (key->code == sf::Keyboard::Key::Escape) window.close();
                }
            }
            forge::steam::pump();
            // Fixed timestep decouples the simulation rate (240 Hz) from
            // rendering (up to 480 FPS). Limit catch-up to avoid a death spiral.
            const double elapsed = std::min(0.1, static_cast<double>(clock.restart().asSeconds()));
            accumulator += elapsed;
            const forge::InputState input = window.hasFocus()
                ? forge::readKeyboardInput() : forge::InputState{};
            int ticks = 0;
            while (accumulator >= fixedDt && ticks < 24) {
                behaviors.update(scene, input, static_cast<float>(fixedDt));
                forge::stepPhysics(scene, static_cast<float>(fixedDt));
                accumulator -= fixedDt;
                ++ticks;
            }
            if (ticks == 24) accumulator = 0.0;
            window.clear(scene.background);
            forge::drawScene(window, scene, folder, textures);
            window.display();
        }
        behaviors.stop(scene);
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Game error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
