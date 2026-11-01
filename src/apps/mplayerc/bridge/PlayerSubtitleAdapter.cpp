// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include "DualSubtitleProvider.h"
#include "SubtitleTextSource.h"
#include "Subtitles/RenderedHdmvSubtitle.h"
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
        key.Format(L"BitmapScale%d", i);
        profile.ReadInt(Section, key, state.slots[i].bitmapScale, 25, 200);
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
        key.Format(L"BitmapScale%d", i); profile.WriteInt(Section, key, state.slots[i].bitmapScale);
    }
    profile.Flush(false);
}

enum { Path0 = 600, Path1, Browse0, Browse1, Visible0, Visible1, Delay0, Delay1,
    Font0, Font1, Color0, Color1, Layout, Swap, Apply, Disable, Source0, Source1, FontLabel0, FontLabel1, SourceStatus };
class DualSubtitleDialog final : public CDialog {
    std::vector<WORD> layout;
    std::vector<std::unique_ptr<CWnd>> controls;
    Bridge::DualSubtitleState state;
    std::function<bool(const Bridge::DualSubtitleState&, CString&)> apply;
    std::function<CString()> subtitleStatus;
    std::vector<std::wstring> names, keys;
    std::vector<bool> textSources;
    bool IsTextSource(int index) const { return index < 0 || (size_t(index) < textSources.size() && textSources[index]); }
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
        auto next = state;
        for (int i = 0; i < 2; ++i) {
            const bool wasText = IsTextSource(state.slots[i].embeddedTrack);
            auto combo = static_cast<CComboBox*>(GetDlgItem(Source0 + i));
            const int source = combo->GetCurSel();
            if (source < 0 || source > static_cast<int>(keys.size())) return false;
            next.slots[i].embeddedTrack = source - 1;
            next.slots[i].embeddedKey = source ? keys[source - 1] : L"";
            CString text; GetDlgItemTextW(Path0 + i, text); next.slots[i].path = text.GetString();
            next.slots[i].visible = IsDlgButtonChecked(Visible0 + i) == BST_CHECKED;
            GetDlgItemTextW(Delay0 + i, text);
            wchar_t* end = nullptr; const auto delay = wcstol(text, &end, 10);
            if (text.IsEmpty() || *end || delay < -Bridge::SubtitleDelayLimit || delay > Bridge::SubtitleDelayLimit) return false;
            next.slots[i].delayMs = static_cast<int>(delay);
            GetDlgItemTextW(Font0 + i, text); const auto font = wcstol(text, &end, 10);
            if (text.IsEmpty() || *end || font < (wasText ? 8 : 25) || font > (wasText ? 48 : 200)) return false;
            if (wasText) next.slots[i].fontSize = static_cast<int>(font);
            else next.slots[i].bitmapScale = static_cast<int>(font);
        }
        next.layout = static_cast<CComboBox*>(GetDlgItem(Layout))->GetCurSel();
        if (next.layout < 0) return false;
        state = std::move(next);
        return true;
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
            const bool textSource = IsTextSource(state.slots[i].embeddedTrack);
            SetDlgItemInt(Font0 + i, textSource ? state.slots[i].fontSize : state.slots[i].bitmapScale);
            SetDlgItemTextW(FontLabel0 + i, textSource ? (ChineseSubUI() ? L"字号" : L"Size") : (ChineseSubUI() ? L"缩放%" : L"Scale%"));
            GetDlgItem(Color0 + i)->EnableWindow(textSource);
            CString color; color.Format(L"#%02X%02X%02X", GetRValue(state.slots[i].color), GetGValue(state.slots[i].color), GetBValue(state.slots[i].color));
            SetDlgItemTextW(Color0 + i, color);
        }
        static_cast<CComboBox*>(GetDlgItem(Layout))->SetCurSel(state.layout);
    }
    bool Commit(bool enabled) {
        const bool cn = ChineseSubUI();
        if (!Read()) { AfxMessageBox(cn ? L"延迟范围 ±600000 毫秒，字号 8–48，图片缩放 25–200%。" : L"Delay: +/-600000 ms; text size: 8-48; picture scale: 25-200%.", MB_ICONWARNING); return false; }
        state.enabled = enabled;
        CString error;
        if (!apply(state, error)) { AfxMessageBox(error, MB_ICONWARNING); return false; }
        SaveSubtitlePreferences(state); return true;
    }
