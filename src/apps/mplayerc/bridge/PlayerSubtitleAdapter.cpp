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
    Font0, Font1, Color0, Color1, Layout, Swap, Apply, Disable, Source0, Source1 };
class DualSubtitleDialog final : public CDialog {
    std::vector<WORD> layout;
    std::vector<std::unique_ptr<CWnd>> controls;
    Bridge::DualSubtitleState state;
    std::function<bool(const Bridge::DualSubtitleState&, CString&)> apply;
    std::vector<std::wstring> names, keys;
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
            auto combo = static_cast<CComboBox*>(GetDlgItem(Source0 + i));
            const int source = combo->GetCurSel();
            if (source < 0 || source > static_cast<int>(keys.size())) return false;
            state.slots[i].embeddedTrack = source - 1;
            state.slots[i].embeddedKey = source ? keys[source - 1] : L"";
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
            const bool embedded = state.slots[i].embeddedTrack >= 0;
            static_cast<CComboBox*>(GetDlgItem(Source0 + i))->SetCurSel(embedded ? state.slots[i].embeddedTrack + 1 : 0);
            GetDlgItem(Path0+i)->EnableWindow(!embedded);
            GetDlgItem(Browse0+i)->EnableWindow(!embedded);
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
        std::vector<std::wstring> trackNames, std::vector<std::wstring> trackKeys,
        std::function<bool(const Bridge::DualSubtitleState&, CString&)> callback) : state(initial), apply(std::move(callback)), names(std::move(trackNames)), keys(std::move(trackKeys)) {
        layout.resize(sizeof(DLGTEMPLATE) / sizeof(WORD));
        auto dialog = reinterpret_cast<DLGTEMPLATE*>(layout.data());
        dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
        dialog->cx = 385; dialog->cy = 279;
        layout.push_back(0); layout.push_back(0);
        String(ChineseSubUI() ? L"双字幕对照" : L"Dual subtitles"); layout.push_back(9); String(L"Segoe UI");
        InitModalIndirect(reinterpret_cast<DLGTEMPLATE*>(layout.data()), parent);
    }
