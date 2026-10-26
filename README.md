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

---

# Steamworks integration (optional, Windows x64)

**This edition includes optional Steamworks game integration**: Steam achievements, overlay, lobbies/friend invitations and a P2P message transport foundation. Steamworks is not bundled: the SDK is provided directly by Valve to Steamworks partners and must be obtained separately. Steam features are **not required** for an ordinary SpriteForge project.

## 1. Download the official Steamworks SDK

1. Create or access your Steamworks partner account and your game's App ID: <https://partner.steamgames.com/>.
2. Download the official SDK: <https://partner.steamgames.com/doc/sdk> (SDK download may require partner access).
3. Extract it somewhere on Windows. Find the directory containing **`public/steam/steam_api.h`** and **`redistributable_bin/win64/steam_api64.lib`**, e.g. `C:\SDKs\steamworks_sdk_XXX\sdk`.
4. Rebuild the SpriteForge **editor one time** from this ZIP using `build_windows.bat`, preferably in a **new directory** first; back up your previous `Project/`, `scene.json`, `scripts/`, and `assets/`. Changing game behaviors still does not rebuild the editor.

**Important:** The official SDK headers, import `.lib`, and Steam DLL are **not in this ZIP**. Without the SDK and an appropriate Steam App ID, this cannot be tested as a functioning Steam game. Non-Steam builds still work.

## 2. Configure an individual game in the editor

1. Open your game project in SpriteForge.
2. Open the **Steam** tab below the scene.
3. Check **Enable Steamworks**, enter *your actual Steam App ID*, and browse to the extracted SDK root (`sdk` folder containing `public/` and `redistributable_bin/`).
4. Click **Save Steam settings**. Settings and achievement metadata live in the game's `steam.json` file; SDK path is a local development setting and not bundled into the finished game.
5. Add achievements using **API Name**, **Display Name**, and **Description**. Example ID: `ACH_FIRST_FACTORY`.
6. **Separately create and publish** those exact API names, names, icons, and descriptions in Steamworks Partner > Achievements. The editor cannot change your Steamworks Partner account. Local achievement entries are just a convenient checklist and a source of IDs for your C++ code.
7. Press **Play / Compile**. CMake now builds only this game's `GameRuntime.exe` with the Steamworks SDK. The Steam DLL is copied beside the runtime; the editor writes a **development-only** `steam_appid.txt` there and launches with that directory as the working directory.

SteamAPI is initialized by `Runtime.cpp` **before** SFML constructs the game window/OpenGL context, allowing Steam to inject its overlay. The game runs `SteamAPI_RunCallbacks()` once per frame and shuts Steam down when it closes. Shift+Tab works only when Steam's overlay is enabled and initialization succeeds; launch through Steam or use a licensed, configured developer App ID. You can still play an enabled build offline if initialization fails, but Steam functions return `false`/empty results.

## 3. Unlock achievements from C++

In a C++ behavior, include the public wrapper header:

```cpp
#include "SteamIntegration.h"
// After completing a gameplay task:
forge::steam::unlockAchievement("ACH_FIRST_FACTORY");
```

For example, trigger it after your factory places its first working conveyor belt, when a tank is repaired for the first time, or on the first successful tower-defense wave. The local API name must **exactly** match a published achievement in Steamworks Partner. The wrapper calls `SteamUserStats()->SetAchievement()` and `StoreStats()`; achievements requested before Steam stats finish loading are queued. Other useful methods: `forge::steam::available()`, `hasAchievement()`, `setIntStat()` and `openOverlay("achievements")`.

## 4. Multiplayer foundation — NOT automatic multiplayer

Use `forge::steam::createLobby(maxPlayers)` to start a friend-only lobby and `forge::steam::showInviteDialog()` to show Steam's invite dialog. Steam automatically supplies lobby-join callbacks. The runtime handles `+connect_lobby <SteamID>` command-line arguments from Steam. Useful APIs:

| C++ API | Purpose |
|---|---|
| `steam::playerId()` and `playerName()` | Local Steam identity |
| `steam::createLobby(4)` | Request a friends-only lobby |
| `steam::joinLobby(lobbyID)` | Join an existing Steam lobby |
| `steam::lobbyId()` / `lobbyMembers()` / `leaveLobby()` | Inspect and leave lobby |
| `steam::showInviteDialog()` | Invite Steam friends through overlay |
| `steam::send(peerID, bytes, reliable)` | Send a P2P message through Steam Networking Messages |
| `steam::receive()` | Drain received messages (SteamID + payload), then validate and apply yourself |

See `examples/SteamExamples.md` for C++ snippets. Transport sessions accept messages only from members of your current lobby and messages are size-limited. You still need to create multiplayer gameplay rules, ownership/authority, synchronization, interpolation, packet encoding/validation, anti-cheat defenses, and disconnect recovery. Steam **does not provide free hosted dedicated game servers**. P2P via Valve's relay network allows a player to act as host without relying on public port forwarding in many cases.

## 5. Export and publish on Steam