public:
    DualSubtitleDialog(CWnd* parent, const Bridge::DualSubtitleState& initial,
        std::vector<std::wstring> trackNames, std::vector<std::wstring> trackKeys,
        std::vector<bool> trackText, std::function<bool(const Bridge::DualSubtitleState&, CString&)> callback,
        std::function<CString()> status) : state(initial), apply(std::move(callback)), subtitleStatus(std::move(status)), names(std::move(trackNames)), keys(std::move(trackKeys)), textSources(std::move(trackText)) {
        layout.resize(sizeof(DLGTEMPLATE) / sizeof(WORD));
        auto dialog = reinterpret_cast<DLGTEMPLATE*>(layout.data());
        dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
        dialog->cx = 385; dialog->cy = 304;
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
            source->AddString(cn ? L"本地字幕文件（SRT / ASS / SSA / VTT）" : L"Subtitle file (SRT / ASS / SSA / VTT)");
            source->SetDroppedWidth(600);
            for (const auto& name : names) source->AddString((cn ? CString(L"内嵌 · ") : CString(L"Embedded: ")) + name.c_str());
            Control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Path0+i, {10,y+21,306,y+36});
            Control(L"BUTTON", cn ? L"选择文件…" : L"Browse...", WS_TABSTOP, Browse0+i, {312,y+20,375,y+37});
            Control(L"BUTTON", cn ? L"显示" : L"Show", BS_AUTOCHECKBOX | WS_TABSTOP, Visible0+i, {10,y+43,58,y+57});
            Control(L"STATIC", cn ? L"延迟 ms" : L"Delay ms", 0, -1, {66,y+45,109,y+57});
            Control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, Delay0+i, {112,y+42,172,y+57});
            Control(L"STATIC", cn ? L"字号" : L"Size", 0, FontLabel0+i, {180,y+45,214,y+57});
            Control(L"EDIT", L"", ES_NUMBER | WS_TABSTOP, Font0+i, {215,y+42,248,y+57});
            Control(L"BUTTON", L"", WS_TABSTOP, Color0+i, {265,y+41,340,y+59});
        }
        Control(L"STATIC", cn ? L"布局" : L"Layout", 0, -1, {10,194,50,208});
        Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, Layout, {54,191,265,251});
        auto combo = static_cast<CComboBox*>(GetDlgItem(Layout));
        combo->AddString(cn ? L"底部双层（主上 / 副下）" : L"Bottom pair (primary above secondary)");
        combo->AddString(cn ? L"主字幕底部 / 副字幕顶部" : L"Primary bottom / secondary top");
        Control(L"BUTTON", cn ? L"交换主副" : L"Swap", WS_TABSTOP, Swap, {282,191,375,210});
        Control(L"STATIC", cn ? L"ASS 使用阅读排版；PGS 原图自动适应分区。正延迟晚出现。" : L"ASS uses reading layout; PGS pictures fit their lane. Positive delay: later.", 0, -1, {10,219,375,245});
        Control(L"STATIC", subtitleStatus(), 0, SourceStatus, {10,245,375,270});
        Control(L"BUTTON", cn ? L"应用" : L"Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, Apply, {103,275,178,294});
        Control(L"BUTTON", cn ? L"仅用主字幕" : L"Primary only", WS_TABSTOP, Disable, {182,275,287,294});
        Control(L"BUTTON", cn ? L"关闭" : L"Close", WS_TABSTOP, IDCANCEL, {291,275,375,294});
        SetTimer(1,500,nullptr);
        Refresh(); return TRUE;
    }
    void OnOK() override { Commit(true); }
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (message == WM_TIMER && wParam == 1 && GetDlgItem(SourceStatus)) SetDlgItemTextW(SourceStatus,subtitleStatus());
        return CDialog::WindowProc(message,wParam,lParam);
    }
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
                    L"Text subtitles (*.srt;*.ass;*.ssa;*.vtt)|*.srt;*.ass;*.ssa;*.vtt|SubRip (*.srt)|*.srt|ASS / SSA (*.ass;*.ssa)|*.ass;*.ssa|WebVTT (*.vtt)|*.vtt||", this);
                if (dialog.DoModal() == IDOK) {
                    // Preserve edits using the previously displayed source's
                    // units, then switch Scale% back to the text Size control.
                    if (!Read()) {
                        AfxMessageBox(ChineseSubUI() ? L"请先修正延迟、字号或缩放数值。" : L"Correct the delay, size or scale value first.", MB_ICONWARNING);
                        return TRUE;
                    }
                    const int slot = id - Browse0;
                    state.slots[slot].embeddedTrack = -1; state.slots[slot].embeddedKey.clear();
                    state.slots[slot].path = dialog.GetPathName().GetString();
                    Refresh();
                }
                return TRUE;
            }
        }
        return CDialog::OnCommand(wParam, lParam);
    }
};
}

