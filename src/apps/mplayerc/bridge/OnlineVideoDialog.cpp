// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include "OpenDlg.h"
#include "DSUtil/FileHandle.h"
#include "DSUtil/text.h"
#include "OnlineUrl.h"

namespace {
bool ChineseUI()
{
	const auto language = AfxGetAppSettings().iCurrentLanguage;
	return language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_SIMPLIFIED)
		|| language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_TRADITIONAL);
}

class OnlineVideoDialog final : public COpenDlg {
	CString m_parserPath;
	CStatic m_parserStatus;
public:
	explicit OnlineVideoDialog(CWnd* parent) : COpenDlg(parent), m_parserPath(AfxGetAppSettings().strYdlExePath) {}
	CString ParserPath() const { return GetFullExePath(m_parserPath, true); }
protected:
	BOOL OnInitDialog() override
	{
		COpenDlg::OnInitDialog();
		SetWindowTextW(ChineseUI() ? L"打开在线视频" : L"Open online video");
		GetDlgItem(IDC_STATIC)->SetWindowTextW(ChineseUI()
			? L"粘贴 Bilibili 或 YouTube 视频网页链接，点击播放。清晰度沿用在线服务设置。"
			: L"Paste a Bilibili or YouTube video-page URL and click Play. Quality follows Online services settings.");
		GetDlgItem(IDC_STATIC2)->SetWindowTextW(ChineseUI() ? L"视频链接：" : L"Video URL:");
		GetDlgItem(IDC_BUTTON1)->SetWindowTextW(ChineseUI() ? L"解析器…" : L"Parser...");
		GetDlgItem(IDOK)->SetWindowTextW(ChineseUI() ? L"播放" : L"Play");
		for (const auto id : {IDC_STATIC1, IDC_COMBO2, IDC_BUTTON2, IDC_CHECK1, IDC_CHECK2}) {
			GetDlgItem(id)->ShowWindow(SW_HIDE);
		}
		CRect statusRect(10, 61, 248, 88);
		MapDialogRect(statusRect);
		m_parserStatus.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, statusRect, this);
		m_parserStatus.SetFont(GetFont());
		UpdateParserStatus();
		return TRUE;
	}
	void UpdateParserStatus()
	{
		CString text;
		if (ParserPath().IsEmpty()) {
			text = ChineseUI() ? L"未找到 yt-dlp.exe。点击“解析器…”选择本机文件。"
				: L"yt-dlp.exe was not found. Click Parser... to select your local executable.";
		} else {
			text = ChineseUI() ? L"解析器：" : L"Parser: ";
			text += GetFileName(ParserPath());
		}
		m_parserStatus.SetWindowTextW(text);
	}
	afx_msg void OnSelectParser()
	{
		CFileDialog dialog(TRUE, L"exe", DEFAULT_YTDLP_EXE, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
			L"yt-dlp executable (*.exe)|*.exe||", this);
		if (dialog.DoModal() == IDOK) { m_parserPath = dialog.GetPathName(); UpdateParserStatus(); }
	}
	afx_msg void OnPlay()
	{
		CString input;
		m_mrucombo.GetWindowTextW(input);
		const CStringA utf8 = WStrToUTF8(input);
		std::string url, error;
		if (!Bridge::NormalizeOnlineUrl(std::string_view(utf8.GetString(), utf8.GetLength()), url, error)) {
			CString message = UTF8ToWStr(error.c_str());
			if (ChineseUI()) { message = L"请粘贴完整的 Bilibili 或 YouTube 视频网页链接；暂不支持 b23.tv 分享短链。"; }
			AfxMessageBox(message, MB_ICONWARNING);
			return;
		}
		if (ParserPath().IsEmpty()) {
			AfxMessageBox(ChineseUI() ? L"请先点击“解析器…”选择 yt-dlp.exe。" : L"Select yt-dlp.exe with Parser... first.", MB_ICONWARNING);
			return;
		}
		m_fns.clear();
		m_fns.emplace_back(UTF8ToWStr(url.c_str()));
		CDialog::OnOK();
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(OnlineVideoDialog, COpenDlg)
	ON_BN_CLICKED(IDC_BUTTON1, OnSelectParser)
	ON_BN_CLICKED(IDOK, OnPlay)
END_MESSAGE_MAP()
}

void CMainFrame::OnBridgeOpenOnline()
{
	if (m_eMediaLoadState == MLS_LOADING || !CanShowDialog()) { return; }
	OnlineVideoDialog dialog(this);
	if (dialog.DoModal() != IDOK || dialog.m_fns.empty()) { return; }
	auto& settings = AfxGetAppSettings();
	settings.bYdlEnable = true;
	settings.strYdlExePath = dialog.ParserPath();
	SendMessageW(WM_COMMAND, ID_FILE_CLOSEMEDIA);
	m_wndPlaylistBar.Open(dialog.m_fns, false);
	OpenCurPlaylistItem();
}
