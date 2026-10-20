// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include "FullscreenWnd.h"
#include "RateControl.h"
#include "DSUtil/text.h"
#include <cmath>

namespace {
bool ChineseUI()
{
	const auto language = AfxGetAppSettings().iCurrentLanguage;
	return language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_SIMPLIFIED)
		|| language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_TRADITIONAL);
}
constexpr const wchar_t* ProfileIds[] = {L"native", L"bilibili", L"youtube", L"custom"};
}

void CMainFrame::AddBridgeProfileMenu(CMenu& menu)
{
	if (menu.GetMenuState(ID_FILE_LOAD_SUBTITLE, MF_BYCOMMAND) != UINT(-1)
		&& menu.GetMenuState(ID_BRIDGE_DUAL_SUBTITLES, MF_BYCOMMAND) == UINT(-1)) {
		menu.AppendMenuW(MF_STRING, ID_BRIDGE_DUAL_SUBTITLES, ChineseUI() ? L"双字幕对照…" : L"Dual subtitles...");
	}
	if (menu.GetMenuState(ID_FILE_OPENFILEURL, MF_BYCOMMAND) != UINT(-1)
		&& menu.GetMenuState(ID_BRIDGE_OPEN_ONLINE, MF_BYCOMMAND) == UINT(-1)) {
		const int count = menu.GetMenuItemCount();
		for (int position = 0; position < count; ++position) {
			if (menu.GetMenuItemID(position) == ID_FILE_OPENFILEURL) {
				menu.InsertMenuW(position + 1, MF_BYPOSITION | MF_STRING, ID_BRIDGE_OPEN_ONLINE,
					ChineseUI() ? L"打开在线视频…" : L"Open online video...");
				break;
			}
		}
	}
	if (menu.GetMenuState(ID_VIEW_OPTIONS, MF_BYCOMMAND) == UINT(-1)
		|| menu.GetMenuState(ID_BRIDGE_PROFILE_NATIVE, MF_BYCOMMAND) != UINT(-1)) {
		return;
	}
	CMenu profiles;
	if (!profiles.CreatePopupMenu()) { return; }
	profiles.AppendMenuW(MF_STRING, ID_BRIDGE_PROFILE_NATIVE, ChineseUI() ? L"MPC-BE 原生" : L"MPC-BE native");
	profiles.AppendMenuW(MF_STRING, ID_BRIDGE_PROFILE_BILIBILI, ChineseUI() ? L"Bilibili（预览）" : L"Bilibili (preview)");
	profiles.AppendMenuW(MF_STRING, ID_BRIDGE_PROFILE_YOUTUBE, L"YouTube");
	profiles.AppendMenuW(MF_STRING, ID_BRIDGE_PROFILE_CUSTOM, ChineseUI() ? L"自定义" : L"Custom");
	if (menu.AppendMenuW(MF_POPUP, reinterpret_cast<UINT_PTR>(profiles.GetSafeHmenu()),
		ChineseUI() ? L"快捷键预设" : L"Shortcut preset")) {
		profiles.Detach(); // parent menu now owns the submenu
	}
}

void CMainFrame::OnBridgeProfileSelected(UINT id)
{
	if (id < ID_BRIDGE_PROFILE_NATIVE || id > ID_BRIDGE_PROFILE_CUSTOM) { return; }
	CancelBridgeHold();
	auto& settings = AfxGetAppSettings();
	CString error;
	if (!settings.SetBridgeProfile(ProfileIds[id - ID_BRIDGE_PROFILE_NATIVE], settings.strBridgeCustomProfile, error)) {
		AfxMessageBox(error, MB_ICONWARNING);
		return;
	}
	AfxGetProfile().WriteString(L"MPC-Bridge", L"ShortcutProfile", settings.strBridgeProfileId);
	CString message = ChineseUI() ? L"快捷键：" : L"Shortcuts: ";
	message += UTF8ToWStr(settings.bridgeShortcutProfile.displayName.c_str());
	m_OSD.DisplayMessage(OSD_TOPLEFT, message, 2500);
}

void CMainFrame::OnUpdateBridgeProfile(CCmdUI* command)
{
	const auto& settings = AfxGetAppSettings();
	command->Enable(command->m_nID != ID_BRIDGE_PROFILE_CUSTOM || !settings.strBridgeCustomProfile.IsEmpty());
	command->SetRadio(settings.strBridgeProfileId == ProfileIds[command->m_nID - ID_BRIDGE_PROFILE_NATIVE]);
}