std::vector<CMainFrame::BridgeEmbeddedSubtitle> CMainFrame::GetBridgeEmbeddedSubtitles() {
    std::vector<BridgeEmbeddedSubtitle> tracks;
    CAutoLock lock(&m_csSubLock);
    for (const auto& stream : m_pSubStreams) {
        auto text = dynamic_cast<CRenderedTextSubtitle*>(stream.p);
        // ASS codec headers are parsed through a temporary file, so m_path may
        // be nonempty even for embedded tracks. Use the actual stream origin.
        if (std::find(m_ExternalSubstreams.begin(), m_ExternalSubstreams.end(), stream.p) != m_ExternalSubstreams.end()) continue;
        CComHeapPtr<WCHAR> streamName;
        LCID language = 0;
        stream->GetStreamInfo(0, &streamName, &language);
        CString name = streamName ? CString(streamName) : CString();
        if (name.IsEmpty()) name.Format(L"Track %u", static_cast<unsigned>(tracks.size() + 1));
        CString key, label;
        const bool readable = text && Bridge::SupportedSubtitleTextType(text->m_subtitleType);
        if (text) {
            // Preserve existing text-track keys for playback memory compatibility.
            key.Format(L"%d|%s", static_cast<int>(text->m_subtitleType), name.GetString());
            label = name + L" · " + Bridge::SubtitleTextFormatName(text->m_subtitleType);
        } else {
            CLSID clsid{}; stream->GetClassID(&clsid);
            key.Format(L"picture|%s|%u|%s", CStringFromGUID(clsid).GetString(), language, name.GetString());
            const auto bitmap = dynamic_cast<CRenderedHdmvSubtitle*>(stream.p);
            const wchar_t* format = bitmap && bitmap->GetType() == ST_HDMV ? L"PGS" : L"Picture";
            label = name + L" · " + format;
            if (!bitmap || bitmap->GetType() != ST_HDMV) label += ChineseSubUI() ? L"（仅单字幕）" : L" (primary only)";
        }
        tracks.push_back({stream, label.GetString(), key.GetString(), readable});
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
    std::shared_ptr<CSimpleTextSubtitle> originals[2];
    CComPtr<ISubStream> streams[2];
    for (int i = 0; i < (next.enabled ? 2 : 1); ++i) {
        auto& slot = next.slots[i];
        if (!Bridge::ValidSubtitleSlot(slot)) { error = L"Invalid subtitle settings."; return false; }
        if (slot.embeddedTrack >= 0) {
            const int index = Bridge::ResolveEmbeddedTrack(slot, keys);
            if (index < 0) { error = cn ? L"所选内嵌字幕轨道已不可用，请重新选择。" : L"The embedded subtitle track is unavailable. Select it again."; return false; }
            slot.embeddedTrack = index;
            slot.path.clear();
            streams[i] = tracks[index].stream;
            const auto bitmap = dynamic_cast<CRenderedHdmvSubtitle*>(streams[i].p);
            if (next.enabled && !tracks[index].readable && (!bitmap || bitmap->GetType() != ST_HDMV)) {
                error = cn ? L"此来源暂不支持双字幕。可用“仅用主字幕”查看原字幕。"
                    : L"This source supports Primary only; dual mode supports text and PGS.";
                return false;
            }
            auto text = dynamic_cast<CRenderedTextSubtitle*>(streams[i].p);
            CAutoLock lock(&m_csSubLock);
            if (text) parsed[i] = Bridge::MakeSubtitleReadingCopy(*text); // empty is valid before the first packet arrives
        } else {
            CString path = FullSubtitlePath(slot.path);
            if (slot.path.empty() || path.IsEmpty() || Bridge::SubtitleFileFormat(path.GetString()) == Bridge::SubtitleTextFormat::Unsupported) {
                error = cn ? L"请选择 SRT、ASS、SSA、VTT 字幕文件或内嵌字幕轨道。" : L"Choose an SRT, ASS, SSA or VTT file, or an embedded subtitle track."; return false;
            }
            slot.path = path.GetString(); slot.embeddedKey.clear();
            auto source = std::make_shared<CSimpleTextSubtitle>();
            const auto& s = AfxGetAppSettings();
            if (!source->Open(path, ExpandCodePage(s.iSubtitleDefaultCodePage), s.bSubtitleAutoDetectCodePage, L"", GetCurFileName())
                || !Bridge::SupportedSubtitleTextType(source->m_subtitleType) || source->IsEmpty()) {
                error.Format(cn ? L"无法读取有效的文本字幕：\n%s\n原字幕已保留。" : L"Cannot read a valid text subtitle:\n%s\nPrevious subtitles were kept.", path.GetString()); return false;
            }
            originals[i] = source;
            parsed[i] = Bridge::MakeSubtitleReadingCopy(*source);
            if (next.enabled && parsed[i]->IsEmpty()) {
                error = cn ? L"该字幕没有可用于双语阅读的文字。可用“仅用主字幕”查看原样式。"
                    : L"This subtitle has no text for reading layout. Use Primary only to view the original.";
                return false;
            }
        }
    }
    if (next.enabled && ((streams[0] && streams[0] == streams[1])
        || (next.slots[0].embeddedTrack < 0 && next.slots[1].embeddedTrack < 0
            && !CString(next.slots[0].path.c_str()).CompareNoCase(next.slots[1].path.c_str())))) {
        error = cn ? L"主副字幕不能选择同一来源。" : L"Choose different primary and secondary subtitle sources."; return false;
    }
    // Nothing above modifies the live stream: failed replacement is transactional.
    bool sameSources = m_bridgeSubtitles.enabled && next.enabled;
    for (int i=0;i<2;++i) sameSources = sameSources
        && m_bridgeSubtitles.slots[i].embeddedKey == next.slots[i].embeddedKey
        && m_bridgeSubtitles.slots[i].path == next.slots[i].path;
    if (sameSources) for (int i=0;i<2;++i) if (m_bridgeSubtitlePrepared[i]) parsed[i] = m_bridgeSubtitleSources[i];
    CComPtr<ISubStream> actual = streams[0];
    if (!actual) {
        CComPtr<CRenderedTextSubtitle> primary = new CRenderedTextSubtitle(&m_csSubLock);
        primary->Copy(*originals[0]);
        {
            CAutoLock lock(&m_csSubLock);
            for (const auto& stream : m_pSubStreams) {
                if (auto text = dynamic_cast<CRenderedTextSubtitle*>(stream.p)) {
                    if (!text->m_path.CompareNoCase(next.slots[0].path.c_str())) { actual = stream; break; }
                }
            }
            if (!actual) actual = m_pSubStreams.emplace_back(primary);
            else if (auto old = dynamic_cast<CRenderedTextSubtitle*>(actual.p)) { old->Copy(*originals[0]); old->Deinit(); }
        }
        if (std::find(m_ExternalSubstreams.begin(), m_ExternalSubstreams.end(), actual.p) == m_ExternalSubstreams.end()) m_ExternalSubstreams.push_back(actual.p);
        streams[0] = actual;
    }
    const bool previouslyActive = m_bridgeSubtitleActive;
    DetachBridgeSubtitles();
    if (!sameSources) {
        m_bridgeSubtitlePreparation.reset(); m_bridgeSubtitlePreparationPublished = false;
        for (int i=0;i<2;++i) { m_bridgeSubtitleIndependent[i].Release(); m_bridgeSubtitlePrepared[i] = false; }
    }
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
    if (m_bridgeMemory.ready && !m_bridgeMemory.applying) {
        m_bridgeMemory.degradedSubtitles = false;
        SaveBridgePlaybackMemory(true);
    }
    return true;
}

bool CMainFrame::AttachBridgeSubtitles(ISubStream* selected, bool rebuild) {
    if (!m_pCAP) return false;
    if (m_bridgeQualitySwitch.pending || !AfxGetAppSettings().fEnableSubtitles) return false;
    if (!m_bridgeSubtitles.enabled) { DetachBridgeSubtitles(); return false; }
    auto primary = dynamic_cast<CRenderedTextSubtitle*>(selected);
    auto primaryPicture = dynamic_cast<CRenderedHdmvSubtitle*>(selected);
    const auto tracks = GetBridgeEmbeddedSubtitles();
    int embedded = -1;
    for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].stream == selected) embedded = static_cast<int>(i);
    if ((!primary || !Bridge::SupportedSubtitleTextType(primary->m_subtitleType) || (embedded < 0 && primary->m_path.IsEmpty()))
        && (!primaryPicture || primaryPicture->GetType() != ST_HDMV || embedded < 0)) {
        DetachBridgeSubtitles(); m_bridgeSubtitles.enabled = false;
        m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"当前轨道不支持双字幕，已恢复普通字幕。" : L"This track does not support dual subtitles. Using normal subtitles.", 4000);
        return false;
    }
    if (selected != m_bridgeSubtitleStreams[0]) {
        if (selected == m_bridgeSubtitleStreams[1]) {
            std::swap(m_bridgeSubtitles.slots[0], m_bridgeSubtitles.slots[1]);
            std::swap(m_bridgeSubtitleStreams[0], m_bridgeSubtitleStreams[1]);
            std::swap(m_bridgeSubtitleSources[0], m_bridgeSubtitleSources[1]);
            std::swap(m_bridgeSubtitleIndependent[0], m_bridgeSubtitleIndependent[1]);
            std::swap(m_bridgeSubtitlePrepared[0], m_bridgeSubtitlePrepared[1]);
        } else {
            auto& slot = m_bridgeSubtitles.slots[0]; slot.embeddedTrack = embedded;
            if (embedded >= 0) slot.embeddedKey = tracks[embedded].key;
            else { slot.path = primary->m_path.GetString(); slot.embeddedKey.clear(); }
            m_bridgeSubtitleStreams[0] = selected;
            m_bridgeSubtitleIndependent[0].Release(); m_bridgeSubtitlePrepared[0] = false;
            CAutoLock lock(&m_csSubLock);
            m_bridgeSubtitleSources[0] = primary ? Bridge::MakeSubtitleReadingCopy(*primary) : nullptr;
        }
        rebuild = true;
    }
    for (int i = 0; i < 2; ++i) if (!m_bridgeSubtitleSources[i] && !dynamic_cast<CRenderedHdmvSubtitle*>(m_bridgeSubtitleStreams[i].p)) return false;
    CComPtr<ISubStream> effective[2];
    for (int i=0;i<2;++i) effective[i] = m_bridgeSubtitleIndependent[i] ? m_bridgeSubtitleIndependent[i] : m_bridgeSubtitleStreams[i];
    bool completePictures = true;
    for (int i=0;i<2;++i) if (dynamic_cast<CRenderedHdmvSubtitle*>(effective[i].p) && !m_bridgeSubtitlePrepared[i]) completePictures = false;
    if (rebuild || !m_bridgeSubtitleProvider) {
        CComPtr<CDualSubtitleProvider> next = new CDualSubtitleProvider(&m_csSubLock, m_bridgeSubtitles,
            m_bridgeSubtitleSources, effective, AfxGetAppSettings().subdefstyle, completePictures);
        CAutoLock publish(&m_csSubLock);
        m_bridgeSubtitleProvider = next;
    }
    for (int i = 0; i < 2; ++i) m_bridgeSubtitleObserved[i] = m_bridgeSubtitlePrepared[i] ? 0 : reinterpret_cast<DWORD_PTR>(m_bridgeSubtitleStreams[i].p);
    m_bridgeSubtitleProvider.p->hidden = false;
    m_pCAP->SetSubtitleDelay(0); g_bExternalSubtitle = true;
    for (auto& stream : m_bridgeSubtitleStreams) if (auto provider = dynamic_cast<CSubPicProviderImpl*>(stream.p)) provider->SetMediaTimeline(true);
    m_bridgeSubtitleActive = true;
    if (CComQIPtr<IExFilterConfig> config = m_pCAP.p) config->Flt_SetBool("lessRedraws", false);
    if (m_pMS && GetMediaState() != State_Running) {
        REFERENCE_TIME now = 0; m_pMS->GetCurrentPosition(&now);
        g_bExternalSubtitleTime = true; m_pCAP->SetTime(now);
    }
    m_pCAP->SetSubPicProvider(m_bridgeSubtitleProvider);
    // Showing a paused live-picture provider must discard its cached hidden
    // frame even when the provider pointer itself has not changed.
    m_pCAP->Invalidate();
    // A new provider can reuse the old allocation address. Explicitly retire
    // paused swap-chain frames on every attachment, including same-time edits.
    m_bridgeSubtitleRefreshFrames = 8;
    PrepareBridgeSubtitleFile();
    return true;
}

