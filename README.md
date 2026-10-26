# SpriteForge 2D — Independent game projects & built-in C++ editor

A Windows 2D scene editor with **C++17, SFML 3.0.2, Dear ImGui, and JSON scenes**. Each game has its own `scripts/` directory. The editor does **not** link your game scripts. `Build`, `Play`, and `Export EXE` compile your game separately into its own standalone Windows executable (Option A: statically linked C++ behavior classes; no game-logic DLL).

## Build SpriteForge itself (once)

Requires Windows 10/11 x64, Visual Studio 2022 **Desktop development with C++**, CMake 3.28+, Git, and an internet connection for downloading dependencies on the first build.

Double-click `build_windows.bat`, or in a **Developer PowerShell for Visual Studio 2022**:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Editor --parallel 4
.\build\bin\Release\Editor.exe
```

The build installs `build/bin/Release/Editor.exe`, a reusable `sdk/` folder beside it, and a default `Project/` folder. Keep `sdk/` beside the editor: its C++ engine sources and game-building CMake module are required to compile your games. The editor no longer has a `GameRuntime` build target; **individual game projects** now build their own runtime EXEs.

> **Verification:** We successfully compiled and ran the example behavior code and its generated registry with Linux C++17 and minimal SFML stubs. The entire Windows/ImGui/SFML editor and game executable have **not been compiled or tested** here. If Windows compilation reports an error, share the Build Output text so it can be corrected.

## Terminology

- **Game project:** your complete game, including scenes, sprites, C++ code, and assets.
- **Scene:** a level, main menu, map, or another set of game objects in that project. This first engine version loads one scene per game run.
- **Object:** a placed sprite with position, scale, rotation and an optional C++ behavior.
- **Behavior:** compiled game logic attached to an object, e.g. `TurretAI` or `MoveWithKeys`.
- **Asset:** a PNG, JPG, etc., imported into the project. **Prefab and multiple-scene project management are not implemented yet.**

## How to use the C++ editor

1. Open `Editor.exe` and use **File → New project** to select a folder, or work in the included `Project/` demo.
2. Import PNG/JPG sprites into the scene using the **Assets** tab.
3. Switch to the **Code** tab under the scene. Click **Expand** to make the text editor taller.
4. Click **New behavior**, enter e.g. `TurretAI`, and SpriteForge creates `scripts/TurretAI.cpp`. You can also create arbitrary `.cpp`, `.h`, `.hpp`, or `.inl` files using **New file**.
5. Select files in the Code panel to edit them. **Save file** saves changes; switching files or building also saves edits.
6. Right-click file names to **Rename** or **Delete** them (with confirmation). If other source files include a renamed header, update those `#include`s manually.
7. To give a behavior Inspector-adjustable values, open its `.cpp` and use **+ Float property**. In its C++ class read a value with `ctx.setting("Range", 100.f)`. The properties are recorded in `behaviors.json`, shown in the Inspector, and built into game registration code.
8. Select an object in the scene and choose the behavior under **Inspector → C++ Behavior**. Change its float properties as needed.
9. Click **Build** to compile the game, or **Play / Compile** to compile and run it in a **separate game window**. Press Escape in the game to quit, or press Stop Game in the editor.
10. **Export EXE** compiles your current game code, then copies the compiled `GameRuntime.exe` under your chosen game name alongside `scene.json` and the whole `assets/` tree. Distribute the exported **folder**, not just the EXE.

**The editor itself is not rebuilt when you change a game script.** Only your game C++ and its dependencies are compiled. First-time builds can take longer because CMake downloads and compiles dependencies in each project's build folder. Subsequent builds are incremental. The editor does not include C++ autocomplete, breakpoints, syntax highlighting, or live reloading.

### How games and projects are arranged

