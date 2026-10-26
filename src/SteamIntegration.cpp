#include "SteamIntegration.h"
#if defined(SPRITEFORGE_WITH_STEAM) && SPRITEFORGE_WITH_STEAM
#include <steam/steam_api.h>
#include <algorithm>
#include <deque>
#include <unordered_set>

namespace forge::steam {
namespace {
struct State {
    bool active = false;
    bool statsReady = false;
    std::uint64_t lobby = 0;
    std::deque<std::string> pendingAchievements;
    CCallback<State, UserStatsReceived_t> statsCallback;
    CCallback<State, LobbyEnter_t> lobbyEnterCallback;
    CCallback<State, GameLobbyJoinRequested_t> lobbyInviteCallback;
    CCallback<State, SteamNetworkingMessagesSessionRequest_t> sessionCallback;

    State() : statsCallback(this, &State::onStats),
              lobbyEnterCallback(this, &State::onLobbyEnter),
              lobbyInviteCallback(this, &State::onInvite),
              sessionCallback(this, &State::onSessionRequest) {}

    void onStats(UserStatsReceived_t* result) {
        if (result->m_nGameID != SteamUtils()->GetAppID()) return;
        statsReady = result->m_eResult == k_EResultOK;
        if (!statsReady) return;
        while (!pendingAchievements.empty()) {
            const std::string id = std::move(pendingAchievements.front());
            pendingAchievements.pop_front();
            if (SteamUserStats()->SetAchievement(id.c_str())) SteamUserStats()->StoreStats();
        }
    }
    void onLobbyEnter(LobbyEnter_t* event) {
        if (event->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess)
            lobby = event->m_ulSteamIDLobby;
    }
    void onInvite(GameLobbyJoinRequested_t* event) {
        SteamMatchmaking()->JoinLobby(event->m_steamIDLobby);
    }
    void onSessionRequest(SteamNetworkingMessagesSessionRequest_t* event) {
        // Accept only messages from members of OUR lobby, not arbitrary Steam users.
        if (!lobby) return;
        CSteamID sender(event->m_identityRemote.GetSteamID64());
        const CSteamID id(lobby);
        for (int i = 0; i < SteamMatchmaking()->GetNumLobbyMembers(id); ++i) {
            if (SteamMatchmaking()->GetLobbyMemberByIndex(id, i) == sender) {
                SteamNetworkingMessages()->AcceptSessionWithUser(event->m_identityRemote);
                return;
            }
        }
    }
};
State& state() { static State s; return s; }
} // namespace

bool initialize() {
    if (state().active) return true;
    if (!SteamAPI_Init()) return false;
    state().active = true;
    state().statsReady = false;
    SteamUserStats()->RequestCurrentStats();
    return true;
}
void pump() { if (available()) SteamAPI_RunCallbacks(); }
void shutdown() {
    if (!state().active) return;
    leaveLobby();
    state().statsReady = false;
    state().pendingAchievements.clear();
    SteamAPI_Shutdown();
    state().active = false;
}
bool available() { return state().active; }
std::uint64_t playerId() { return available() ? SteamUser()->GetSteamID().ConvertToUint64() : 0; }
std::string playerName() { return available() ? SteamFriends()->GetPersonaName() : std::string{}; }
bool unlockAchievement(const std::string& apiName) {
    if (!available() || apiName.empty()) return false;
    if (!state().statsReady) {
        state().pendingAchievements.push_back(apiName);
        return true; // queued, not yet verified by Steam
    }
    if (!SteamUserStats()->SetAchievement(apiName.c_str())) return false;
    return SteamUserStats()->StoreStats();
}
bool hasAchievement(const std::string& apiName) {
    if (!available() || !state().statsReady) return false;
    bool unlocked = false;
    return SteamUserStats()->GetAchievement(apiName.c_str(), &unlocked) && unlocked;
}
bool setIntStat(const std::string& apiName, int value) {
    if (!available() || !state().statsReady) return false;
    return SteamUserStats()->SetStat(apiName.c_str(), value) && SteamUserStats()->StoreStats();
}
void openOverlay(const std::string& dialog) {
    if (available()) SteamFriends()->ActivateGameOverlay(dialog.c_str());
}
bool createLobby(int maxPlayers) {
    if (!available() || maxPlayers < 2 || maxPlayers > 250 || state().lobby) return false;
    return SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, maxPlayers) != k_uAPICallInvalid;
}
bool joinLobby(std::uint64_t id) {
    if (!available() || !id) return false;
    return SteamMatchmaking()->JoinLobby(CSteamID(id)) != k_uAPICallInvalid;
}
void leaveLobby() {
    if (available() && state().lobby) SteamMatchmaking()->LeaveLobby(CSteamID(state().lobby));
    state().lobby = 0;
}
std::uint64_t lobbyId() { return state().lobby; }
std::vector<std::uint64_t> lobbyMembers() {
    std::vector<std::uint64_t> result;
    if (!available() || !state().lobby) return result;
    const CSteamID id(state().lobby);
    const int count = SteamMatchmaking()->GetNumLobbyMembers(id);
    for (int i = 0; i < count; ++i)
        result.push_back(SteamMatchmaking()->GetLobbyMemberByIndex(id, i).ConvertToUint64());
    return result;
}
bool showInviteDialog() {
    if (!available() || !state().lobby) return false;
    SteamFriends()->ActivateGameOverlayInviteDialog(CSteamID(state().lobby));
    return true;
}
bool send(std::uint64_t recipient, const std::string& data, bool reliable) {
    if (!available() || !state().lobby || recipient == 0 || data.size() > 1024 * 1024) return false;
    const auto members = lobbyMembers();
    if (std::find(members.begin(), members.end(), recipient) == members.end()) return false;
    SteamNetworkingIdentity to;
    to.SetSteamID64(recipient);
    const int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
    return SteamNetworkingMessages()->SendMessageToUser(to, data.data(), static_cast<std::uint32_t>(data.size()), flags, 0) == k_EResultOK;
}
std::vector<Packet> receive() {
    std::vector<Packet> result;
    if (!available() || !state().lobby) return result;
    SteamNetworkingMessage_t* messages[32] = {};
    const int count = SteamNetworkingMessages()->ReceiveMessagesOnChannel(0, messages, 32);
    const auto members = lobbyMembers();
    for (int i = 0; i < count; ++i) {
        auto* m = messages[i];
        const auto from = m->m_identityPeer.GetSteamID64();
        if (m->m_cbSize >= 0 && m->m_cbSize <= 1024 * 1024 &&
            std::find(members.begin(), members.end(), from) != members.end())
            result.push_back({from, std::string(static_cast<const char*>(m->m_pData), static_cast<size_t>(m->m_cbSize))});
        m->Release();
    }
    return result;
}
} // namespace forge::steam
#else
namespace forge::steam {
bool initialize() { return false; }
void pump() {}
void shutdown() {}
bool available() { return false; }
std::uint64_t playerId() { return 0; }
std::string playerName() { return {}; }
bool unlockAchievement(const std::string&) { return false; }
bool hasAchievement(const std::string&) { return false; }
bool setIntStat(const std::string&, int) { return false; }
void openOverlay(const std::string&) {}
bool createLobby(int) { return false; }
bool joinLobby(std::uint64_t) { return false; }
void leaveLobby() {}
std::uint64_t lobbyId() { return 0; }
std::vector<std::uint64_t> lobbyMembers() { return {}; }
bool showInviteDialog() { return false; }
bool send(std::uint64_t, const std::string&, bool) { return false; }
std::vector<Packet> receive() { return {}; }
} // namespace forge::steam
#endif
