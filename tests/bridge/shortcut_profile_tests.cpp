// SPDX-License-Identifier: GPL-3.0-or-later
#include "apps/mplayerc/bridge/ShortcutProfile.h"
#include "apps/mplayerc/bridge/HoldGesture.h"
#include "apps/mplayerc/bridge/OnlineUrl.h"
#include "DSUtil/MediaRequestHeaders.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;
void Check(bool condition, const char* name)
{
	if (!condition) { throw std::runtime_error(name); }
	++checks;
}

std::string Profile(std::string_view bindings)
{
	return "{\"schemaVersion\":1,\"id\":\"test\",\"displayName\":\"Test\",\"bindings\":["
		+ std::string(bindings) + "]}";
}
}

int main()
{
	try {
		using namespace Bridge;
		ShortcutProfile profile;
		std::string error;
		Check(LoadBuiltin("youtube", profile, error), "YouTube profile must load");
		Check(profile.Find('J', 0)->value == -10 && profile.Find(0x25, 0)->value == -5, "distinct seek distances");
		Check(profile.Find('5', 0)->action == Action::SeekPercent && profile.Find('5', 0)->value == 50, "numeric percentage");
		Check(profile.Find('K', 0)->action == profile.Find(0x20, 0)->action, "multiple keys for one action");
		Check(profile.Find(0xbe, Shift)->action == Action::RateRelative, "shifted punctuation");
		Check(!profile.Find('K', Control), "modifiers must match exactly");
		Check(!profile.Find(0x65, 0), "numpad does not silently alias number row");
		Check(!RouteKey(profile, 'K', 0, false, false).consume, "text input keeps keys");
		Check(RouteKey(profile, 'K', 0, false, true).binding != nullptr, "play toggle dispatch");
		const auto repeated = RouteKey(profile, 'K', 0, true, true);
		Check(repeated.consume && !repeated.binding, "suppressed repeat cannot fall through to native binding");
		Check(RouteKey(profile, 'L', 0, true, true).binding != nullptr, "seek can repeat");
		Check(!RouteKey(profile, 'X', 0, false, true).consume, "unmapped native command preserved");
		auto reject = [&](std::string_view input, const char* name) {
			const auto previousId = profile.id;
			Check(!ParseProfile(input, profile, error) && !error.empty() && profile.id == previousId, name);
		};
		reject(Profile("{\"key\":\"K\",\"action\":\"play.toggle\"},{\"key\":\"K\",\"action\":\"mute.toggle\"}"), "duplicate chords rejected atomically");
		reject(Profile("{\"key\":\"K\",\"action\":\"shell.exec\"}"), "arbitrary command action rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"play.toggle\",\"trigger\":\"hold\"}"), "unsupported trigger rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"play.toggle\",\"repeat\":1}"), "repeat type checked");
		reject(Profile("{\"key\":\"K\",\"modifiers\":[\"Shift\",\"Shift\"],\"action\":\"play.toggle\"}"), "duplicate modifiers rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"seek.percent\",\"args\":{\"percent\":101}}"), "out of range percentage rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"seek.relative\"}"), "missing arguments rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"play.toggle\",\"args\":{\"seconds\":5}}"), "unused arguments rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"volume.relative\",\"args\":{\"percent\":0.5}}"), "fractional volume step rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"seek.relative\",\"args\":{\"seconds\":0}}"), "zero relative step rejected");
		reject(Profile("{\"key\":\"K\",\"key\":\"L\",\"action\":\"play.toggle\"}"), "duplicate JSON fields rejected");
		reject("{\"schemaVersion\":3,\"id\":\"test\",\"displayName\":\"Test\",\"bindings\":[]}", "future schema rejected");
		reject(Profile("{\"key\":\"k\",\"action\":\"play.toggle\"}"), "unsupported key rejected");
		reject(Profile("{\"key\":\"K\",\"action\":\"play.toggle\",\"repeet\":true}"), "typo is not silently ignored");
		reject("[]", "root object required");
		reject("{", "malformed JSON rejected");
		reject(Profile("") + std::string(1, '\0') + "ignored", "NUL cannot truncate imported data");
		reject(std::string(MaxProfileBytes + 1, ' '), "oversized input rejected");
		reject(std::string("{\"schemaVersion\":1,\"id\":\"test\",\"displayName\":\"") + char(0xff) + "\",\"bindings\":[]}", "invalid UTF8 rejected");
		Check(ParseProfile(Profile("{\"key\":\"J\",\"action\":\"none\"}"), profile, error), "explicit disabled binding accepted");
		Check(ParseProfile(std::string("\xef\xbb\xbf") + Profile("{\"key\":\"J\",\"action\":\"none\"}"), profile, error), "UTF8 BOM import accepted");
		Check(RouteKey(profile, 'J', 0, false, true).consume && profile.Find('J', 0)->action == Action::None, "disabled chord masks native action");
		Check(LoadBuiltin("native", profile, error) && profile.bindings.empty(), "native mode leaves mappings intact");
		Check(LoadBuiltin("bilibili", profile, error) && profile.Find(0x26, 0)->value == 10, "Bilibili draft loads");
		Check(!LoadBuiltin("unknown", profile, error) && profile.id == "bilibili", "unknown preset preserves state");
		const auto right = *profile.Find(0x27, 0);
		Check(right.hold && right.hold->rate == 3 && right.hold->delayMs == 300 && !right.repeat, "Bilibili hold definition");
		Check(RouteKey(profile, 0x27, 0, true, true).consume && !RouteKey(profile, 0x27, 0, true, true).binding, "right repeat never seeks");
		HoldGesture gesture;
		Check(!gesture.Begin(*profile.Find(0x25, 0), 0), "ordinary key has no gesture");
		Check(gesture.Begin(right, 1000) && gesture.Owns(0x27) && !gesture.Owns(0x25), "gesture owns only its key");
		Check(!gesture.Tick(1299), "below hold threshold does not accelerate");
		const auto tap = gesture.Release(1299);
		Check(tap && tap->value == 5 && !gesture.Owns(0x27), "short release produces exactly one seek");
		Check(!gesture.Release(1300), "duplicate release does nothing");
		Check(gesture.Begin(right, 2000) && gesture.Tick(2300), "threshold begins long press");
		Check(!gesture.Tick(2301) && !gesture.Tick(9000), "hold dispatches only once");
		Check(!gesture.Release(9001), "long release never produces seek");
		gesture.Begin(right, 10000);
		Check(!gesture.Release(10300), "late key-up without timer cannot become tap");
		gesture.Begin(right, 11000); gesture.Cancel();
		Check(gesture.Owns(0x27) && !gesture.Tick(12000) && !gesture.Release(12001), "cancellation swallows repeats and release");
		gesture.Begin(right, 13000); gesture.Tick(13300); gesture.Cancel();
		Check(!gesture.Release(13301), "active hold cancellation cannot seek");
		gesture.Begin(right, 14000); gesture.Cancel();
		Check(gesture.Begin(right, 15000) && gesture.Release(15010).has_value(), "fresh press recovers lost release");
		Binding mutableBinding = right;
		gesture.Begin(mutableBinding, 16000); mutableBinding.hold.reset(); mutableBinding.value = 50;
		Check(gesture.HeldBinding().hold && gesture.Release(16010)->value == 5, "gesture snapshots binding across preset changes");
		auto holdProfile = [](const char* hold, int version = 2, bool repeat = false) {
			return "{\"schemaVersion\":" + std::to_string(version) + ",\"id\":\"test\",\"displayName\":\"Test\",\"bindings\":[{\"key\":\"Right\",\"action\":\"seek.relative\",\"args\":{\"seconds\":5},\"repeat\":" + (repeat ? "true" : "false") + ",\"hold\":" + hold + "}]}";
		};
		const char* validHold = "{\"action\":\"rate.temporary\",\"args\":{\"rate\":2.5},\"delayMs\":400}";
		reject(holdProfile(validHold, 1), "schema one cannot silently accept hold");
		reject(holdProfile(validHold, 2, true), "hold and repeat conflict rejected");
		for (const char* invalidHold : {"null", "{}", "{\"action\":\"shell.exec\",\"args\":{\"rate\":3},\"delayMs\":300}",
			"{\"action\":\"rate.temporary\",\"args\":{\"rate\":0},\"delayMs\":300}", "{\"action\":\"rate.temporary\",\"args\":{\"rate\":4.1},\"delayMs\":300}",
			"{\"action\":\"rate.temporary\",\"args\":{\"rate\":3},\"delayMs\":99}", "{\"action\":\"rate.temporary\",\"args\":{\"rate\":3},\"delayMs\":2001}",
			"{\"action\":\"rate.temporary\",\"args\":{\"rate\":3},\"delayMs\":300.5}", "{\"action\":\"rate.temporary\",\"args\":{\"rate\":3,\"step\":1},\"delayMs\":300}",
			"{\"action\":\"rate.temporary\",\"args\":{\"rate\":3},\"delayMs\":300,\"repeet\":true}", "{\"action\":\"rate.temporary\",\"args\":{\"rate\":3},\"delayMs\":300,\"delayMs\":400}"}) {
			reject(holdProfile(invalidHold), "invalid hold configuration rejected atomically");
		}
		Check(ParseProfile(holdProfile(validHold), profile, error) && profile.Find(0x27, 0)->hold->rate == 2.5, "custom schema two hold accepted");
		for (int i = 0; i < 20; ++i) {
			Check(LoadBuiltin("youtube", profile, error) && profile.bindings.size() == 28, "switching does not accumulate bindings");
			Check(LoadBuiltin("native", profile, error) && profile.bindings.empty(), "native switch clears overlay");
		}
		constexpr std::int64_t duration = 600 * 10000000LL;
		Check(PercentTarget(duration, 50) == duration / 2, "halfway seek");
		Check(PercentTarget(0, 50) == 0 && PercentTarget(-1, 50) == 0, "unknown duration");
		Check(RelativeTarget(0, duration, -10) == 0 && RelativeTarget(duration, duration, 10) == duration, "relative seeks clamp");
		Check(RelativeTarget(100000000, duration, 5) == 150000000, "seconds to media units");
		const auto maximum = std::numeric_limits<std::int64_t>::max();
		Check(PercentTarget(maximum, 100) == maximum && RelativeTarget(maximum, maximum, 10) == maximum, "seek arithmetic avoids overflow");
		Check(PercentTarget(duration, std::numeric_limits<double>::quiet_NaN()) == 0, "NaN defensive handling");
		std::cout << "PASS: " << checks << " shortcut-profile checks\n";
		std::string onlineUrl;
		Check(NormalizeOnlineUrl(" https://www.bilibili.com/video/BV123?p=2\r\n", onlineUrl, error)
			&& onlineUrl == "https://www.bilibili.com/video/BV123?p=2", "Bilibili URL and page selector retained");
		Check(NormalizeOnlineUrl("HTTPS://WWW.YOUTUBE.COM/watch?t=5&v=abc", onlineUrl, error), "case insensitive scheme and host");
		Check(NormalizeOnlineUrl("https://youtu.be/abc?t=5", onlineUrl, error), "YouTube short URL");
		Check(NormalizeOnlineUrl("https://m.youtube.com/shorts/abc", onlineUrl, error), "YouTube mobile shorts URL");
		Check(NormalizeOnlineUrl("https://www.bilibili.com/bangumi/play/ep123", onlineUrl, error), "Bilibili episode URL");
		const auto previousUrl = onlineUrl;
		for (const auto badUrl : {"file:///C:/video.mp4", "https://localhost/video/a", "https://youtube.com.evil.test/watch?v=a",
			"https://youtube.com@evil.test/watch?v=a", "https://evil.test@youtube.com/watch?v=a", "https://youtube.com:8080/watch?v=a",
			"https://youtube.com/", "https://youtube.com/@channel", "https://youtube.com/watch?list=a", "https://youtube.com/watch?v=",
			"https://youtu.be/", "https://youtube.com/shorts/?x=a", "https://bilibili.com/video/", "https://b23.tv/abc",
			"https://youtube.com/watch?v=a\" --exec bad", "https://youtube.com/watch?v=a\\bad", "https://youtu.be/a b"}) {
			Check(!NormalizeOnlineUrl(badUrl, onlineUrl, error) && onlineUrl == previousUrl && !error.empty(), "invalid online URL rejected transactionally");
		}
		Check(!NormalizeOnlineUrl(std::string(4097, 'a'), onlineUrl, error), "online URL length bounded");
		Check(!NormalizeOnlineUrl(std::string("https://youtu.be/a\0b", 21), onlineUrl, error), "online URL NUL rejected");
		std::cout << "PASS: " << checks << " total shortcut and online-URL checks\n";
		MediaRequestHeaders headers;
		const std::vector<std::wstring> streams = {L"https://cdn.example/video?token=1", L"https://cdn.example/audio?token=2"};
		Check(headers.SetReferer(streams, L"https://www.bilibili.com/video/BV1?private=token#part"), "media referrer accepted");
		Check(headers.ForUrl(streams[0]) == L"Referer: https://www.bilibili.com/\r\n", "referrer origin excludes path and tokens");
		Check(headers.RefererForUrl(streams[0]) == L"https://www.bilibili.com/", "external source filters receive a referrer value");
		Check(headers.ForUrl(streams[1]) == headers.ForUrl(streams[0]), "audio and video receive referrer");
		Check(headers.ForUrl(L"https://cdn.example/other").empty(), "headers do not leak to unrelated requests");
		Check(headers.ForUrl(L"https://cdn.example/video?token=3").empty(), "headers match the exact signed URL");
		const auto snapshot = headers.ForUrl(streams[0]);
		Check(headers.SetReferer({}, L"") && headers.ForUrl(streams[0]).empty() && !snapshot.empty(), "new session clears registry without changing captured header");
		for (const auto badReferer : {L"https://bilibili.com/\r\nCookie: secret", L"file:///private", L"https://user:pass@bilibili.com/video", L"https:///video"}) {
			Check(!headers.SetReferer(streams, badReferer) && headers.ForUrl(streams[0]).empty(), "invalid referrer cannot inject headers or retain stale state");
		}
		Check(!headers.SetReferer(std::vector<std::wstring>(9, streams[0]), L"https://bilibili.com/"), "media header registry bounded");
		std::cout << "PASS: " << checks << " total checks including media headers\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL: " << exception.what() << '\n';
		return 1;
	}
}
