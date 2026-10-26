#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Game-only Steamworks facade. It is safe to call these functions in non-Steam
// projects; they return false/empty results instead of requiring Steamworks.
// For a Steam game, initialize() is called by Runtime.cpp before the SFML
// window creates its OpenGL context, and pump() is called once per frame.
namespace forge::steam {

struct Packet { std::uint64_t sender = 0; std::string payload; };

bool initialize();
void pump();
void shutdown();
bool available();
std::uint64_t playerId();
std::string playerName();

// Must match a published API Name in Steamworks Partner administration.
bool unlockAchievement(const std::string& apiName);
bool hasAchievement(const std::string& apiName);
bool setIntStat(const std::string& apiName, int value);
void openOverlay(const std::string& dialog = "friends");

// Steam lobbies are discovery/invitation, NOT game-state synchronization.
bool createLobby(int maxPlayers = 4);
bool joinLobby(std::uint64_t lobbyId);
void leaveLobby();
std::uint64_t lobbyId();
std::vector<std::uint64_t> lobbyMembers();
bool showInviteDialog();

// Reliable/unreliable messages transported over Steam's P2P networking layer.
// The caller must serialize, validate and synchronize its own game state.
bool send(std::uint64_t recipient, const std::string& data, bool reliable = true);
std::vector<Packet> receive();

} // namespace forge::steam