void CMainFrame::PrepareBridgeSubtitleFile() {
    if (!m_bridgeSubtitles.enabled || m_eMediaLoadState != MLS_LOADED || GetPlaybackMode() != PM_FILE) return;
    const CString file = GetCurFileName();
    if (PathIsURLW(file) || file.Right(4).CompareNoCase(L".mkv")) return;
    std::array<Bridge::SubtitleFileRequest,2> selected;
    bool any = false;
    for (int i=0;i<2;++i) {
        if (m_bridgeSubtitles.slots[i].embeddedTrack < 0 || !m_bridgeSubtitleStreams[i]) continue;
        auto& request = selected[i]; request.key = m_bridgeSubtitles.slots[i].embeddedKey;
        CComHeapPtr<WCHAR> name;
        m_bridgeSubtitleStreams[i]->GetStreamInfo(0,&name,nullptr);
        if (name) request.nativeName = name;
        if (auto picture = dynamic_cast<CRenderedHdmvSubtitle*>(m_bridgeSubtitleStreams[i].p)) {
            if (picture->GetType() == ST_HDMV) request.codec = "S_HDMV/PGS";
        } else if (auto text = dynamic_cast<CRenderedTextSubtitle*>(m_bridgeSubtitleStreams[i].p)) {
            switch (text->m_subtitleType) {
            case Subtitle::ASS: request.codec = "S_TEXT/ASS"; break;
            case Subtitle::SSA: request.codec = "S_TEXT/SSA"; break;
            case Subtitle::SRT: request.codec = "S_TEXT/UTF8"; break;
            case Subtitle::VTT: request.codec = "S_TEXT/WEBVTT"; break;
            }
        }
        if (request.codec.empty()) return;
        any = true;
    }
    if (!any || (m_bridgeSubtitlePreparation && m_bridgeSubtitlePreparation->Matches(file.GetString(),selected))) return;
    m_bridgeSubtitlePreparation.reset();
    m_bridgeSubtitlePreparationPublished = false;
    try {
        m_bridgeSubtitlePreparation = std::make_unique<Bridge::SubtitleFilePreparation>(file.GetString(),selected,&m_csSubLock);
        m_OSD.DisplayMessage(OSD_TOPLEFT,ChineseSubUI() ? L"正在后台读取完整字幕，当前字幕继续显示。" : L"Preparing full subtitle tracks in the background.",3000);
    } catch (...) {
        m_OSD.DisplayMessage(OSD_TOPLEFT,ChineseSubUI() ? L"无法准备完整字幕，继续使用当前字幕。" : L"Cannot prepare full tracks. Keeping current subtitles.",4000);
    }
}

