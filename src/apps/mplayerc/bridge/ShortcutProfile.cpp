// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShortcutProfile.h"
#include "BuiltinProfiles.h"
#include "ExtLib/rapidjson/include/rapidjson/document.h"
#include "ExtLib/rapidjson/include/rapidjson/error/en.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace Bridge {
namespace {

bool Fields(const rapidjson::Value& object, std::initializer_list<std::string_view> allowed,
	std::string& error)
{
	if (!object.IsObject()) { error = "Expected an object."; return false; }
	std::set<std::string> seen;
	for (const auto& member : object.GetObject()) {
		const std::string name(member.name.GetString(), member.name.GetStringLength());
		if (!seen.insert(name).second) { error = "Duplicate field: " + name; return false; }
		if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
			error = "Unknown field: " + name; return false;
		}
	}
	return true;
}

bool String(const rapidjson::Value& object, const char* field, std::string& value)
{
	const auto it = object.FindMember(field);
	if (it == object.MemberEnd() || !it->value.IsString() || !it->value.GetStringLength()) { return false; }
	value.assign(it->value.GetString(), it->value.GetStringLength());
	return value.find('\0') == std::string::npos;
}

bool Key(std::string_view name, std::uint16_t& key)
{
	if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) {
		key = static_cast<std::uint16_t>(name[0]); return true;
	}
	const std::pair<std::string_view, std::uint16_t> keys[] = {
		{"Space", 0x20}, {"Escape", 0x1b}, {"Left", 0x25}, {"Up", 0x26},
		{"Right", 0x27}, {"Down", 0x28}, {"Home", 0x24}, {"End", 0x23},
		{"Enter", 0x0d}, {"Comma", 0xbc}, {"Period", 0xbe}
	};
	for (const auto& candidate : keys) {
		if (name == candidate.first) { key = candidate.second; return true; }
	}
	return false;
}

bool ReadAction(const rapidjson::Value& object, Binding& binding, std::string& error)
{
	std::string name;
	if (!String(object, "action", name)) { error = "Missing action."; return false; }
	const std::pair<std::string_view, Action> actions[] = {
		{"none", Action::None}, {"play.toggle", Action::PlayToggle},
		{"fullscreen.toggle", Action::FullscreenToggle}, {"fullscreen.exit", Action::FullscreenExit},
		{"mute.toggle", Action::MuteToggle}, {"seek.relative", Action::SeekRelative},
		{"seek.percent", Action::SeekPercent}, {"volume.relative", Action::VolumeRelative},
		{"rate.relative", Action::RateRelative}, {"subtitles.toggle", Action::SubtitlesToggle},
		{"frame.forward", Action::FrameForward}, {"frame.backward", Action::FrameBackward},
		{"playlist.next", Action::PlaylistNext}, {"playlist.previous", Action::PlaylistPrevious}
	};
	bool known = false;
	for (const auto& action : actions) {
		if (name == action.first) { binding.action = action.second; known = true; break; }
	}
	if (!known) { error = "Unknown action: " + name; return false; }

	const char* argument = nullptr;
	double minimum = 0, maximum = 0;
	switch (binding.action) {
	case Action::SeekRelative: argument = "seconds"; minimum = -3600; maximum = 3600; break;
	case Action::SeekPercent: argument = "percent"; minimum = 0; maximum = 100; break;
	case Action::VolumeRelative: argument = "percent"; minimum = -100; maximum = 100; break;
	case Action::RateRelative: argument = "step"; minimum = -4; maximum = 4; break;
	default: break;
	}
	const auto args = object.FindMember("args");
	if (!argument) {
		if (args != object.MemberEnd() && (!args->value.IsObject() || args->value.MemberCount())) {
			error = "This action takes no arguments."; return false;
		}
		return true;
	}
	if (args == object.MemberEnd() || !Fields(args->value, {argument}, error)) {
		if (error.empty()) { error = "Missing action arguments."; }
		return false;
	}
	const auto value = args->value.FindMember(argument);
	if (value == args->value.MemberEnd() || !value->value.IsNumber()) { error = "Missing numeric argument."; return false; }
	binding.value = value->value.GetDouble();
	if (!std::isfinite(binding.value) || binding.value < minimum || binding.value > maximum) {
		error = "Action argument outside its supported range."; return false;
	}
	if (binding.action != Action::SeekPercent && binding.value == 0) { error = "Action step cannot be zero."; return false; }
	if (binding.action == Action::VolumeRelative && std::floor(binding.value) != binding.value) {
		error = "Volume step must be an integer."; return false;
	}
	return true;
}

} // namespace

const Binding* ShortcutProfile::Find(std::uint16_t key, std::uint8_t modifiers) const
{
	for (const auto& binding : bindings) {
		if (binding.key == key && binding.modifiers == modifiers) { return &binding; }
	}
	return nullptr;
}