1. In **Steam** tab, enable Steam, enter your App ID and SDK location, then Build / Test.
2. **Export EXE** creates `YourGame.exe`, `scene.json`, the complete `assets/` tree, and **`steam_api64.dll`**. It intentionally does **not** include `steam_appid.txt` (development-only). Scripts are linked into the game executable: your users do not need your source files or the SDK.
3. Create your product and configure its executable/launch options, supported OS, artwork, store listing, builds and depots in the Steamworks Partner portal.
4. Upload the exported folder with **SteamPipe / ContentBuilder**, which is shipped as part of the official Steamworks SDK. The editor does **not** directly upload or publish games to Steam.
5. Test a build installed through Steam with a licensed test account, verify Shift+Tab, unlocked achievements, packaging and (if used) invitations/connectivity; complete Steam's review/release steps.

Steam integration is optional; unchecking it and building again produces an ordinary non-Steam game. Your original scene and behaviors remain usable.

## Common mistakes

- **"Steam enabled but SDK path missing"**: select the inner SDK directory containing `public/` and `redistributable_bin/`, not the enclosing download folder.
- **`SteamAPI_Init` fails**: run Steam as the same Windows account, use a valid licensed App ID, and check the development-only `steam_appid.txt` beside the built EXE.
- **Overlay missing**: enable the Steam overlay in Steam, ensure the game actually initialized Steam, and test launching it from Steam. A native SFML/OpenGL game should be supported.
- **Achievements not unlocking**: Steamworks API names are case-sensitive; achievements must be configured and published on the Steamworks Partner dashboard.
- **Invitations work but other players don't move**: matchmaking and transport are included, but state replication and game-specific multiplayer logic still need to be written.
- **Error after switching Steam on/off**: close a running game before rebuilding; inspect SpriteForge **Build Output** and confirm the requested SDK and App ID.

## Testing status

The non-Steam SteamIntegration stub compiles with Linux g++ C++17. The Steam-enabled wrapper passes a C++17 syntax check against a **locally authored API-shaped test header, not Valve's real Steamworks SDK**. Windows + real Steamworks linking, overlay display, achievement unlocks and multiplayer connectivity have NOT been tested here. Validate these on your PC with the official SDK and your own Steamworks App ID.

## 2D array tilemaps (v0.5 prototype)

This version adds a **Tilemap_World** entry and a **Tilemap** tab to the editor. It is designed for grid-based worlds, such as a Terraria-like map, while keeping sprites, C++ behaviors, Steamworks options, and EXE export.

### Using the editor

1. Run `build_windows.bat` in the source root to rebuild the editor. The CMake post-build step copies the updated EngineCore into the game SDK.
2. Open your game project. Select **Create Tilemap** in Hierarchy (or use the Tilemap tab).
3. Select the **Tilemap_World** row in Hierarchy to edit array width, height and tile size in Inspector.
4. Import a 16×16 or 32×32 tile PNG using **Assets**. In the **Tilemap** tab, click `+ filename.png` to register it as a numbered block type. The block ID is stable across saves; unused images don't automatically become block types.
5. Select a block type and enable **Paint mode**, then **left-drag** across the scene to paint. **Right-drag** erases blocks (sets value `0`, air). Middle-drag pans and the mouse wheel zooms. Turn off **Paint Tiles** to select and drag ordinary sprites again.
6. Press **File > Save scene**. The tilemap array and block type mappings are stored in `scene.json` version 2. Old version 1 files remain loadable. **Play / Compile** and **Export EXE** use the same tilemap in `GameRuntime.exe`.

The first-install demo comes with an example grass/dirt/stone/ore world, plus five tile textures. Existing user projects are intentionally **not overwritten** by rebuilding the editor. If you already have a `Project/scene.json`, use Create Tilemap in that project, or open `sample_project` from the source ZIP as a separate project.

### How the block array is represented in C++

The world uses a flat `std::vector<int>` of length `width * height`, with row-major indexing. To read and write a cell from a compiled behavior:

```cpp
const int x = 10;
const int y = 12;
const int current = ctx.scene.tilemap.get(x, y);
ctx.scene.tilemap.set(x, y, 2); // set dirt, if block ID 2 is defined
ctx.scene.tilemap.set(x, y, 0); // remove block (air)
```

The behavior receives the scene via `forge::BehaviorContext`. You can also use `ctx.scene.tilemap.width`, `.height`, `.blockSize`, `.tiles`, or `.cells`. `resize()` preserves existing overlapping cells. The renderer draws the visible portion of the array with textures at the configured tile size. The tilemap renders behind ordinary SpriteForge objects.

### What is not included yet

- This is a **first functional tilemap** with visible-region culling, **not** an optimized Terraria-scale chunk/atlas renderer. Tiles are currently drawn individually; massive worlds need a vertex-array batched renderer and chunk management for performance.
- No automatically applied block collisions, gravity, liquids, lighting, wiring, or procedural generation. C++ scripts can read/write tile IDs, but you need to program gameplay rules yourself.
- One tilemap per scene, not separate foreground/background/wall layers. A tile definition stores ID, name, texture path and a `solid` flag (the latter is ready for future collision code).
- The array panel shows a small, read-only preview; you paint in Scene View and configure settings in Inspector.
- This release is source code; a full native Visual Studio / SFML 3 build was not available to test in the authoring environment.

### Build and source compatibility

You need **Visual Studio 2022**, **CMake 3.28+**, and **Git**. The first build downloads SFML 3.0.2, ImGui, and nlohmann/json via CMake. The optional Steamworks integration is retained; it is disabled by default and needs your own SDK/App ID to test.

Back up your existing project before upgrading. The old `scene.json` format loads, while saving writes format version 2 with a `tilemap` object.
