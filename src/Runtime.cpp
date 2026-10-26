#include "EngineCore.h"
#include "Behaviors.h"
#include <shellapi.h>
#include <windows.h>
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
        forge::BehaviorRegistry registry;
        extern void registerGameBehaviors(forge::BehaviorRegistry&);
        registerGameBehaviors(registry); // This PROJECT'S C++ code is statically linked here.
        forge::BehaviorSystem behaviors(registry);
        behaviors.start(scene);
        sf::RenderWindow window(sf::VideoMode({scene.width, scene.height}), scene.title);
        window.setVerticalSyncEnabled(true);
        forge::TextureCache textures;
        sf::Clock clock;
        while (window.isOpen()) {
            while (auto event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) window.close();
                if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                    if (key->code == sf::Keyboard::Key::Escape) window.close();
                }
            }
            float dt = std::min(clock.restart().asSeconds(), 0.05f);
            const forge::InputState input = window.hasFocus()
                ? forge::readKeyboardInput() : forge::InputState{};
            behaviors.update(scene, input, dt);
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