protected:
    BOOL OnInitDialog() override {
        CDialog::OnInitDialog(); const bool cn = ChineseSubUI();
        for (int i = 0; i < 2; ++i) {
            const int y = 10 + i * 85;
            Control(L"STATIC", i ? (cn ? L"副字幕" : L"Secondary") : (cn ? L"主字幕" : L"Primary"), 0, -1, {10,y+2,65,y+14});
            Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, Source0+i, {70,y,375,y+100});
            auto source = static_cast<CComboBox*>(GetDlgItem(Source0+i));
            source->AddString(cn ? L"本地 SRT 文件" : L"Local SRT file");
            for (const auto& name : names) source->AddString((cn ? CString(L"内嵌 · ") : CString(L"Embedded: ")) + name.c_str());
            Control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Path0+i, {10,y+21,306,y+36});
            Control(L"BUTTON", cn ? L"选择文件…" : L"Browse...", WS_TABSTOP, Browse0+i, {312,y+20,375,y+37});
            Control(L"BUTTON", cn ? L"显示" : L"Show", BS_AUTOCHECKBOX | WS_TABSTOP, Visible0+i, {10,y+43,58,y+57});
            Control(L"STATIC", cn ? L"延迟 ms" : L"Delay ms", 0, -1, {66,y+45,109,y+57});
            Control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Delay0+i, {112,y+42,172,y+57});
            Control(L"STATIC", cn ? L"字号" : L"Size", 0, -1, {182,y+45,213,y+57});
            Control(L"EDIT", L"", ES_NUMBER | WS_TABSTOP, Font0+i, {215,y+42,248,y+57});
            Control(L"BUTTON", L"", WS_TABSTOP, Color0+i, {265,y+41,340,y+59});
        }
        Control(L"STATIC", cn ? L"布局" : L"Layout", 0, -1, {10,194,50,208});
        Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, Layout, {54,191,265,251});
        auto combo = static_cast<CComboBox*>(GetDlgItem(Layout));
        combo->AddString(cn ? L"底部双层（主上 / 副下）" : L"Bottom pair (primary above secondary)");
        combo->AddString(cn ? L"主字幕底部 / 副字幕顶部" : L"Primary bottom / secondary top");
        Control(L"BUTTON", cn ? L"交换主副" : L"Swap", WS_TABSTOP, Swap, {282,191,375,210});
        Control(L"STATIC", cn ? L"内嵌 ASS 按文字显示；正延迟晚出现，负延迟早出现。" : L"Embedded ASS is rendered as text. Positive delay: later; negative: earlier.", 0, -1, {10,219,375,239});
        Control(L"BUTTON", cn ? L"应用" : L"Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, Apply, {103,250,178,269});
        Control(L"BUTTON", cn ? L"关闭副字幕" : L"Disable secondary", WS_TABSTOP, Disable, {182,250,287,269});
        Control(L"BUTTON", cn ? L"关闭" : L"Close", WS_TABSTOP, IDCANCEL, {291,250,375,269});
        Refresh(); return TRUE;
    }
    void OnOK() override { Commit(true); }
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override {
        const int id = LOWORD(wParam);
        if ((id == Source0 || id == Source1) && HIWORD(wParam) == CBN_SELCHANGE) {
            if (Read()) Refresh();
            return TRUE;
        }
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

namespace {
std::shared_ptr<CSimpleTextSubtitle> SnapshotSubtitle(CRenderedTextSubtitle& stream, bool embedded) {
    auto copy = std::make_shared<CSimpleTextSubtitle>();
    if (!embedded) copy->Copy(stream);
    else {
        // Composite lanes use plain text. Preserve the native track and its ASS
        // styles for ordinary playback, saving and subtitle synchronization.
        copy->m_subtitleType = Subtitle::SRT;
        copy->m_dstScreenSize = DEFSCREENSIZE;
        copy->CreateDefaultStyle(DEFAULT_CHARSET);
        for (size_t i = 0; i < stream.GetCount(); ++i) {
            const auto& cue = stream.GetAt(i);
            copy->Add(stream.GetStrW(static_cast<int>(i), false), cue.start, cue.end);
        }
    }
    return copy;
}
}

std::vector<CMainFrame::BridgeEmbeddedSubtitle> CMainFrame::GetBridgeEmbeddedSubtitles() {
    std::vector<BridgeEmbeddedSubtitle> tracks;
    CAutoLock lock(&m_csSubLock);
    for (const auto& stream : m_pSubStreams) {
        auto text = dynamic_cast<CRenderedTextSubtitle*>(stream.p);
        // ASS codec headers are parsed through a temporary file, so m_path may
        // be nonempty even for embedded tracks. Use the actual stream origin.
        if (!text || std::find(m_ExternalSubstreams.begin(), m_ExternalSubstreams.end(), stream.p) != m_ExternalSubstreams.end()) continue;
        CString name = text->m_name;
        if (name.IsEmpty()) name.Format(L"Track %u", static_cast<unsigned>(tracks.size() + 1));
        CString key; key.Format(L"%d|%s", static_cast<int>(text->m_subtitleType), name.GetString());
        tracks.push_back({stream, name.GetString(), key.GetString()});
    }
    return tracks;
}

void CMainFrame::DetachBridgeSubtitles() {
    for (auto& observed : m_bridgeSubtitleObserved) observed = 0;
    if (m_bridgeSubtitleActive && m_pCAP) {
        m_pCAP->SetSubtitleDelay(m_bridgeSubtitles.slots[0].delayMs);
        g_bExternalSubtitleTime = false;
        if (CComQIPtr<IExFilterConfig> config = m_pCAP.p) config->Flt_SetBool("lessRedraws", true);
    }
    m_bridgeSubtitleActive = false;
    m_bridgeSubtitleProvider.Release();
}

bool CMainFrame::ApplyBridgeSubtitles(const Bridge::DualSubtitleState& requested, CString& error, bool activate) {
    const bool cn = ChineseSubUI();
    if (!m_pCAP || m_pDVS || !AfxGetAppSettings().IsISRAutoLoadEnabled()
        || GetPlaybackMode() != PM_FILE || m_eMediaLoadState != MLS_LOADED) {
        error = cn ? L"请先打开视频，并在字幕设置中选择内置字幕渲染器。" : L"Open a video using the internal subtitle renderer first."; return false;
    }
    auto next = requested;
    const bool wasVisible = AfxGetAppSettings().fEnableSubtitles;
    const auto tracks = GetBridgeEmbeddedSubtitles();
    std::vector<std::wstring> keys;
    for (const auto& track : tracks) keys.push_back(track.key);
    std::shared_ptr<CSimpleTextSubtitle> parsed[2];
    CComPtr<ISubStream> streams[2];
    for (int i = 0; i < (next.enabled ? 2 : 1); ++i) {
        auto& slot = next.slots[i];
        if (!Bridge::ValidSubtitleSlot(slot)) { error = L"Invalid subtitle settings."; return false; }
        if (slot.embeddedTrack >= 0) {
            const int index = Bridge::ResolveEmbeddedTrack(slot, keys);
            if (index < 0) { error = cn ? L"所选内嵌字幕轨道已不可用，请重新选择。" : L"The embedded subtitle track is unavailable. Select it again."; return false; }
            slot.embeddedTrack = index;
            streams[i] = tracks[index].stream;
            auto text = dynamic_cast<CRenderedTextSubtitle*>(streams[i].p);
            CAutoLock lock(&m_csSubLock);
            parsed[i] = SnapshotSubtitle(*text, true); // empty is valid before the first packet arrives
        } else {
            CString path = FullSubtitlePath(slot.path);
            if (slot.path.empty() || path.IsEmpty() || path.Right(4).CompareNoCase(L".srt")) { error = cn ? L"请选择本地 SRT 文件或内嵌字幕轨道。" : L"Choose a local SRT file or embedded track."; return false; }
            slot.path = path.GetString(); slot.embeddedKey.clear();
            auto source = std::make_shared<CSimpleTextSubtitle>();
            const auto& s = AfxGetAppSettings();
            if (!source->Open(path, ExpandCodePage(s.iSubtitleDefaultCodePage), s.bSubtitleAutoDetectCodePage, L"", GetCurFileName())
                || source->m_subtitleType != Subtitle::SRT || source->IsEmpty()) {
                error.Format(cn ? L"无法读取有效 SRT 字幕：\n%s\n原字幕已保留。" : L"Cannot read a valid SRT subtitle:\n%s\nPrevious subtitles were kept.", path.GetString()); return false;
            }
            parsed[i] = std::move(source);
        }
    }
    if (next.enabled && ((streams[0] && streams[0] == streams[1])
        || (next.slots[0].embeddedTrack < 0 && next.slots[1].embeddedTrack < 0
            && !CString(next.slots[0].path.c_str()).CompareNoCase(next.slots[1].path.c_str())))) {
        error = cn ? L"主副字幕不能选择同一来源。" : L"Choose different primary and secondary subtitle sources."; return false;
    }
    // Nothing above modifies the live stream: failed replacement is transactional.
    CComPtr<ISubStream> actual = streams[0];
    if (!actual) {
        CComPtr<CRenderedTextSubtitle> primary = new CRenderedTextSubtitle(&m_csSubLock);
        primary->Copy(*parsed[0]); primary->SetName(L"Primary");
        {
            CAutoLock lock(&m_csSubLock);
            for (const auto& stream : m_pSubStreams) {
                if (auto text = dynamic_cast<CRenderedTextSubtitle*>(stream.p)) {
                    if (!text->m_path.CompareNoCase(next.slots[0].path.c_str())) { actual = stream; break; }
                }
            }
            if (!actual) actual = m_pSubStreams.emplace_back(primary);
            else if (auto old = dynamic_cast<CRenderedTextSubtitle*>(actual.p)) { old->Copy(*parsed[0]); old->Deinit(); }
        }
        if (std::find(m_ExternalSubstreams.begin(), m_ExternalSubstreams.end(), actual.p) == m_ExternalSubstreams.end()) m_ExternalSubstreams.push_back(actual.p);
        streams[0] = actual;
    }
    const bool previouslyActive = m_bridgeSubtitleActive;
    DetachBridgeSubtitles();
    m_bridgeSubtitles = next;
    for (int i = 0; i < 2; ++i) {
        m_bridgeSubtitleSources[i] = std::move(parsed[i]);
        m_bridgeSubtitleStreams[i] = streams[i];
    }
    if (previouslyActive || next.enabled) m_pCAP->SetSubtitleDelay(next.enabled ? 0 : next.slots[0].delayMs);
    AfxGetAppSettings().fEnableSubtitles = true;
    SetSubtitle(actual);
    m_nSelSub2 = m_iSubtitleSel;
    if (!activate && !wasVisible) {
        AfxGetAppSettings().fEnableSubtitles = false; m_iSubtitleSel = -1; UpdateSubtitle();
    }
    if (next.slots[0].embeddedTrack < 0) m_wndPlaylistBar.AddSubtitleToCurrent(next.slots[0].path.c_str());
    RepaintVideo(); SetToolBarSubtitleButton();
    return true;
}

void CMainFrame::AttachBridgeSubtitles(ISubStream* selected) {
    if (!m_pCAP) return;
    if (m_bridgeQualitySwitch.pending || !AfxGetAppSettings().fEnableSubtitles) return;
    if (!m_bridgeSubtitles.enabled) { DetachBridgeSubtitles(); return; }
    auto primary = dynamic_cast<CRenderedTextSubtitle*>(selected);
    const auto tracks = GetBridgeEmbeddedSubtitles();
    int embedded = -1;
    for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].stream == selected) embedded = static_cast<int>(i);
    if (!primary || (embedded < 0 && (primary->m_subtitleType != Subtitle::SRT || primary->m_path.IsEmpty()))) {
        DetachBridgeSubtitles(); m_bridgeSubtitles.enabled = false;
        m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"当前轨道不支持双字幕，已恢复普通字幕。" : L"This track does not support dual subtitles. Using normal subtitles.", 4000);
        return;
    }
    if (selected != m_bridgeSubtitleStreams[0]) {
        if (selected == m_bridgeSubtitleStreams[1]) {
            std::swap(m_bridgeSubtitles.slots[0], m_bridgeSubtitles.slots[1]);
            std::swap(m_bridgeSubtitleStreams[0], m_bridgeSubtitleStreams[1]);
            std::swap(m_bridgeSubtitleSources[0], m_bridgeSubtitleSources[1]);
        } else {
            auto& slot = m_bridgeSubtitles.slots[0]; slot.embeddedTrack = embedded;
            if (embedded >= 0) slot.embeddedKey = tracks[embedded].key;
            else { slot.path = primary->m_path.GetString(); slot.embeddedKey.clear(); }
            m_bridgeSubtitleStreams[0] = selected;
            CAutoLock lock(&m_csSubLock);
            m_bridgeSubtitleSources[0] = SnapshotSubtitle(*primary, embedded >= 0);
        }
        m_bridgeSubtitleProvider.Release();
    }
    if (!m_bridgeSubtitleSources[0] || !m_bridgeSubtitleSources[1]) return;
    if (!m_bridgeSubtitleProvider) m_bridgeSubtitleProvider = new CDualSubtitleProvider(&m_csSubLock, m_bridgeSubtitles,
        m_bridgeSubtitleSources, AfxGetAppSettings().subdefstyle);
    for (int i = 0; i < 2; ++i) m_bridgeSubtitleObserved[i] = reinterpret_cast<DWORD_PTR>(m_bridgeSubtitleStreams[i].p);
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
    const auto tracks = GetBridgeEmbeddedSubtitles();
    std::vector<std::wstring> names, keys;
    for (const auto& track : tracks) { names.push_back(track.name); keys.push_back(track.key); }
    auto initial = m_bridgeSubtitles;
    if (!initial.enabled && initial.slots[0].path.empty() && initial.slots[0].embeddedTrack < 0) {
        initial = ReadSubtitlePreferences();
        if (auto primary = dynamic_cast<CRenderedTextSubtitle*>(m_pCurrentSubStream.p)) {
            if (primary->m_subtitleType == Subtitle::SRT) initial.slots[0].path = primary->m_path.GetString();
        }
        for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].stream == m_pCurrentSubStream) {
            initial.slots[0].embeddedTrack = static_cast<int>(i); initial.slots[0].embeddedKey = tracks[i].key;
        }
        initial.slots[0].delayMs = m_pCAP->GetSubtitleDelay();
        for (size_t i = 0; i < tracks.size(); ++i) if (static_cast<int>(i) != initial.slots[0].embeddedTrack) {
            initial.slots[1].embeddedTrack = static_cast<int>(i); initial.slots[1].embeddedKey = tracks[i].key; break;
        }
    }
    for (auto& slot : initial.slots) if (slot.embeddedTrack >= 0) slot.embeddedTrack = Bridge::ResolveEmbeddedTrack(slot, keys);
    DualSubtitleDialog dialog(GetModalParent(), initial, names, keys, [this](const Bridge::DualSubtitleState& value, CString& error) {
        return ApplyBridgeSubtitles(value, error);
    }); dialog.DoModal();
}