```text
SpriteForge2D/                         editor source and build scripts
  src/Editor.cpp                      editor implementation
  src/ProjectWorkspace.cpp            project/scripts and behavior metadata
  src/BuildRunner.cpp                 external CMake game builds
  src/EngineCore.*                     shared engine code
  src/Behaviors.*                      C++ behavior API and runtime
  src/Runtime.cpp                      exported runtime entry point
  cmake/SpriteForgeGame.cmake          per-game build configuration
  sample_project/                      installed as Project/ beside Editor.exe

MyFactoryGame/                         INDEPENDENT GAME PROJECT
  CMakeLists.txt                       delegates build to SpriteForge SDK
  scene.json                           current scene
  behaviors.json                       registered class names and properties
  assets/
    player.png
  scripts/
    Player.cpp                          your editable C++ code
    TurretAI.cpp                        your editable C++ code
    .generated/GameBehaviors.cpp        automatically generated; do NOT edit
  .spriteforge-build/                 created on first Build, not source-controlled
    build.log                           shown in Build Output
    bin/Release/GameRuntime.exe         standalone compiled game logic
```

### A minimal behavior

The **New behavior** action generates a class with a matching registration function. You only need to implement `onUpdate`, which is called each frame. You can optionally implement `onCreate` and `onDestroy`.

```cpp
class TurretAI final : public forge::Behavior {
public:
    void onUpdate(forge::BehaviorContext& ctx) override {
        ctx.object.rotation += ctx.setting("Speed", 30.f) * ctx.deltaTime;
    }
};
```

`ctx` provides the object's transform, scene lookups, keyboard input, delta time, and `spawnSprite`/`destroy` for deferred object mutations. The generated registration function at the bottom of the file should stay intact. Float settings are defined in the Code tab and stored in `behaviors.json`. Edits to object-specific settings remain in `scene.json`.

### Source and export behavior

`Editor.exe` is a stable scene/code editing tool that reads the game's `behaviors.json`. It **does not execute user-defined C++ behavior classes inside the editor process**. To test them, it starts a CMake build and then launches the project's `GameRuntime.exe` with `--scene <absolute-path>`.

Each `GameRuntime.exe` embeds the current project's C++ behavior classes. It loads a JSON scene and assets when it starts. An exported EXE defaults to `scene.json` beside itself. You cannot run an exported EXE without its accompanying assets if the scene uses sprites.

When you use **Build Output**, you can read the compiler's stdout/stderr from `.spriteforge-build/build.log`. If an active game window is running, close it before compiling again; Windows prevents replacing a running EXE.

## Migration from earlier SpriteForge releases

1. **Back up your old `Project/` folder** (especially its `scene.json`, `assets/`, and any game code).
2. Extract this ZIP to **a new folder** and run its `build_windows.bat`. Don't copy the new sources over an older build without refreshing CMake.
3. The included default project has matching `.cpp` scripts for `MoveWithKeys`, `Spin`, and `Patrol` (and a `behaviors.json`).
4. For previous projects that only had `scene.json` and `assets/`, open them using **File → Open project**. The editor creates a new `scripts/` directory and behavior manifest. Move/adapt any prior `game/GameBehaviors.cpp` classes into the new project, using **New behavior** to generate registration boilerplate for each class.
5. When you change *game* code, use the editor's **Build** button; don't rerun `build_windows.bat` unless you've changed SpriteForge engine/editor sources.

For the bundled default `Project/` located beside Editor.exe, the post-build installer also tries to migrate the built-in example behaviors without overwriting existing files.

## Current limitations

This is a small first-version engine, not yet a production game engine. It has one active scene per game, one behavior per object, and supports numeric Inspector properties only. It does not yet provide a generalized physics engine, collision handling, tilemap painting, scripting debugger, game UI widgets, prefabs, animation, networking, or multiple scene loading. Code editing is a plain multiline text field, without syntax highlighting or autocomplete, and limited to 1 MiB per file. Custom scripts may use engine public headers via `#include "Behaviors.h"`; project source changes compile into that game's EXE only.

## Troubleshooting: shellapi.h / EXTERN_C compilation errors

If the in-editor **Build** displays Windows SDK `shellapi.h` errors such as `C2146` / `C2086` involving `EXTERN_C` when compiling `sdk/src/Runtime.cpp`, ensure `#include <windows.h>` appears **before** `#include <shellapi.h>` in `src/Runtime.cpp`. This header ordering is fixed in this archive.

If you already built the editor from an older archive, the runtime template is copied to `build/bin/Release/sdk/src/Runtime.cpp` (alongside `Editor.exe`). Either change the order there and rebuild the game, or replace the original `src/Runtime.cpp` and rebuild the editor with `build_windows.bat` so the fixed template is copied. Running the `.bat` builds **the editor**, while the Build button inside SpriteForge builds **the current game**.