void CMainFrame::PollBridgeSubtitlePreparation() {
    if (!m_bridgeSubtitlePreparation || m_bridgeSubtitlePreparationPublished || !m_bridgeSubtitlePreparation->Ready()) return;
    m_bridgeSubtitlePreparationPublished = true;
    if (m_bridgeSubtitlePreparation->status != Bridge::SubtitleReadStatus::Ready) {
        m_OSD.DisplayMessage(OSD_TOPLEFT,ChineseSubUI() ? L"完整字幕读取未完成，保留当前原生字幕。" : L"Full track preparation failed. Keeping native subtitles.",4000);
        return;
    }
    const auto& result = m_bridgeSubtitlePreparation->result;
    for (int i=0;i<2;++i) if (result[i].uid) {
        m_bridgeSubtitleIndependent[i] = result[i].picture;
        if (result[i].text) m_bridgeSubtitleSources[i] = result[i].text;
        m_bridgeSubtitlePrepared[i] = true;
    }
    AttachBridgeSubtitles(m_pCurrentSubStream, true); RepaintVideo();
    m_OSD.DisplayMessage(OSD_TOPLEFT,ChineseSubUI() ? L"完整字幕已就绪。" : L"Full subtitle tracks are ready.",2500);
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
    std::vector<bool> textSources;
    for (const auto& track : tracks) { names.push_back(track.name); keys.push_back(track.key); textSources.push_back(track.readable); }
    auto initial = m_bridgeSubtitles;
    if (!initial.enabled && initial.slots[0].path.empty() && initial.slots[0].embeddedTrack < 0) {
        initial = ReadSubtitlePreferences();
        if (auto primary = dynamic_cast<CRenderedTextSubtitle*>(m_pCurrentSubStream.p)) {
            if (Bridge::SupportedSubtitleTextType(primary->m_subtitleType)) initial.slots[0].path = primary->m_path.GetString();
        }
        for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].stream == m_pCurrentSubStream) {
            initial.slots[0].embeddedTrack = static_cast<int>(i); initial.slots[0].embeddedKey = tracks[i].key;
        }
        initial.slots[0].delayMs = m_pCAP->GetSubtitleDelay();
        for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].readable && static_cast<int>(i) != initial.slots[0].embeddedTrack) {
            initial.slots[1].embeddedTrack = static_cast<int>(i); initial.slots[1].embeddedKey = tracks[i].key; break;
        }
    }
    for (auto& slot : initial.slots) if (slot.embeddedTrack >= 0) slot.embeddedTrack = Bridge::ResolveEmbeddedTrack(slot, keys);
    DualSubtitleDialog dialog(GetModalParent(), initial, names, keys, textSources, [this](const Bridge::DualSubtitleState& value, CString& error) {
        return ApplyBridgeSubtitles(value, error);
    }, [this] {
        if (!m_bridgeSubtitlePreparation) return CString();
        if (m_bridgeSubtitlePreparation->Ready()) return CString(m_bridgeSubtitlePreparation->status == Bridge::SubtitleReadStatus::Ready
            ? (ChineseSubUI() ? L"完整字幕已就绪。" : L"Full subtitle tracks are ready.")
            : (ChineseSubUI() ? L"完整读取失败，保留原生字幕。" : L"Full track preparation failed; native subtitles retained."));
        const auto seconds = m_bridgeSubtitlePreparation->scannedTime.load()/10000000;
        CString progress; progress.Format(ChineseSubUI() ? L"后台读取完整字幕：已扫描到 %lld:%02lld" : L"Reading full subtitle tracks: scanned to %lld:%02lld",seconds/60,seconds%60);
        return progress;
    }); dialog.DoModal();
}

