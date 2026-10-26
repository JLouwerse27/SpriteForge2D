#include "EngineCore.h"
#include "Behaviors.h"
#include "GameBehaviors.h"
#include <windows.h>
#include <algorithm>
#include <string>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        const auto folder = forge::executableDirectory();
        forge::Scene scene;
        std::string error;
        if (!forge::loadScene(scene, folder / "scene.json", error)) {
            MessageBoxA(nullptr, error.c_str(), "Unable to load game", MB_OK | MB_ICONERROR);
            return 1;
        }
        forge::BehaviorRegistry registry;
        registerGameBehaviors(registry); // C++ game logic linked into the executable.
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