bool ParseProfile(std::string_view json, ShortcutProfile& output, std::string& error)
{
	error.clear();
	if (json.empty() || json.size() > MaxProfileBytes) { error = "Profile is empty or exceeds 128 KiB."; return false; }
	if (json.find('\0') != std::string_view::npos) { error = "Profile contains a NUL byte."; return false; }
	if (json.substr(0, 3) == "\xef\xbb\xbf") { json.remove_prefix(3); }
	rapidjson::Document document;
	document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(json.data(), json.size());
	if (document.HasParseError()) {
		error = std::string("Invalid JSON: ") + rapidjson::GetParseError_En(document.GetParseError()); return false;
	}
	if (!Fields(document, {"schemaVersion", "id", "displayName", "profileVersion", "reference", "bindings"}, error)) { return false; }
	const auto version = document.FindMember("schemaVersion");
	if (version == document.MemberEnd() || !version->value.IsInt() || version->value.GetInt() != 1) {
		error = "Unsupported profile schemaVersion (expected 1)."; return false;
	}
	ShortcutProfile candidate;
	if (!String(document, "id", candidate.id) || !String(document, "displayName", candidate.displayName)) {
		error = "Profile id and displayName are required."; return false;
	}
	if (candidate.id.size() > 64 || !std::all_of(candidate.id.begin(), candidate.id.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
	})) { error = "Profile id must use lowercase letters, digits, '-' or '_'."; return false; }
	if (candidate.displayName.size() > 256) { error = "displayName exceeds 256 UTF-8 bytes."; return false; }
	const auto reference = document.FindMember("reference");
	if (reference != document.MemberEnd() && (!reference->value.IsString() || reference->value.GetStringLength() > 4096)) {
		error = "reference must be a string of at most 4096 bytes."; return false;
	}
	const auto profileVersion = document.FindMember("profileVersion");
	if (profileVersion != document.MemberEnd() && (!profileVersion->value.IsUint() || profileVersion->value.GetUint() == 0)) {
		error = "profileVersion must be a positive integer."; return false;
	}
	const auto bindings = document.FindMember("bindings");
	if (bindings == document.MemberEnd() || !bindings->value.IsArray() || bindings->value.Size() > 512) {
		error = "bindings must be an array with at most 512 entries."; return false;
	}
	std::set<std::pair<std::uint16_t, std::uint8_t>> chords;
	for (const auto& entry : bindings->value.GetArray()) {
		if (!Fields(entry, {"key", "modifiers", "trigger", "action", "args", "repeat"}, error)) { return false; }
		Binding binding;
		std::string name;
		if (!String(entry, "key", name) || !Key(name, binding.key)) { error = "Unknown key name."; return false; }
		const auto modifiers = entry.FindMember("modifiers");
		if (modifiers != entry.MemberEnd()) {
			if (!modifiers->value.IsArray()) { error = "modifiers must be an array."; return false; }
			for (const auto& modifier : modifiers->value.GetArray()) {
				if (!modifier.IsString()) { error = "Invalid modifier."; return false; }
				const std::string_view mod(modifier.GetString(), modifier.GetStringLength());
				const auto flag = mod == "Shift" ? Shift : mod == "Control" ? Control : mod == "Alt" ? Alt : 0;
				if (!flag || (binding.modifiers & flag)) { error = "Unknown or repeated modifier."; return false; }
				binding.modifiers |= static_cast<std::uint8_t>(flag);
			}
		}
		const auto trigger = entry.FindMember("trigger");
		if (trigger != entry.MemberEnd() && (!trigger->value.IsString()
			|| std::string_view(trigger->value.GetString(), trigger->value.GetStringLength()) != "press")) {
			error = "This preview supports only the press trigger."; return false;
		}
		const auto repeat = entry.FindMember("repeat");
		if (repeat != entry.MemberEnd()) {
			if (!repeat->value.IsBool()) { error = "repeat must be a boolean."; return false; }
			binding.repeat = repeat->value.GetBool();
		}
		if (!ReadAction(entry, binding, error)) { return false; }
		if (!chords.emplace(binding.key, binding.modifiers).second) { error = "Duplicate key binding: " + name; return false; }
		candidate.bindings.push_back(binding);
	}
	output = std::move(candidate);
	return true;
}

bool LoadBuiltin(std::string_view id, ShortcutProfile& output, std::string& error)
{
	if (id == "native") { output = {}; output.id = "native"; output.displayName = "MPC-BE"; error.clear(); return true; }
	if (id == "youtube") { return ParseProfile(BuiltinYoutube, output, error); }
	if (id == "bilibili") { return ParseProfile(BuiltinBilibili, output, error); }
	error = "Unknown built-in profile.";
	return false;
}

InputDecision RouteKey(const ShortcutProfile& profile, std::uint16_t key,
	std::uint8_t modifiers, bool repeat, bool playerHasFocus)
{
	if (!playerHasFocus) { return {}; }
	const auto* binding = profile.Find(key, modifiers);
	if (!binding) { return {}; }
	return {true, repeat && !binding->repeat ? nullptr : binding};
}

std::int64_t RelativeTarget(std::int64_t position, std::int64_t duration, double seconds)
{
	if (duration <= 0) { return std::max<std::int64_t>(0, position); }
	if (!std::isfinite(seconds)) { return std::clamp<std::int64_t>(position, 0, duration); }
	const long double target = static_cast<long double>(position) + static_cast<long double>(seconds) * 10000000;
	if (target <= 0) { return 0; }
	if (target >= static_cast<long double>(duration)) { return duration; }
	return static_cast<std::int64_t>(target);
}

std::int64_t PercentTarget(std::int64_t duration, double percent)
{
	if (duration <= 0 || !std::isfinite(percent)) { return 0; }
	if (percent <= 0) { return 0; }
	if (percent >= 100) { return duration; }
	const long double target = static_cast<long double>(duration) * std::clamp(percent, 0.0, 100.0) / 100;
	if (target >= static_cast<long double>(duration)) { return duration; }
	return static_cast<std::int64_t>(target);
}

} // namespace Bridge