bool CMainFrame::HandleBridgeShortcut(MSG* message)
{
	const bool down = message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN;
	const bool up = message->message == WM_KEYUP || message->message == WM_SYSKEYUP;
	if (!down && !up) { return false; }
	const auto key = static_cast<std::uint16_t>(message->wParam);
	const bool repeat = (message->lParam & (1LL << 30)) != 0;
	if (up && m_bridgeHold.Owns(key)) {
		const auto tap = m_bridgeHold.Release(GetTickCount64());
		CancelBridgeHold();
		if (tap && !m_bInMenu && !m_bInOptions && IsBridgePlaybackWindow(message->hwnd)) {
			ExecuteBridgeBinding(*tap);
		}
		return true;
	}
	if (down && m_bridgeHold.Owns(key)) {
		if (repeat) { return true; }
		CancelBridgeHold(); // A fresh press recovers from a missed release after losing focus.
		m_bridgeHold.Release(GetTickCount64());
	}
	if (!down || m_bInMenu || m_bInOptions) { return false; }
	// Only the playback surface owns these keys. Buttons, sliders and edits retain normal input.
	const bool playbackFocus = IsBridgePlaybackWindow(message->hwnd);
	std::uint8_t modifiers = 0;
	if (GetKeyState(VK_SHIFT) < 0) { modifiers |= Bridge::Shift; }
	if (GetKeyState(VK_CONTROL) < 0) { modifiers |= Bridge::Control; }
	if (GetKeyState(VK_MENU) < 0) { modifiers |= Bridge::Alt; }
	const auto& profile = AfxGetAppSettings().bridgeShortcutProfile;
	const auto decision = Bridge::RouteKey(profile, static_cast<std::uint16_t>(message->wParam), modifiers,
		(message->lParam & (1LL << 30)) != 0, playbackFocus);
	if (!decision.consume) { return false; }
	if (!decision.binding) { return true; }
	const auto& binding = *decision.binding;
	if (binding.hold) {
		CancelBridgeHold();
		CancelLeftLongPressSpeed(true);
		m_bridgeHold.Begin(binding, GetTickCount64());
		if (!SetTimer(TIMER_BRIDGE_HOLD, 25, nullptr)) { m_bridgeHold.Cancel(); }
	} else {
		ExecuteBridgeBinding(binding);
	}
	return true;
}

bool CMainFrame::IsBridgePlaybackWindow(HWND window) const
{
	return window == m_hWnd || window == m_wndView.GetSafeHwnd()
		|| (m_pFullscreenWnd && window == m_pFullscreenWnd->GetSafeHwnd());
}

void CMainFrame::CancelBridgeHold()
{
	KillTimer(TIMER_BRIDGE_HOLD);
	m_bridgeHold.Cancel();
	if (m_bridgeHoldRateActive) {
		m_bridgeHoldRateActive = false;
		if (m_pMS && SUCCEEDED(m_pMS->SetRate(m_bridgeHoldPreviousRate))) {
			m_PlaybackRate = m_bridgeHoldPreviousRate;
		} else if (m_eMediaLoadState == MLS_LOADED) {
			m_OSD.DisplayMessage(OSD_TOPRIGHT, ChineseUI() ? L"未能恢复播放速度。" : L"Could not restore playback speed.");
		}
	}
}

void CMainFrame::OnBridgeHoldTimer()
{
	const auto focus = ::GetFocus();
	if (m_bInMenu || m_bInOptions || (focus && !IsBridgePlaybackWindow(focus))
		|| m_eMediaLoadState != MLS_LOADED || GetPlaybackMode() != PM_FILE
		|| (m_bridgeHoldRateActive && GetMediaState() != State_Running)) {
		CancelBridgeHold();
		return;
	}
	if (!m_bridgeHold.Tick(GetTickCount64())) { return; }
	const bool live = m_YtDlp.GetFormatsCount() ? m_YtDlp.mIsLive : m_bIsLiveOnline;
	if (live || GetMediaState() != State_Running || !m_pMS) {
		CancelBridgeHold();
		m_OSD.DisplayMessage(OSD_TOPRIGHT, ChineseUI() ? L"此时无法临时加速，请先播放视频。" : L"Temporary speed is unavailable. Start playback first.");
		return;
	}
	double previous = 1.0;
	const double rate = m_bridgeHold.HeldBinding().hold->rate;
	if (FAILED(m_pMS->GetRate(&previous)) || !std::isfinite(previous) || previous <= 0
		|| FAILED(m_pMS->SetRate(rate))) {
		CancelBridgeHold();
		m_OSD.DisplayMessage(OSD_TOPRIGHT, ChineseUI() ? L"此视频不支持临时加速。" : L"Temporary speed is unsupported for this video.");
		return;
	}
	m_bridgeHoldPreviousRate = previous;
	m_bridgeHoldRateActive = true;
	m_PlaybackRate = rate;
	CString text;
	text.Format(ChineseUI() ? L"临时 %s 倍速，松开恢复" : L"Temporary %sx speed; release to restore", Rate2String(rate).GetString());
	m_OSD.DisplayMessage(OSD_TOPRIGHT, text, 3000);
}

