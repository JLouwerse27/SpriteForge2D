# Example game code: Steam achievements and multiplayer

These examples are **illustrations for behavior classes**. Add calls to an existing behavior generated through **Code > New behavior**, or create a new behavior for each example. You do **not** need to change SpriteForge editor code. `#include "SteamIntegration.h"` works for Steam-enabled and ordinary builds; when Steam is disabled, methods return false/empty results.

## Unlock an achievement from gameplay

Suppose you have published the API Name `ACH_BUILD_FIRST_TURRET` in Steamworks Partner and also added the same ID in **SpriteForge > Steam**:

```cpp
#include "Behaviors.h"
#include "SteamIntegration.h"

// Inside your TurretBuilder behavior, after the first successful turret creation:
if (!forge::steam::hasAchievement("ACH_BUILD_FIRST_TURRET")) {
    forge::steam::unlockAchievement("ACH_BUILD_FIRST_TURRET");
}
```

`unlockAchievement()` calls `SetAchievement()` and `StoreStats()`. If Steam stats are not ready yet, it queues the achievement until the stats callback arrives; calling it with a typo or an unpublished achievement ID will not unlock anything. A game without Steamworks compiles and plays normally, but achievements will not unlock. For proper progression, keep an internal game-save state, too.

## Create a lobby and invite friends

Use edge-triggered keyboard input so holding Space does not create repeated lobbies:

```cpp
#include "Behaviors.h"
#include "SteamIntegration.h"

class LobbyControl final : public forge::Behavior {
    bool wasPressed_ = false;
public:
    void onUpdate(forge::BehaviorContext& ctx) override {
        const bool pressed = ctx.input.action;
        if (pressed && !wasPressed_) {
            if (forge::steam::lobbyId() == 0)
                forge::steam::createLobby(4); // Async: lobby appears after callback
            else
                forge::steam::showInviteDialog(); // Opens Steam invite overlay
        }
        wasPressed_ = pressed;
    }
};
```

Lobby invitations are delivered by Steam. When an invitation is accepted, SpriteForge automatically processes a `GameLobbyJoinRequested_t` callback, or Steam launches the game with `+connect_lobby <id>` which SpriteForge also handles.

## Send and receive multiplayer messages

After joining the same lobby:

```cpp
// Send player input to a different member:
const std::uint64_t me = forge::steam::playerId();
for (const auto other : forge::steam::lobbyMembers()) {
    if (other != me) forge::steam::send(other, "jump", true);
}

// Drain messages once per frame in ONE global/network-manager behavior:
for (const auto& packet : forge::steam::receive()) {
    // packet.sender = SteamID; packet.payload = bytes / string received.
    // Validate sender, packet format, input rate, and authority BEFORE applying.
}
```

This is a transport foundation, NOT a finished multiplayer game. You must decide which player is authoritative, serialize and validate state, replicate spawning and destruction, handle disconnects and resynchronization, and test latency. **Steam relay networking is not Valve hosting your gameplay simulation**.
