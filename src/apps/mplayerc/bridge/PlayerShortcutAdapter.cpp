// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include "FullscreenWnd.h"
#include "RateControl.h"
#include "DSUtil/text.h"

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
	if ((message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)
		|| m_bInMenu || m_bInOptions) { return false; }
	// Only the playback surface owns these keys. Buttons, sliders and edits retain normal input.
	const bool playbackFocus = message->hwnd == m_hWnd || message->hwnd == m_wndView.GetSafeHwnd()
		|| (m_pFullscreenWnd && message->hwnd == m_pFullscreenWnd->GetSafeHwnd());
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
	using Bridge::Action;
	if (binding.action == Action::FullscreenToggle) { OnViewFullscreen(); return true; }
	if (binding.action == Action::FullscreenExit) {
		if (m_bFullScreen || IsD3DFullScreenMode()) { OnViewFullscreen(); }
		return true;
	}
	if (binding.action == Action::None) { return true; }
	if (m_eMediaLoadState != MLS_LOADED || GetPlaybackMode() != PM_FILE) { return true; }
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
	return true;
}
