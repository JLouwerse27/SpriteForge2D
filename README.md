# SpriteForge 2D (SFML 3 Visual Editor + EXE Export)

A small, editable **Windows 2D game engine MVP** written in C++17. The editor uses **SFML 3.0.2** and **Dear ImGui + ImGui-SFML**. Games are exported as a native Windows `.exe` plus `scene.json` and an `assets` folder.

> **Build status:** Source project supplied, not compiled or run on Windows in the creation environment. Build it using the instructions below. The first configure requires Internet access to download open-source dependencies.

## Requirements

- Windows 10/11 (x64)
- Visual Studio 2022 with **Desktop development with C++** installed
- CMake **3.28+** and Git available on PATH
- Internet access the first time you run CMake (FetchContent downloads dependencies)

## Build on Windows

From a **Developer PowerShell for VS 2022**, open this folder and run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Editor --parallel 4
.\build\bin\Release\Editor.exe
```

Or double-click `build_windows.bat`. The script builds the editor and its GameRuntime, then opens the editor.

The first build can download/build several dependencies. CMake automatically pins and fetches:
- SFML 3.0.2
- Dear ImGui 1.91.6
- ImGui-SFML 3.0
- nlohmann/json 3.12.0

These libraries are statically linked for the Windows executables; no SFML DLL files are intentionally required in exported builds. Compiling with Visual Studio's static MSVC CRT further avoids requiring a separate VC++ redistributable for the engine code.

## Editor UI

| Panel | What to do |
|---|---|
| **Assets** | Click **Import PNG / JPG...** and choose a local image. You can drag the thumbnail into the Scene or double-click it to place in the center. |
| **Hierarchy** | Select a sprite, delete it, or drag entries to change draw order. |
| **Scene** | Left-click an object to select, left-drag to move, middle-drag to pan, scroll to zoom. |
| **Inspector** | Modify position, scale, rotation, visibility, and basic player movement. Click empty scene to edit background and size. |
| **Play/Stop** | Play a temporary copy of the scene in the viewport. Objects marked **Controllable** respond to WASD / arrows. Stop returns to edit mode without saving the test movement. |
| **File > Save** | Save a JSON scene; all sprites are stored in the sibling `assets/` folder. |
| **Export EXE** | Type the game name in the input above the viewport and click **Export EXE**; select a destination parent folder. |

The first launch opens a sample project at `build/bin/Release/Project/scene.json`. In Debug builds it would be `build/bin/Debug/Project/scene.json`.

## EXE export

Exporting `MyPlatformer` creates:

```
<chosen folder>/MyPlatformer/
    MyPlatformer.exe
    scene.json
    assets/
        player.png
        enemy.png
        ground.png
```

**You must distribute the entire exported folder.** Renaming/copying the `.exe` alone is not enough, because it loads `scene.json` and referenced images at runtime.

The export **does not compile source code**: it copies the separately built `GameRuntime.exe` and renames it to your chosen game name. This works after you successfully build both editor and runtime on Windows. Exporting a game is then immediate and does not require the recipient to have Visual Studio.

## Supported game behavior

- Sprite import, scene placement, selection, transforms, and draw order
- Static objects and a basic player movement behavior (`Controllable` checkbox, WASD / arrow keys)
- Scene save/load as JSON
- Run/pause via Play and Stop (Stop resets test movement)
- Windows desktop game export with an independent native runtime

**Not yet implemented:** tilemap painting, collision detection, gravity, sprite sheet animation, custom scripts, multiple scenes, runtime menu UI, sound, gamepad mappings, gizmo handles, undo/redo, or packaging assets *inside* a single `.exe`. This is a working-design MVP rather than a complete Unity/Godot replacement.

## Adding your own behavior

Edit `src/EngineCore.cpp` (`updatePlayerControls`) to add movement rules, or extend the `Object` struct in `src/EngineCore.h` and its serialization functions. The `GameRuntime` target is compiled using the same engine core as the editor. Rebuild after C++ changes; only scene/sprite edits need no rebuild.

## File layout

```
SpriteForge2D/
  CMakeLists.txt
  README.md
  build_windows.bat
  src/
    EngineCore.h
    EngineCore.cpp
    Editor.cpp
    Runtime.cpp
  scripts/
    install_sample.cmake
  sample_project/
    scene.json
    assets/{player.png,enemy.png,ground.png}
```

## Troubleshooting

- **CMake says Git not found**: install Git for Windows, reopen terminal.
- **No C++ compiler**: install Visual Studio 2022 Desktop development with C++ workload.
- **No `GameRuntime.exe` beside `Editor.exe`**: build the `Editor` target, which builds both. Do not move editor alone.
- **Missing sprite**: the scene references paths relative to the scene folder such as `assets/player.png`; reimport the image.
- **Windows SmartScreen warning**: self-built unsigned `.exe` files may trigger warnings on other PCs. Code signing is a separate process.
- **Can't export directly to one `.exe`**: this version always requires a data folder. A standalone single-file asset packer would be a future extension.
