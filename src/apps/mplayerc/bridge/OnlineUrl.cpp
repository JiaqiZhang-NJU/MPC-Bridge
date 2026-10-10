// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnlineUrl.h"
#include <algorithm>

namespace Bridge {
bool NormalizeOnlineUrl(std::string_view input, std::string& output, std::string& error)
{
	error.clear();
	while (!input.empty() && (input.front() == ' ' || input.front() == '\r' || input.front() == '\n' || input.front() == '\t')) { input.remove_prefix(1); }
	while (!input.empty() && (input.back() == ' ' || input.back() == '\r' || input.back() == '\n' || input.back() == '\t')) { input.remove_suffix(1); }
	if (input.empty() || input.size() > 4096) { error = "Paste a video URL of at most 4096 bytes."; return false; }
	for (const unsigned char c : input) {
		if (c <= 0x20 || c == 0x7f || c == '"' || c == '\\') {
			error = "The URL contains an unsupported character."; return false;
		}
	}
	std::string lower(input);
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
	});
	const auto separator = lower.find("://");
	if (separator == std::string::npos || (lower.substr(0, separator) != "https" && lower.substr(0, separator) != "http")) {
		error = "Use an HTTP or HTTPS video-page URL."; return false;
	}
	const auto start = separator + 3;
	const auto end = lower.find_first_of("/?#", start);
	const auto host = lower.substr(start, end == std::string::npos ? end : end - start);
	const bool youtube = host == "youtube.com" || host == "www.youtube.com" || host == "m.youtube.com";
	const bool shortYoutube = host == "youtu.be";
	const bool bilibili = host == "bilibili.com" || host == "www.bilibili.com" || host == "m.bilibili.com";
	if (!youtube && !shortYoutube && !bilibili) {
		error = "Use a full Bilibili or YouTube video-page URL (youtu.be is also supported)."; return false;
	}
	if (end == std::string::npos) { error = "The URL does not identify a video."; return false; }
	const auto suffix = lower.substr(end);
	const auto path = suffix.substr(0, suffix.find_first_of("?#"));
	const auto hasId = [&path](std::string_view prefix) {
		return path.rfind(prefix, 0) == 0 && path.size() > prefix.size() && path[prefix.size()] != '/';
	};
	bool watch = false;
	if (youtube && path == "/watch" && suffix.size() > path.size() && suffix[path.size()] == '?') {
		const auto query = suffix.substr(path.size() + 1, suffix.find('#') == std::string::npos ? std::string::npos : suffix.find('#') - path.size() - 1);
		for (std::size_t position = 0; position < query.size();) {
			const auto next = query.find('&', position);
			const auto part = query.substr(position, next == std::string::npos ? next : next - position);
			if (part.rfind("v=", 0) == 0 && part.size() > 2) { watch = true; }
			if (next == std::string::npos) { break; }
			position = next + 1;
		}
	}
	const bool video = (youtube && (watch || hasId("/shorts/")))
		|| (shortYoutube && hasId("/"))
		|| (bilibili && (hasId("/video/") || hasId("/bangumi/play/")));
	if (!video) { error = "Paste a video-page link rather than a channel or home-page link."; return false; }
	output.assign(input);
	return true;
}
}
