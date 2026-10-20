// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include "DualSubtitleProvider.h"
#include "DSUtil/FileHandle.h"
#include "DSUtil/text.h"
#include "filters/renderer/VideoRenderers/Variables.h"
#include <FilterInterfaces.h>
#include <functional>

namespace {
bool ChineseSubUI() {
    const auto language = AfxGetAppSettings().iCurrentLanguage;
    return language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_SIMPLIFIED)
        || language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_TRADITIONAL);
}
const wchar_t* Section = L"MPC-Bridge Dual Subtitles";
CString FullSubtitlePath(const std::wstring& path) {
    wchar_t full[32768];
    const DWORD length = GetFullPathNameW(path.c_str(), _countof(full), full, nullptr);
    return length && length < _countof(full) ? CString(full) : CString();
}
Bridge::DualSubtitleState ReadSubtitlePreferences() {
    Bridge::DualSubtitleState state;
    auto& profile = AfxGetProfile();
    profile.ReadInt(Section, L"Layout", state.layout, 0, 1);
    for (int i = 0; i < 2; ++i) {
        CString key; key.Format(L"FontSize%d", i);
        profile.ReadInt(Section, key, state.slots[i].fontSize, 8, 48);
        key.Format(L"Color%d", i); int color = state.slots[i].color;
        profile.ReadInt(Section, key, color, 0, 0xffffff); state.slots[i].color = color;
    }
    return state;
}
void SaveSubtitlePreferences(const Bridge::DualSubtitleState& state) {
    auto& profile = AfxGetProfile();
    profile.WriteInt(Section, L"Version", 1); profile.WriteInt(Section, L"Layout", state.layout);
    for (int i = 0; i < 2; ++i) {
        CString key; key.Format(L"FontSize%d", i); profile.WriteInt(Section, key, state.slots[i].fontSize);
        key.Format(L"Color%d", i); profile.WriteInt(Section, key, state.slots[i].color);
    }
    profile.Flush(false);
}

enum { Path0 = 600, Path1, Browse0, Browse1, Visible0, Visible1, Delay0, Delay1,
    Font0, Font1, Color0, Color1, Layout, Swap, Apply, Disable };
