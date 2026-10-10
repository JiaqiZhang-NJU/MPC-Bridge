// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Bridge {

// Bind a public referrer origin to exact media URLs, rather than attaching it to all HTTP traffic.
class MediaRequestHeaders {
	std::mutex m_mutex;
	std::map<std::wstring, std::wstring> m_headers;
public:
	bool SetReferer(const std::vector<std::wstring>& urls, std::wstring_view referer)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_headers.clear();
		if (referer.empty()) { return true; }
		if (referer.size() > 4096 || urls.size() > 8) { return false; }
		for (const auto c : referer) {
			if (c <= 0x20 || c == 0x7f || c == L'"' || c == L'\\') { return false; }
		}
		const auto start = referer.substr(0, 8) == L"https://" ? 8u : referer.substr(0, 7) == L"http://" ? 7u : 0u;
		if (!start) { return false; }
		const auto end = referer.find_first_of(L"/?#", start);
		const auto authority = referer.substr(start, end == std::wstring_view::npos ? end : end - start);
		if (authority.empty() || authority.find(L'@') != std::wstring_view::npos) { return false; }
		// Omit paths, fragments and query tokens from the referrer sent to the CDN.
		const std::wstring header = L"Referer: " + std::wstring(referer.substr(0, start)) + std::wstring(authority) + L"/\r\n";
		for (const auto& url : urls) {
			if (url.rfind(L"https://", 0) == 0 || url.rfind(L"http://", 0) == 0) { m_headers[url] = header; }
		}
		return true;
	}
	std::wstring ForUrl(std::wstring_view url)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto found = m_headers.find(std::wstring(url));
		return found == m_headers.end() ? std::wstring() : found->second;
	}
	std::wstring RefererForUrl(std::wstring_view url)
	{
		const auto header = ForUrl(url);
		return header.empty() ? std::wstring() : header.substr(9, header.size() - 11);
	}
};

inline MediaRequestHeaders mediaRequestHeaders;
}