void CMainFrame::RefreshBridgeSubtitleFiles() {
    if (!m_bridgeSubtitles.enabled || m_eMediaLoadState != MLS_LOADED) {
        m_bridgeSubtitlePreparation.reset();
        return;
    }
    PollBridgeSubtitlePreparation();
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
        m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"字幕内容较大，建议切换上下布局或调整字幕大小。" : L"Subtitles need more space. Use top/bottom layout or adjust subtitle size.", 5000);
    }
}

void CMainFrame::CloseBridgeSubtitles() {
    for (auto& stream : m_bridgeSubtitleStreams) if (auto provider = dynamic_cast<CSubPicProviderImpl*>(stream.p)) provider->SetMediaTimeline(false);
    m_bridgeSubtitleRefreshFrames = 0;
    m_bridgeSubtitlePreparation.reset(); m_bridgeSubtitlePreparationPublished = false;
    for (auto& observed : m_bridgeSubtitleObserved) observed = 0;
    m_bridgeSubtitleRefreshQueued = false;
    m_bridgeSubtitleActive = false; m_bridgeSubtitleProvider.Release();
    for (auto& stream : m_bridgeSubtitleStreams) stream.Release();
    for (int i=0;i<2;++i) { m_bridgeSubtitleIndependent[i].Release(); m_bridgeSubtitlePrepared[i] = false; }
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
    bool rebuild = !m_bridgeSubtitleProvider;
    bool refreshPictures = false;
    for (int i = 0; i < 2; ++i) {
        auto& slot = m_bridgeSubtitles.slots[i];
        const auto previous = m_bridgeSubtitleStreams[i];
        if (slot.embeddedTrack >= 0) {
            const int index = Bridge::ResolveEmbeddedTrack(slot, keys);
            if (index < 0) {
                DetachBridgeSubtitles(); m_bridgeSubtitles.enabled = false; UpdateSubtitle();
                m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseSubUI() ? L"内嵌轨道已变化，请重新选择双字幕。" : L"Embedded tracks changed. Select dual subtitles again.", 5000); return;
            }
            slot.embeddedTrack = index; m_bridgeSubtitleStreams[i] = tracks[index].stream;
        }
        if (m_bridgeSubtitlePrepared[i]) continue;
        const bool replaced = previous != m_bridgeSubtitleStreams[i];
        rebuild |= replaced;
        if (auto text = dynamic_cast<CRenderedTextSubtitle*>(m_bridgeSubtitleStreams[i].p)) {
            CAutoLock lock(&m_csSubLock);
            auto next = Bridge::MakeSubtitleReadingCopy(*text);
            const auto& old = m_bridgeSubtitleSources[i];
            // NewSegment resets the decoder before redelivering cues. The
            // existing reading copy still has valid absolute media intervals;
            // keep those until a replacement arrives, including after seeks.
            if (!replaced && next->IsEmpty() && old) continue;
            bool same = !replaced && old && old->GetCount() == next->GetCount();
            for(size_t n=0;same && n<next->GetCount();++n) {
                const auto& a=old->GetAt(n); const auto& b=next->GetAt(n);
                same = a.start==b.start && a.end==b.end && a.str==b.str
                    && a.layer==b.layer && a.readorder==b.readorder && a.actor==b.actor;
            }
            if(!same) { m_bridgeSubtitleSources[i] = std::move(next); rebuild = true; }
        } else if (slot.embeddedTrack >= 0) {
            m_bridgeSubtitleSources[i].reset(); refreshPictures = true;
        }
    }
    if (!rebuild) {
        if (refreshPictures && m_pCAP) {
            m_pCAP->Invalidate();
            if (GetMediaState() != State_Running) { m_bridgeSubtitleRefreshFrames = 8; RepaintVideo(); }
        }
        return;
    }
    AttachBridgeSubtitles(m_pCurrentSubStream, true); RepaintVideo();
}