class DualSubtitleDialog final : public CDialog {
    std::vector<WORD> layout;
    std::vector<std::unique_ptr<CWnd>> controls;
    Bridge::DualSubtitleState state;
    std::function<bool(const Bridge::DualSubtitleState&, CString&)> apply;
    void String(const wchar_t* value) { do { layout.push_back(WORD(*value)); } while (*value++); }
    void Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, CRect rect) {
        MapDialogRect(rect);
        std::unique_ptr<CWnd> control;
        const std::wstring kind(type);
        if (kind == L"EDIT") control = std::make_unique<CEdit>();
        else if (kind == L"BUTTON") control = std::make_unique<CButton>();
        else if (kind == L"COMBOBOX") control = std::make_unique<CComboBox>();
        else control = std::make_unique<CStatic>();
        control->CreateEx(kind == L"EDIT" ? WS_EX_CLIENTEDGE : 0, type, text,
            WS_CHILD | WS_VISIBLE | style, rect, this, id);
        control->SetFont(GetFont()); controls.push_back(std::move(control));
    }
    bool Read() {
        for (int i = 0; i < 2; ++i) {
            CString text; GetDlgItemTextW(Path0 + i, text); state.slots[i].path = text.GetString();
            state.slots[i].visible = IsDlgButtonChecked(Visible0 + i) == BST_CHECKED;
            GetDlgItemTextW(Delay0 + i, text);
            wchar_t* end = nullptr; const auto delay = wcstol(text, &end, 10);
            if (text.IsEmpty() || *end || delay < -Bridge::SubtitleDelayLimit || delay > Bridge::SubtitleDelayLimit) return false;
            state.slots[i].delayMs = static_cast<int>(delay);
            GetDlgItemTextW(Font0 + i, text); const auto font = wcstol(text, &end, 10);
            if (text.IsEmpty() || *end || font < 8 || font > 48) return false;
            state.slots[i].fontSize = static_cast<int>(font);
        }
        state.layout = static_cast<CComboBox*>(GetDlgItem(Layout))->GetCurSel();
        return state.layout >= 0;
    }
    void Refresh() {
        for (int i = 0; i < 2; ++i) {
            SetDlgItemTextW(Path0 + i, state.slots[i].path.c_str());
            CheckDlgButton(Visible0 + i, state.slots[i].visible ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemInt(Delay0 + i, state.slots[i].delayMs, TRUE);
            SetDlgItemInt(Font0 + i, state.slots[i].fontSize);
            CString color; color.Format(L"#%02X%02X%02X", GetRValue(state.slots[i].color), GetGValue(state.slots[i].color), GetBValue(state.slots[i].color));
            SetDlgItemTextW(Color0 + i, color);
        }
        static_cast<CComboBox*>(GetDlgItem(Layout))->SetCurSel(state.layout);
    }
    bool Commit(bool enabled) {
        const bool cn = ChineseSubUI();
        if (!Read()) { AfxMessageBox(cn ? L"延迟应为 -600000 到 600000 毫秒，字号应为 8 到 48。" : L"Delay must be -600000 to 600000 ms; font size must be 8 to 48.", MB_ICONWARNING); return false; }
        state.enabled = enabled;
        CString error;
        if (!apply(state, error)) { AfxMessageBox(error, MB_ICONWARNING); return false; }
        SaveSubtitlePreferences(state); return true;
    }
public:
    DualSubtitleDialog(CWnd* parent, const Bridge::DualSubtitleState& initial,
        std::function<bool(const Bridge::DualSubtitleState&, CString&)> callback) : state(initial), apply(std::move(callback)) {
        layout.resize(sizeof(DLGTEMPLATE) / sizeof(WORD));
        auto dialog = reinterpret_cast<DLGTEMPLATE*>(layout.data());
        dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
        dialog->cx = 385; dialog->cy = 239;
        layout.push_back(0); layout.push_back(0);
        String(ChineseSubUI() ? L"双字幕对照" : L"Dual subtitles"); layout.push_back(9); String(L"Segoe UI");
        InitModalIndirect(reinterpret_cast<DLGTEMPLATE*>(layout.data()), parent);
    }
protected:
    BOOL OnInitDialog() override {
        CDialog::OnInitDialog(); const bool cn = ChineseSubUI();
        for (int i = 0; i < 2; ++i) {
            const int y = 10 + i * 70;
            Control(L"STATIC", i ? (cn ? L"副字幕（SRT）" : L"Secondary (SRT)") : (cn ? L"主字幕（SRT）" : L"Primary (SRT)"), 0, -1, {10,y,135,y+12});
            Control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Path0+i, {10,y+15,306,y+30});
            Control(L"BUTTON", cn ? L"选择文件…" : L"Browse...", WS_TABSTOP, Browse0+i, {312,y+14,375,y+31});
            Control(L"BUTTON", cn ? L"显示" : L"Show", BS_AUTOCHECKBOX | WS_TABSTOP, Visible0+i, {10,y+37,58,y+51});
            Control(L"STATIC", cn ? L"延迟 ms" : L"Delay ms", 0, -1, {66,y+39,109,y+51});
            Control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Delay0+i, {112,y+36,172,y+51});
            Control(L"STATIC", cn ? L"字号" : L"Size", 0, -1, {182,y+39,213,y+51});
            Control(L"EDIT", L"", ES_NUMBER | WS_TABSTOP, Font0+i, {215,y+36,248,y+51});
            Control(L"BUTTON", L"", WS_TABSTOP, Color0+i, {265,y+35,340,y+53});
        }
        Control(L"STATIC", cn ? L"布局" : L"Layout", 0, -1, {10,154,50,168});
        Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, Layout, {54,151,265,211});
        auto combo = static_cast<CComboBox*>(GetDlgItem(Layout));
        combo->AddString(cn ? L"底部双层（主上 / 副下）" : L"Bottom pair (primary above secondary)");
        combo->AddString(cn ? L"主字幕底部 / 副字幕顶部" : L"Primary bottom / secondary top");
        Control(L"BUTTON", cn ? L"交换主副" : L"Swap", WS_TABSTOP, Swap, {282,151,375,170});
        Control(L"STATIC", cn ? L"正延迟：晚出现；负延迟：早出现。字号按画面缩放。" : L"Positive delay: later. Negative: earlier. Size scales with the picture.", 0, -1, {10,179,375,194});
        Control(L"BUTTON", cn ? L"应用" : L"Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, Apply, {103,210,178,229});
        Control(L"BUTTON", cn ? L"关闭副字幕" : L"Disable secondary", WS_TABSTOP, Disable, {182,210,287,229});
        Control(L"BUTTON", cn ? L"关闭" : L"Close", WS_TABSTOP, IDCANCEL, {291,210,375,229});
        Refresh(); return TRUE;
    }
    void OnOK() override { Commit(true); }
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override {
        const int id = LOWORD(wParam);
        if (HIWORD(wParam) == BN_CLICKED) {
            if (id == Apply) { Commit(true); return TRUE; }
            if (id == Disable) { if (Commit(false)) EndDialog(IDOK); return TRUE; }
            if (id == Swap) { if (Read()) { std::swap(state.slots[0], state.slots[1]); Refresh(); } return TRUE; }
            if (id == Color0 || id == Color1) {
                const int i = id-Color0; CColorDialog dialog(state.slots[i].color, CC_FULLOPEN, this);
                if (Read() && dialog.DoModal() == IDOK) { state.slots[i].color = dialog.GetColor(); Refresh(); }
                return TRUE;
            }
            if (id == Browse0 || id == Browse1) {
                CFileDialog dialog(TRUE, L"srt", nullptr, OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY,
                    L"SubRip (*.srt)|*.srt||", this);
                if (dialog.DoModal() == IDOK) SetDlgItemTextW(Path0+id-Browse0, dialog.GetPathName());
                return TRUE;
            }
        }
        return CDialog::OnCommand(wParam, lParam);
    }
};
}

