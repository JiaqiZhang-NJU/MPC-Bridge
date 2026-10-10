// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Bridge {

// Values intentionally match Win32 accelerator modifiers without requiring windows.h.
enum Modifier : std::uint8_t { Shift = 4, Control = 8, Alt = 16 };

enum class Action {
	None, PlayToggle, FullscreenToggle, FullscreenExit, MuteToggle,
	SeekRelative, SeekPercent, VolumeRelative, RateRelative,
	SubtitlesToggle, FrameForward, FrameBackward, PlaylistNext, PlaylistPrevious
};

struct HoldAction {
	std::uint32_t delayMs = 300;
	double rate = 3.0;
};

struct Binding {
	std::uint16_t key = 0;
	std::uint8_t modifiers = 0;
	Action action = Action::None;
	double value = 0;
	bool repeat = false;
	std::optional<HoldAction> hold;
};

struct ShortcutProfile {
	std::string id;
	std::string displayName;
	std::vector<Binding> bindings;

	const Binding* Find(std::uint16_t key, std::uint8_t modifiers) const;
};

constexpr std::size_t MaxProfileBytes = 128 * 1024;

// Transactional: output is untouched on failure. Profiles contain data, never commands/scripts.
bool ParseProfile(std::string_view json, ShortcutProfile& output, std::string& error);
bool LoadBuiltin(std::string_view id, ShortcutProfile& output, std::string& error);

struct InputDecision {
	bool consume = false;
	const Binding* binding = nullptr;
};

// Used by the native adapter and tested without MFC. Suppressed repeats are still consumed.
InputDecision RouteKey(const ShortcutProfile& profile, std::uint16_t key,
	std::uint8_t modifiers, bool repeat, bool playerHasFocus);

// Seek targets in 100 ns media units; bounds and overflow are handled here.
std::int64_t RelativeTarget(std::int64_t position, std::int64_t duration, double seconds);
std::int64_t PercentTarget(std::int64_t duration, double percent);

} // namespace Bridge