void CMainFrame::RefreshBridgeSubtitleFiles() {
    if (!m_bridgeSubtitles.enabled || m_eMediaLoadState != MLS_LOADED) return;
    bool changed = false;
    static ULONGLONG lastAttempt = 0;
    static CString lastError;
    for (int i = 0; i < 2; ++i) {
        if (m_bridgeSubtitles.slots[i].embeddedTrack >= 0) continue;
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
            for (int i = 0; i < 2; ++i) if (m_bridgeSubtitles.slots[i].embeddedTrack < 0) { m_bridgeSubtitleFileTokens[i] ^= 1; break; }
        } else lastError.Empty();
    }
    if (m_bridgeSubtitleProvider && m_bridgeSubtitleProvider.p->overflow.exchange(false)) {
        m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"字幕超出显示区域，请切换上下布局或减小字号。" : L"Subtitle exceeds its area. Use top/bottom layout or a smaller size.", 5000);
    }
}

void CMainFrame::CloseBridgeSubtitles() {
    for (auto& observed : m_bridgeSubtitleObserved) observed = 0;
    m_bridgeSubtitleRefreshQueued = false;
    m_bridgeSubtitleActive = false; m_bridgeSubtitleProvider.Release();
    for (auto& stream : m_bridgeSubtitleStreams) stream.Release();
    if (!m_bridgeQualitySwitch.internalClose) {
        m_bridgeSubtitles = {}; for (auto& source : m_bridgeSubtitleSources) source.reset();
        m_bridgeSubtitleFileTokens[0] = m_bridgeSubtitleFileTokens[1] = 0;
    }
}