bool CMainFrame::ApplyBridgeSubtitles(const Bridge::DualSubtitleState& requested, CString& error, bool activate) {
    const bool cn = ChineseSubUI();
    if (!m_pCAP || m_pDVS || !AfxGetAppSettings().IsISRAutoLoadEnabled()
        || GetPlaybackMode() != PM_FILE || m_eMediaLoadState != MLS_LOADED) {
        error = cn ? L"请先打开视频，并在字幕设置中选择内置字幕渲染器。" : L"Open a video using the internal subtitle renderer first."; return false;
    }
    auto next = requested;
    const bool wasVisible = AfxGetAppSettings().fEnableSubtitles;
    std::shared_ptr<CSimpleTextSubtitle> parsed[2];
    for (int i = 0; i < (next.enabled ? 2 : 1); ++i) {
        if (!Bridge::ValidSubtitleSlot(next.slots[i])) { error = L"Invalid subtitle settings."; return false; }
        CString path = FullSubtitlePath(next.slots[i].path);
        if (path.IsEmpty() || path.Right(4).CompareNoCase(L".srt")) { error = cn ? L"请选择本地 SRT 字幕文件。" : L"Choose a local SRT subtitle file."; return false; }
        next.slots[i].path = path.GetString();
        if (i && path.CompareNoCase(next.slots[0].path.c_str()) == 0) { error = cn ? L"该文件已用作主字幕，请选择另一个文件。" : L"This file is already the primary subtitle. Choose another file."; return false; }
        // Parsing is transactional: failure must leave the live provider untouched.
        auto source = std::make_shared<CSimpleTextSubtitle>();
        const auto& s = AfxGetAppSettings();
        if (!source->Open(path, ExpandCodePage(s.iSubtitleDefaultCodePage), s.bSubtitleAutoDetectCodePage, L"", GetCurFileName())
            || source->m_subtitleType != Subtitle::SRT || source->IsEmpty()) {
            error.Format(cn ? L"无法读取有效 SRT 字幕：\n%s\n原字幕已保留。" : L"Cannot read a valid SRT subtitle:\n%s\nPrevious subtitles were kept.", path.GetString()); return false;
        }
        parsed[i] = std::move(source);
    }
    CComPtr<CRenderedTextSubtitle> primary = new CRenderedTextSubtitle(&m_csSubLock);
    primary->Copy(*parsed[0]); primary->SetName(L"Primary");
    // Keep a normal primary stream for existing save/copy/sync commands.
    ISubStream* actual = nullptr;
    {
        CAutoLock lock(&m_csSubLock);
        for (const auto& stream : m_pSubStreams) {
            if (auto text = dynamic_cast<CRenderedTextSubtitle*>(stream.p)) {
                if (!text->m_path.CompareNoCase(next.slots[0].path.c_str())) { actual = stream; break; }
            }
        }
        if (!actual) actual = m_pSubStreams.emplace_back(primary).p;
    }
    if (std::find(m_ExternalSubstreams.begin(), m_ExternalSubstreams.end(), actual) == m_ExternalSubstreams.end()) m_ExternalSubstreams.push_back(actual);
    if (actual != primary.p) {
        if (auto old = dynamic_cast<CRenderedTextSubtitle*>(actual)) { CAutoLock lock(&m_csSubLock); old->Copy(*parsed[0]); old->Deinit(); }
    }
    const bool previouslyActive = m_bridgeSubtitleActive;
    if (previouslyActive && !next.enabled) {
        g_bExternalSubtitleTime = false;
        if (CComQIPtr<IExFilterConfig> config = m_pCAP.p) config->Flt_SetBool("lessRedraws", true);
    }
    m_bridgeSubtitles = next;
    for (int i = 0; i < 2; ++i) if (parsed[i]) m_bridgeSubtitleSources[i] = std::move(parsed[i]);
    m_bridgeSubtitleProvider.Release(); m_bridgeSubtitleActive = false;
    if (previouslyActive || next.enabled) m_pCAP->SetSubtitleDelay(next.enabled ? 0 : next.slots[0].delayMs);
    AfxGetAppSettings().fEnableSubtitles = true;
    SetSubtitle(actual);
    m_nSelSub2 = m_iSubtitleSel;
    if (!activate && !wasVisible) {
        AfxGetAppSettings().fEnableSubtitles = false; m_iSubtitleSel = -1; UpdateSubtitle();
    }
    m_wndPlaylistBar.AddSubtitleToCurrent(next.slots[0].path.c_str());
    RepaintVideo(); SetToolBarSubtitleButton();
    return true;
}

