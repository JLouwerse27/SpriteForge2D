#pragma once

namespace forge { class BehaviorRegistry; }

// Register all game-specific C++ behavior classes here. This gets compiled
// into Editor.exe and GameRuntime.exe (Option A: no DLL, no script interpreter).
void registerGameBehaviors(forge::BehaviorRegistry& registry);