void CMainFrame::ShowBridgeOnlineQuality()
{
	const auto selected = m_YtDlp.GetVFormat();
	if (!selected || selected->height <= 0) { return; }
	CString available;
	for (const int height : m_YtDlp.GetVideoHeights()) {
		CString item;
		item.Format(L"%dp", height);
		if (!available.IsEmpty()) { available += L" / "; }
		available += item;
	}
	CString text;
	text.Format(ChineseUI() ? L"当前 %dp；可用 %s" : L"Current %dp; available %s", selected->height, available.GetString());
	if (selected->height < AfxGetAppSettings().iYdlMaxHeight) {
		text += ChineseUI() ? L"（已使用可用格式）" : L" (using an available format)";
	}
	m_wndInfoBar.SetLine(ChineseUI() ? L"在线清晰度" : L"Online quality", text);
	m_OSD.DisplayMessage(OSD_TOPLEFT, text, 6000);
}

void CMainFrame::ExecuteBridgeBinding(const Bridge::Binding& binding)
{
	using Bridge::Action;
	if (binding.action == Action::FullscreenToggle) { OnViewFullscreen(); return; }
	if (binding.action == Action::FullscreenExit) {
		if (m_bFullScreen || IsD3DFullScreenMode()) { OnViewFullscreen(); }
		return;
	}
	if (binding.action == Action::None) { return; }
	if (m_eMediaLoadState != MLS_LOADED || GetPlaybackMode() != PM_FILE) { return; }
	// The native flag may be latched before a DASH source reports its duration.
	// For extracted videos, use the extractor's explicit live status instead.
	const bool liveSource = m_YtDlp.GetFormatsCount() ? m_YtDlp.mIsLive : m_bIsLiveOnline;
	switch (binding.action) {
	case Action::PlayToggle: SendMessageW(WM_COMMAND, ID_PLAY_PLAYPAUSE); break;
	case Action::MuteToggle: SendMessageW(WM_COMMAND, ID_VOLUME_MUTE); break;
	case Action::SubtitlesToggle: SendMessageW(WM_COMMAND, ID_STREAM_SUB_ONOFF); break;
	case Action::PlaylistNext: SendMessageW(WM_COMMAND, ID_NAVIGATE_SKIPFORWARD); break;
	case Action::PlaylistPrevious: SendMessageW(WM_COMMAND, ID_NAVIGATE_SKIPBACK); break;
	case Action::SeekRelative:
	case Action::SeekPercent: {
		REFERENCE_TIME duration = 0, position = 0;
		DWORD capabilities = 0;
		if (liveSource || !m_pMS || FAILED(m_pMS->GetDuration(&duration)) || duration <= 0
			|| FAILED(m_pMS->GetCurrentPosition(&position)) || FAILED(m_pMS->GetCapabilities(&capabilities))
			|| !(capabilities & AM_SEEKING_CanSeekAbsolute)) {
			m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseUI() ? L"此视频当前不支持跳转。" : L"Seeking is unavailable for this video.");
			break;
		}
		const auto target = binding.action == Action::SeekRelative
			? Bridge::RelativeTarget(position, duration, binding.value)
			: Bridge::PercentTarget(duration, binding.value);
		if (!ValidateSeek(target, duration)) {
			m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseUI() ? L"视频仍在缓冲，请稍后再跳转。" : L"Video is buffering. Try seeking again shortly.");
			break;
		}
		SeekTo(target);
		break;
	}
	case Action::VolumeRelative: {
		const auto position = std::clamp(m_wndToolBar.m_volctrl.GetPos() + static_cast<int>(binding.value), 0, 100);
		m_wndToolBar.SetVolume(position);
		OnPlayVolume(ID_VOLUME_UP);
		break;
	}
	case Action::RateRelative:
		CancelBridgeHold();
		if (!liveSource && m_pMS) {
			const double rate = std::clamp(m_PlaybackRate + binding.value, 0.25, 4.0);
			if (SUCCEEDED(m_pMS->SetRate(rate))) {
				m_PlaybackRate = rate;
				CString text;
				text.Format(ResStr(IDS_OSD_SPEED), Rate2String(rate));
				m_OSD.DisplayMessage(OSD_TOPRIGHT, text);
			}
		}
		break;
	case Action::FrameForward:
	case Action::FrameBackward:
		if (!m_bAudioOnly && GetMediaState() == State_Paused) {
			const bool forward = binding.action == Action::FrameForward;
			if (m_pMS && (m_pMS->IsFormatSupported(&TIME_FORMAT_FRAME) == S_OK
				|| forward || (m_pFS && m_pFS->CanStep(0, nullptr) == S_OK))) {
				OnPlayFrameStep(forward ? ID_PLAY_FRAMESTEP : ID_PLAY_FRAMESTEP_BACK);
			}
		}
		break;
	default: break;
	}
}