void CMainFrame::AttachBridgeSubtitles(ISubStream* selected) {
    if (!m_pCAP) return;
    if (m_bridgeQualitySwitch.pending || !AfxGetAppSettings().fEnableSubtitles) return;
    const auto primary = dynamic_cast<CRenderedTextSubtitle*>(selected);
    if (!m_bridgeSubtitles.enabled || !primary || primary->m_subtitleType != Subtitle::SRT || primary->m_path.IsEmpty()) {
        if (m_bridgeSubtitleActive) m_pCAP->SetSubtitleDelay(m_bridgeSubtitles.slots[0].delayMs);
        if (m_bridgeSubtitleActive) {
            g_bExternalSubtitleTime = false;
            if (CComQIPtr<IExFilterConfig> config = m_pCAP.p) config->Flt_SetBool("lessRedraws", true);
        }
        m_bridgeSubtitleActive = false;
        if (selected && m_bridgeSubtitles.enabled) {
            m_bridgeSubtitles.enabled = false;
            m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"当前轨道不支持双字幕，已恢复普通字幕。" : L"This track does not support dual subtitles. Using normal subtitles.", 4000);
        }
        return;
    }
    if (primary->m_path.CompareNoCase(m_bridgeSubtitles.slots[0].path.c_str())) {
        auto copy = std::make_shared<CSimpleTextSubtitle>();
        { CAutoLock lock(&m_csSubLock); copy->Copy(*primary); }
        m_bridgeSubtitleSources[0] = std::move(copy);
        m_bridgeSubtitles.slots[0].path = primary->m_path.GetString();
        m_bridgeSubtitleProvider.Release();
    }
    if (!m_bridgeSubtitleSources[0] || !m_bridgeSubtitleSources[1]) return;
    if (!m_bridgeSubtitleProvider) m_bridgeSubtitleProvider = new CDualSubtitleProvider(&m_csSubLock, m_bridgeSubtitles,
        m_bridgeSubtitleSources, AfxGetAppSettings().subdefstyle);
    m_bridgeSubtitleProvider.p->hidden = false;
    m_pCAP->SetSubtitleDelay(0); g_bExternalSubtitle = true;
    m_bridgeSubtitleActive = true;
    if (CComQIPtr<IExFilterConfig> config = m_pCAP.p) config->Flt_SetBool("lessRedraws", false);
    if (m_pMS && GetMediaState() != State_Running) {
        REFERENCE_TIME now = 0; m_pMS->GetCurrentPosition(&now);
        g_bExternalSubtitleTime = true; m_pCAP->SetTime(now);
    }
    m_pCAP->SetSubPicProvider(m_bridgeSubtitleProvider);
}