void CMainFrame::OnBridgeSubtitleRefresh() {
    m_bridgeSubtitleRefreshQueued = false;
    if (!m_bridgeSubtitles.enabled || m_eMediaLoadState != MLS_LOADED) return;
    const auto tracks = GetBridgeEmbeddedSubtitles();
    std::vector<std::wstring> keys;
    for (const auto& track : tracks) keys.push_back(track.key);
    for (int i = 0; i < 2; ++i) {
        auto& slot = m_bridgeSubtitles.slots[i];
        if (slot.embeddedTrack >= 0) {
            const int index = Bridge::ResolveEmbeddedTrack(slot, keys);
            if (index < 0) {
                DetachBridgeSubtitles(); m_bridgeSubtitles.enabled = false; UpdateSubtitle();
                m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"内嵌轨道已变化，请重新选择双字幕。" : L"Embedded tracks changed. Select dual subtitles again.", 5000); return;
            }
            slot.embeddedTrack = index; m_bridgeSubtitleStreams[i] = tracks[index].stream;
        }
        if (auto text = dynamic_cast<CRenderedTextSubtitle*>(m_bridgeSubtitleStreams[i].p)) {
            CAutoLock lock(&m_csSubLock);
            m_bridgeSubtitleSources[i] = SnapshotSubtitle(*text, slot.embeddedTrack >= 0);
        }
    }
    m_bridgeSubtitleProvider.Release();
    AttachBridgeSubtitles(m_pCurrentSubStream); RepaintVideo();
}
