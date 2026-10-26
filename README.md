# SpriteForge 2D — C++ Behaviors (Option A)

A small **Windows 2D visual game editor** using **C++17, SFML 3.0.2, Dear ImGui/ImGui-SFML, and nlohmann/json**. This edition adds *compiled C++ behaviors* that you can assign to sprites in the Inspector. The same C++ game code is statically linked into `Editor.exe` for Play mode and into `GameRuntime.exe` for the exported game. No DLL scripting or interpreter is used.

> **Build verification:** The C++ behavior classes, registry, and lifecycle were compiled and exercised with Linux C++17 and minimal SFML interface stubs. The full editor and Windows `.exe` have **not** been compiled or tested in this environment because Windows and the required dependency downloads are unavailable. You reported the previous version builds on Windows; this is a source update that must be rebuilt there.

## Build on Windows

Requirements: Windows 10/11 x64, Visual Studio 2022 with **Desktop development with C++**, CMake 3.28+, Git, and network access on the first build. From a **Developer PowerShell for VS 2022**:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Editor --parallel 4
.\build\bin\Release\Editor.exe
```

Or run `build_windows.bat`.

**When upgrading from the earlier SpriteForge zip:** Put this updated source in a *new folder* or replace the source files in your existing folder. **Keep a backup of `build/bin/Release/Project`** (or your own saved projects) so you do not lose scene changes or sprites. CMake's sample installer does not intentionally overwrite an existing scene file.

## Attach compiled behaviors in the editor

1. Open SpriteForge and select a sprite in **Hierarchy** (or click it in the viewport).
2. In **Inspector → C++ Behavior (Option A)**, choose `MoveWithKeys`, `Spin`, or `Patrol`.
3. Adjust the exposed properties (`Speed`, `DegreesPerSecond`, `Distance`, etc.).
4. Press **Play**. The editor creates a temporary scene copy and runs the compiled behaviors. **Stop** restores the editable scene.
5. Save. The selected behavior's *name and parameter values* go into `scene.json`.
6. Press **Export EXE** to copy the prebuilt game runtime, your scene, and your entire `assets/` directory into a new game folder.

`MoveWithKeys` uses WASD / arrow keys and works in exported games. `Spin` rotates continuously. `Patrol` moves horizontally back and forth. Existing scenes with the old **Controllable** property still work; it is now marked as a legacy fallback.

## Write your own C++ behavior

Edit `game/GameBehaviors.cpp` (or add another `.cpp` file in `game/`). Classes inherit from `forge::Behavior` and implement `onUpdate`:

```cpp
#include "Behaviors.h"

class TurretSpin final : public forge::Behavior {
public:
    void onUpdate(forge::BehaviorContext& ctx) override {
        ctx.object.rotation += ctx.setting("Speed", 45.f) * ctx.deltaTime;
    }
};
```

In **`registerGameBehaviors()`** in `game/GameBehaviors.cpp`, add:

```cpp
registry.add<TurretSpin>("TurretSpin", "A rotating turret",
    {{"Speed", 45.f, -360.f, 360.f, 1.f}});
```

If your class lives in a separate `.cpp`, declare it in a header and include that header in `GameBehaviors.cpp` before registering it. The registration must be present for the behavior to appear in the Inspector. Additional `game/*.cpp` files are automatically included by CMake's source discovery on the next build.

**Important rebuild step:** Close the editor, run `build_windows.bat` or rebuild the Editor target in Visual Studio, and reopen the editor. You must rebuild whenever you change C++ code. You **do not** have to rebuild just to move sprites, change their settings, or save a scene.

### Behavior API

```cpp
void onCreate(forge::BehaviorContext& ctx);  // once at game start
void onUpdate(forge::BehaviorContext& ctx);  // each frame
void onDestroy(forge::BehaviorContext& ctx); // when stopped or destroyed
```

`onUpdate` is required; the other two callbacks are optional. `ctx` provides:

- `ctx.object`: the current `forge::Object` (position, rotation, scale, etc.).
- `ctx.scene`: the current scene and all objects (`ctx.scene.find(id)`).
- `ctx.input`: booleans `left`, `right`, `up`, `down`, `action` (space).
- `ctx.deltaTime`: elapsed seconds for this frame, clamped to 50 ms maximum.
- `ctx.setting("Name", fallback)`: float configured from the Inspector.
- `ctx.spawnSprite(name, "assets/example.png", position, optionalBehaviorName, optionalSettingMap)`: schedules a new sprite; returns its ID.
- `ctx.destroy(id)`: schedules an object for removal.

**Use `ctx.spawnSprite()` and `ctx.destroy()`** rather than modifying `ctx.scene.objects` directly during an update, because changing a vector while other scripts are running can invalidate object references. New objects start their scripts on the next frame. Your behavior can keep instance-specific C++ state in private members (e.g. timers), reset by Stop/Play; this state is not serialized.

The current system supports **one custom C++ behavior per object**. It is not yet a complete component system, physics engine, or script hot-reloader.

## How Option A exports work

CMake builds these targets:

```
EngineCore (static library)
  + GameCode (static library, game/*.cpp)
    + Editor.exe       [game code is compiled in]
    + GameRuntime.exe  [game code is compiled in]
```

Each exported game directory contains:

```
MyGame/
  MyGame.exe        # copy of compiled GameRuntime.exe
  scene.json        # object positions, behavior names, float values
  assets/           # all imported assets, including those spawned from C++
```

**Export EXE does not run the C++ compiler itself.** If you modify `game/*.cpp`, rebuild before exporting. The exported EXE contains your newly compiled behavior classes; the scene tells it which classes to instantiate. Distribute the whole directory, not just the EXE.

The initial engine has no collision detection, physics, runtime GUI, audio, tilemap painting, multiple scenes, animation, visual scripting, or one-file asset packing yet. These are separate future features.

## Directory layout

```
SpriteForge2D/
  CMakeLists.txt
  build_windows.bat
  README.md
  game/
    GameBehaviors.h        # registerGameBehaviors declaration
    GameBehaviors.cpp      # example game scripts + registration
  src/
    Behaviors.h            # public C++ behavior API
    Behaviors.cpp          # behavior lifecycle and registry
    EngineCore.h/.cpp      # objects, scene, rendering, scene JSON
    Editor.cpp             # ImGui visual editor
    Runtime.cpp            # exported game runtime loop
  sample_project/
    scene.json
    assets/
  scripts/
    install_sample.cmake
```

## Troubleshooting

- **New behavior missing in Inspector:** Did you register it in `registerGameBehaviors()` and rebuild/restart Editor?
- **Missing C++ behavior on Play/Export:** That scene references a class not registered in this build. Re-register/rebuild it or change the Inspector selection.
- **Export runs old behavior:** Close Editor, rebuild `Editor` **and** `GameRuntime`, then export again.
- **Behavior class won't compile:** Confirm it inherits `forge::Behavior`, implements `void onUpdate(forge::BehaviorContext&)`, and its definition is visible at registration.
- **Image missing in exported game:** Put it under the project `assets/` folder; spawned sprites also use relative paths beginning with `assets/`.
- **Build tools not found:** Install CMake, Git, and Visual Studio C++ desktop tools.
- **Unsigned executable warning:** Windows SmartScreen may warn on unsigned binaries; code signing is a separate step.