void CMainFrame::OnBridgeDualSubtitles() {
    if (!CanShowDialog()) return;
    if (!m_pCAP || m_pDVS || !AfxGetAppSettings().IsISRAutoLoadEnabled()
        || GetPlaybackMode() != PM_FILE || m_eMediaLoadState != MLS_LOADED || m_bAudioOnly) {
        AfxMessageBox(ChineseSubUI() ? L"请先播放视频，并在设置 → 字幕中选择内置字幕渲染器。" : L"Play a video and select the internal subtitle renderer in Options > Subtitles.", MB_ICONINFORMATION); return;
    }
    CancelBridgeHold(); CancelLeftLongPressSpeed(true);
    auto initial = m_bridgeSubtitles;
    if (initial.slots[0].path.empty()) {
        initial = ReadSubtitlePreferences();
        if (auto primary = dynamic_cast<CRenderedTextSubtitle*>(m_pCurrentSubStream.p)) {
            if (primary->m_subtitleType == Subtitle::SRT) initial.slots[0].path = primary->m_path.GetString();
        }
        initial.slots[0].delayMs = m_pCAP->GetSubtitleDelay();
    }
    DualSubtitleDialog dialog(GetModalParent(), initial, [this](const Bridge::DualSubtitleState& value, CString& error) {
        return ApplyBridgeSubtitles(value, error);
    }); dialog.DoModal();
}

void CMainFrame::RefreshBridgeSubtitleFiles() {
    if (!m_bridgeSubtitles.enabled || m_eMediaLoadState != MLS_LOADED) return;
    bool changed = false;
    static ULONGLONG lastAttempt = 0;
    static CString lastError;
    for (int i = 0; i < 2; ++i) {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        const bool exists = GetFileAttributesExW(m_bridgeSubtitles.slots[i].path.c_str(), GetFileExInfoStandard, &info) != FALSE;
        ULARGE_INTEGER stamp{}; stamp.LowPart = info.ftLastWriteTime.dwLowDateTime; stamp.HighPart = info.ftLastWriteTime.dwHighDateTime;
        const uint64_t token = exists ? stamp.QuadPart ^ (uint64_t(info.nFileSizeHigh) << 32) ^ info.nFileSizeLow : 0;
        if (token != m_bridgeSubtitleFileTokens[i]) { m_bridgeSubtitleFileTokens[i] = token; changed = true; }
    }
    if (changed && AfxGetAppSettings().fAutoReloadExtSubtitles && GetTickCount64() - lastAttempt > 2000) {
        lastAttempt = GetTickCount64();
        CString error;
        if (!ApplyBridgeSubtitles(m_bridgeSubtitles, error, false)) {
            if (lastError != error) m_OSD.DisplayMessage(OSD_TOPLEFT, error, 5000);
            lastError = error;
            m_bridgeSubtitleFileTokens[0] ^= 1; // retry a temporarily locked or partially written file
        } else lastError.Empty();
    }
    if (m_bridgeSubtitleProvider && m_bridgeSubtitleProvider.p->overflow.exchange(false)) {
        m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"字幕超出显示区域，请切换上下布局或减小字号。" : L"Subtitle exceeds its area. Use top/bottom layout or a smaller size.", 5000);
    }
}

void CMainFrame::CloseBridgeSubtitles() {
    m_bridgeSubtitleActive = false; m_bridgeSubtitleProvider.Release();
    if (!m_bridgeQualitySwitch.internalClose) {
        m_bridgeSubtitles = {}; for (auto& source : m_bridgeSubtitleSources) source.reset();
        m_bridgeSubtitleFileTokens[0] = m_bridgeSubtitleFileTokens[1] = 0;
    }
}

void CMainFrame::OnBridgeSubtitleRefresh() {
    if (!m_bridgeSubtitleActive || !m_bridgeSubtitles.enabled || m_eMediaLoadState != MLS_LOADED) return;
    if (auto primary = dynamic_cast<CRenderedTextSubtitle*>(m_pCurrentSubStream.p)) {
        auto source = std::make_shared<CSimpleTextSubtitle>();
        { CAutoLock lock(&m_csSubLock); source->Copy(*primary); }
        m_bridgeSubtitleSources[0] = std::move(source);
        m_bridgeSubtitleProvider.Release();
        AttachBridgeSubtitles(m_pCurrentSubStream); RepaintVideo();
    }
}
