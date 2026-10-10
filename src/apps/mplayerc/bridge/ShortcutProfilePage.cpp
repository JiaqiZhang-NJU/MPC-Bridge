// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "PPageAccelTbl.h"
#include "DSUtil/text.h"

namespace {
bool ChineseUI()
{
	const auto language = AfxGetAppSettings().iCurrentLanguage;
	return language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_SIMPLIFIED)
		|| language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_TRADITIONAL);
}
}

void CPPageAccelTbl::OnBridgeProfileChanged()
{
	SetModified();
}

void CPPageAccelTbl::OnBridgeImport()
{
	const bool chinese = ChineseUI();
	CFileDialog dialog(TRUE, L"json", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"Shortcut profiles (*.json)|*.json||", this);
	if (dialog.DoModal() != IDOK) { return; }
	try {
		CFile file(dialog.GetPathName(), CFile::modeRead | CFile::typeBinary);
		const auto length = file.GetLength();
		if (length == 0 || length > Bridge::MaxProfileBytes) {
			AfxMessageBox(chinese ? L"预设文件为空或超过 128 KiB。" : L"Profile is empty or exceeds 128 KiB.", MB_ICONWARNING);
			return;
		}
		std::string json(static_cast<size_t>(length), '\0');
		if (file.Read(json.data(), static_cast<UINT>(length)) != length) {
			AfxMessageBox(chinese ? L"无法完整读取预设文件。" : L"Could not read the complete profile.", MB_ICONWARNING);
			return;
		}
		Bridge::ShortcutProfile candidate;
		std::string error;
		if (!Bridge::ParseProfile(json, candidate, error)) {
			AfxMessageBox(UTF8ToWStr(error.c_str()), MB_ICONWARNING);
			return;
		}
		m_BridgeCustomJson = UTF8ToWStr(json.c_str());
		m_BridgeProfile.SetCurSel(3);
		SetModified();
	} catch (CFileException* exception) {
		wchar_t message[512] = {};
		exception->GetErrorMessage(message, _countof(message));
		exception->Delete();
		AfxMessageBox(message, MB_ICONWARNING);
	}
}

void CPPageAccelTbl::OnBridgeHelp()
{
	const bool chinese = ChineseUI();
	static constexpr const char* ids[] = {"native", "bilibili", "youtube", "custom"};
	const auto selected = m_BridgeProfile.GetCurSel();
	if (selected < 0 || selected > 3) { return; }
	if (selected == 0) {
		CString text = chinese ? L"下方列表是你的 MPC-BE 原生键位。平台预设优先处理其声明的按键，其余按键沿用原生设置。\n\n设置页中的选择在应用后生效；菜单中的“快捷键预设”可立即切换。"
			: L"The list shows your native MPC-BE bindings. Presets override only their declared keys. Other keys retain native behavior.\n\nApply settings to activate a selection, or switch immediately with the Shortcut preset menu.";
		if (!AfxGetAppSettings().strBridgeProfileError.IsEmpty()) {
			text += L"\n\n" + AfxGetAppSettings().strBridgeProfileError;
		}
		AfxMessageBox(text, MB_ICONINFORMATION);
		return;
	}
	Bridge::ShortcutProfile profile;
	std::string error;
	const CStringA utf8 = WStrToUTF8(m_BridgeCustomJson);
	const bool loaded = selected == 3
		? Bridge::ParseProfile(std::string_view(utf8.GetString(), utf8.GetLength()), profile, error)
		: Bridge::LoadBuiltin(ids[selected], profile, error);
	if (!loaded) { AfxMessageBox(UTF8ToWStr(error.c_str()), MB_ICONWARNING); return; }
	CString text = UTF8ToWStr(profile.displayName.c_str()) + L"\n\n";
	for (const auto& binding : profile.bindings) {
		ACCEL accelerator = {static_cast<BYTE>(FVIRTKEY | binding.modifiers), binding.key, 0};
		CString action;
		using Bridge::Action;
		switch (binding.action) {
		case Action::None: action = chinese ? L"禁用" : L"Disabled"; break;
		case Action::PlayToggle: action = chinese ? L"播放 / 暂停" : L"Play / pause"; break;
		case Action::FullscreenToggle: action = chinese ? L"切换全屏" : L"Toggle fullscreen"; break;
		case Action::FullscreenExit: action = chinese ? L"退出全屏" : L"Exit fullscreen"; break;
		case Action::MuteToggle: action = chinese ? L"静音 / 取消静音" : L"Mute / unmute"; break;
		case Action::SubtitlesToggle: action = chinese ? L"显示 / 隐藏字幕" : L"Toggle subtitles"; break;
		case Action::SeekRelative: action.Format(chinese ? L"跳转 %g 秒" : L"Seek %g seconds", binding.value); break;
		case Action::SeekPercent: action.Format(chinese ? L"跳到 %g%%" : L"Seek to %g%%", binding.value); break;
		case Action::VolumeRelative: action.Format(chinese ? L"音量 %+g%%" : L"Volume %+g%%", binding.value); break;
		case Action::RateRelative: action.Format(chinese ? L"速度 %+gx" : L"Speed %+gx", binding.value); break;
		case Action::FrameForward: action = chinese ? L"下一帧（暂停时）" : L"Next frame (paused)"; break;
		case Action::FrameBackward: action = chinese ? L"上一帧（暂停时）" : L"Previous frame (paused)"; break;
		case Action::PlaylistNext: action = chinese ? L"队列下一项" : L"Next queued item"; break;
		case Action::PlaylistPrevious: action = chinese ? L"队列上一项" : L"Previous queued item"; break;
		}
		text += MakeAccelShortcutLabel(accelerator) + L"  —  " + action + L"\n";
	}
	text += chinese ? L"\n下方列表保留原生键位。Bilibili 为预览预设，长按行为尚未加入。"
		: L"\nThe list retains native bindings. Bilibili is a preview; hold gestures are not implemented yet.";
	AfxMessageBox(text, MB_ICONINFORMATION);
}
