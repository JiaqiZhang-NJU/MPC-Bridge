// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include <cmath>

namespace {
bool ChineseMemoryUI() {
    const auto lang = AfxGetAppSettings().iCurrentLanguage;
    return lang == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_SIMPLIFIED)
        || lang == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_TRADITIONAL);
}
std::string AudioKey(IAMStreamSelect* select, int index) {
    if (!select || index < 0) return {};
    LCID language = 0; DWORD group = 0; CComHeapPtr<WCHAR> name;
    AM_MEDIA_TYPE* type = nullptr;
    if (FAILED(select->Info(index, &type, nullptr, &language, &group, &name, nullptr, nullptr))) return {};
    CString guid; if (type) { guid = CStringFromGUID(type->subtype); DeleteMediaType(type); }
    return std::to_string(language) + "|" + std::to_string(group) + "|"
        + Bridge::MemoryUtf8(name ? std::wstring(name) : L"") + "|" + Bridge::MemoryUtf8(guid.GetString());
}
std::string SubtitleKey(ISubStream* stream, int index) {
    if (!stream || index < 0) return {};
    LCID language = 0; WCHAR* name = nullptr; CLSID clsid{};
    if (FAILED(stream->GetStreamInfo(index, &name, &language))) return {};
    stream->GetClassID(&clsid);
    const std::string key = std::to_string(language) + "|" + Bridge::MemoryUtf8(name ? std::wstring(name) : L"")
        + "|" + Bridge::MemoryUtf8(CStringFromGUID(clsid).GetString());
    CoTaskMemFree(name); return key;
}
enum { KeepHistory = 700, RememberPosition, RememberSettings, ClearAll };
class MemorySettingsDialog final : public CDialog {
    std::vector<WORD> layout;
    std::vector<std::unique_ptr<CWnd>> controls;
    void Text(const wchar_t* value) { do { layout.push_back(static_cast<WORD>(*value)); } while (*value++); }
    void Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, CRect rect) {
        MapDialogRect(rect);
        std::unique_ptr<CWnd> control;
        if (std::wstring(type) == L"BUTTON") control = std::make_unique<CButton>();
        else control = std::make_unique<CStatic>();
        control->CreateEx(0, type, text, WS_CHILD | WS_VISIBLE | style, rect, this, id);
        control->SetFont(GetFont()); controls.push_back(std::move(control));
    }
    void UpdateEnabled() {
        const bool enabled = IsDlgButtonChecked(KeepHistory) == BST_CHECKED;
        GetDlgItem(RememberPosition)->EnableWindow(enabled);
        GetDlgItem(RememberSettings)->EnableWindow(enabled);
    }
public:
    explicit MemorySettingsDialog(CWnd* parent) {
        layout.resize(sizeof(DLGTEMPLATE) / sizeof(WORD));
        auto dialog = reinterpret_cast<DLGTEMPLATE*>(layout.data());
        dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
        dialog->cx = 300; dialog->cy = 181;
        layout.push_back(0); layout.push_back(0);
        Text(ChineseMemoryUI() ? L"播放记忆" : L"Playback memory");
        layout.push_back(9); Text(L"Segoe UI");
        InitModalIndirect(reinterpret_cast<DLGTEMPLATE*>(layout.data()), parent);
    }
protected:
    BOOL OnInitDialog() override {
        CDialog::OnInitDialog(); const bool cn = ChineseMemoryUI(); const auto& s = AfxGetAppSettings();
        Control(L"BUTTON", cn ? L"保留历史记录" : L"Keep playback history", BS_AUTOCHECKBOX | WS_TABSTOP, KeepHistory, {10, 10, 290, 25});
        Control(L"BUTTON", cn ? L"记住播放进度，再次打开自动续播" : L"Remember position and resume when reopening", BS_AUTOCHECKBOX | WS_TABSTOP, RememberPosition, {20, 32, 290, 47});
        Control(L"BUTTON", cn ? L"记住每个视频的倍速、清晰度和字幕" : L"Remember speed, quality and subtitles per video", BS_AUTOCHECKBOX | WS_TABSTOP, RememberSettings, {20, 54, 290, 69});
        CheckDlgButton(KeepHistory, s.bKeepHistory); CheckDlgButton(RememberPosition, s.bRememberFilePos);
        CheckDlgButton(RememberSettings, s.bRememberVideoSettings); UpdateEnabled();
        Control(L"STATIC", cn ? L"音量沿用全局设置。关闭记忆不会删除旧记录。\n清理视频缓存不会清除播放记忆。"
            : L"Volume uses your global setting. Disabling memory keeps saved records. Clearing the media cache keeps playback memory.", 0, -1, {10, 79, 290, 112});
        Control(L"STATIC", cn ? L"记录保存在播放器配置目录，升级时保留。" : L"Records are kept with your player settings across upgrades.", 0, -1, {10, 116, 290, 140});
        Control(L"BUTTON", cn ? L"清除全部记录…" : L"Clear all records...", BS_PUSHBUTTON | WS_TABSTOP, ClearAll, {10, 153, 135, 170});
        Control(L"BUTTON", cn ? L"保存" : L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, {181, 153, 232, 170});
        Control(L"BUTTON", cn ? L"取消" : L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, {237, 153, 290, 170});
        return TRUE;
    }
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override {
        const auto id = LOWORD(wParam);
        if (id == IDOK) { OnOK(); return TRUE; }
        if (id == KeepHistory) { UpdateEnabled(); return TRUE; }
        if (id == ClearAll) {
            if (AfxMessageBox(ChineseMemoryUI() ? L"清除全部观看进度、逐视频设置和历史列表？" : L"Clear all positions, per-video settings and playback history?", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (AfxGetMyApp()->m_HistoryFile.Clear()) {
                    CComPtr<IApplicationDestinations> destinations;
                    if (SUCCEEDED(destinations.CoCreateInstance(CLSID_ApplicationDestinations))) destinations->RemoveAllDestinations();
                    AfxGetAppSettings().strLastOpenFile.Empty();
                    GetDlgItem(ClearAll)->SetWindowTextW(ChineseMemoryUI() ? L"已清除" : L"Cleared");
                }
                else AfxMessageBox(ChineseMemoryUI() ? L"清除失败，请检查配置目录。" : L"Could not clear records. Check the settings folder.", MB_ICONWARNING);
            }
            return TRUE;
        }
        return CDialog::OnCommand(wParam, lParam);
    }
    void OnOK() override {
        auto& s = AfxGetAppSettings();
        s.bKeepHistory = IsDlgButtonChecked(KeepHistory) == BST_CHECKED;
        s.bRememberFilePos = IsDlgButtonChecked(RememberPosition) == BST_CHECKED;
        s.bRememberVideoSettings = IsDlgButtonChecked(RememberSettings) == BST_CHECKED;
        s.SaveSettings(); AfxGetProfile().Flush(false); CDialog::OnOK();
    }
};
}

void CMainFrame::OnBridgeMemorySettings() {
    CancelBridgeHold(); CancelLeftLongPressSpeed(true);
    MemorySettingsDialog dialog(GetModalParent()); dialog.DoModal();
}

void CMainFrame::BeginBridgePlaybackMemory(const OpenFileData* ofd) {
    if (m_bridgeQualitySwitch.pending) return;
    m_bridgeMemory = {};
    m_bridgeMemory.session = Bridge::MemoryTimestamp();
    m_bridgeMemory.globalSubtitles = AfxGetAppSettings().fEnableSubtitles;
    if (ofd) m_bridgeMemory.explicitStart = ofd->rtStart;
}

void CMainFrame::PrepareBridgePlaybackMemory(OpenFileData& ofd, const CStringW& source) {
    if (m_bridgeQualitySwitch.pending || m_fOpeningAborted || m_YtDlp.mIsLive || m_bIsBDPlay) return;
    auto& memory = m_bridgeMemory;
    auto& s = AfxGetAppSettings();
    const auto url = Bridge::MemoryUtf8(source.GetString());
    const bool online = PathIsURLW(source) != FALSE;
    const auto key = online ? Bridge::OnlineMemoryKey(url, Bridge::MemoryUtf8(m_YtDlp.mResolvedId.GetString()))
        : Bridge::LocalMemoryKey(source.GetString());
    if (key.empty()) return;
    CString directory;
    if (!AfxGetMyApp()->GetAppSavePath(directory)) return;
    memory.supported = true; memory.directory = directory.GetString();
    // Source is independent of extractor changes, and is also used by history deletion.
    const auto sourceKey = Bridge::MemorySource(source.GetString());
    memory.record.key = key; memory.record.source = sourceKey;
    if (online && memory.explicitStart == INVALID_TIME) {
        const auto start = Bridge::OnlineStartMs(url);
        if (start) memory.explicitStart = *start * 10000;
    }
    if (s.bKeepHistory) {
        const auto saved = Bridge::MemoryStore().Read(memory.directory, key, sourceKey);
        if (saved.status == Bridge::MemoryReadStatus::Found) { memory.record = saved.record; memory.found = true; }
        else if (saved.status == Bridge::MemoryReadStatus::Missing && online && s.bRememberFilePos) {
            // Old history cannot verify a local file's size/mtime. Import only an
            // exact webpage URL; supported URLs retain case-sensitive IDs.
            std::vector<SessionInfo> recent;
            AfxGetMyApp()->m_HistoryFile.GetRecentSessions(recent, 900);
            for (const auto& entry : recent) if (entry.Path == source) {
                memory.record.positionMs = entry.Position / 10000;
                memory.record.hasPosition = entry.Position > 0; memory.found = memory.record.hasPosition; break;
            }
        }
    }
    memory.record.session = memory.session;
    const auto& record = memory.record;
    if (s.bKeepHistory && s.bRememberVideoSettings && record.hasSettings && m_YtDlp.GetVFormat()) {
        UINT selected = UINT_MAX;
        if (!record.qualityAutomatic) {
            const auto formats = m_YtDlp.GetVideoFormats();
            for (UINT i = 0; i < formats.size(); ++i) {
                const auto& f = formats[i];
                if (f.id.GetString() == record.qualityFormat && f.height == record.qualityHeight
                    && f.codec == record.qualityCodec && f.fps == record.qualityFps) { selected = i; break; }
            }
            if (selected == UINT_MAX) selected = m_YtDlp.GetHeightIndex(record.qualityHeight);
            if (selected == UINT_MAX) for (const int height : m_YtDlp.GetVideoHeights()) {
                if (height <= record.qualityHeight && (selected == UINT_MAX || formats[selected].height < height)) selected = m_YtDlp.GetHeightIndex(height);
            }
        }
        if (selected != UINT_MAX) m_YtDlp.ChangeFormats(selected);
        m_bridgeQualityAutomatic = record.qualityAutomatic;
    }
    // Never apply a disk position here. Restore once, after tracks and the UI
    // are ready; this also prevents the legacy history seek from winning.
    if (memory.explicitStart != INVALID_TIME) ofd.rtStart = memory.explicitStart;
}

void CMainFrame::RestoreBridgePlaybackMemory() {
    auto& memory = m_bridgeMemory;
    if (!memory.supported || m_eMediaLoadState != MLS_LOADED || !m_pMS || m_bridgeQualitySwitch.pending) return;
    auto& s = AfxGetAppSettings(); const auto record = memory.record;
    memory.applying = true;
    CString warning;
    if (s.bKeepHistory && s.bRememberVideoSettings && record.hasSettings) {
        if (SUCCEEDED(m_pMS->SetRate(record.rate))) m_PlaybackRate = record.rate;
        else warning = ChineseMemoryUI() ? L"此视频未能恢复倍速。" : L"Playback speed could not be restored.";
        if (!record.audioKey.empty()) {
            CComQIPtr<IAMStreamSelect> select = m_pSwitcherFilter.p;
            DWORD count = 0; int match = -1;
            if (select && SUCCEEDED(select->Count(&count))) for (DWORD i = 0; i < count; ++i) {
                if (AudioKey(select, i) == record.audioKey) { if (match >= 0) { match = -1; break; } match = i; }
            }
            memory.degradedAudio = match < 0 || FAILED(select->Enable(match, AMSTREAMSELECTENABLE_ENABLE));
        }
        bool restored = false;
        if (record.dual.enabled || !record.dual.slots[0].path.empty() || record.dual.slots[0].embeddedTrack >= 0) {
            CString error;
            restored = ApplyBridgeSubtitles(record.dual, error, false);
            if (!restored) {
                // Recover either usable lane without overwriting the saved pair.
                for (int i = 0; i < 2 && !restored; ++i) {
                    auto single = record.dual; single.enabled = false; single.slots[0] = record.dual.slots[i];
                    restored = ApplyBridgeSubtitles(single, error, false);
                }
                memory.degradedSubtitles = true;
                warning = ChineseMemoryUI() ? L"部分字幕已不可用，已保留原配置。" : L"Some subtitles are unavailable. Saved settings were kept.";
            }
        } else if (!record.subtitlePath.empty() || !record.subtitleKey.empty()) {
            if (!record.subtitlePath.empty()) {
                CComPtr<ISubStream> stream;
                if (LoadSubtitle(Bridge::MemoryWide(record.subtitlePath).c_str(), &stream, false)) { SetSubtitle(stream); restored = true; }
            } else {
                CComPtr<ISubStream> match; int selected = -1; bool ambiguous = false;
                for (const auto& stream : m_pSubStreams) for (int i = 0; i < stream->GetStreamCount(); ++i) {
                    if (SubtitleKey(stream, i) == record.subtitleKey) {
                        if (match) ambiguous = true;
                        match = stream; selected = i;
                    }
                }
                if (match && !ambiguous) { match->SetStream(selected); SetSubtitle(match); restored = true; }
            }
            if (restored && m_pCAP) m_pCAP->SetSubtitleDelay(record.subtitleDelayMs);
            memory.degradedSubtitles = !restored;
        }
        s.fEnableSubtitles = record.subtitlesVisible && !memory.degradedSubtitles;
        // A valid single lane remains visible when its partner is missing.
        if (restored) s.fEnableSubtitles = record.subtitlesVisible;
        if (!s.fEnableSubtitles) { m_nSelSub2 = m_iSubtitleSel; m_iSubtitleSel = -1; }
        else if (m_iSubtitleSel < 0 && restored) m_iSubtitleSel = m_nSelSub2;
        UpdateSubtitle();
    }
    REFERENCE_TIME duration = 0;
    m_pMS->GetDuration(&duration);
    REFERENCE_TIME target = memory.explicitStart;
    bool fromHistory = false;
    if (target == INVALID_TIME && s.bKeepHistory && s.bRememberFilePos && record.hasPosition) {
        target = record.completed ? 0 : record.positionMs * 10000; fromHistory = target > 0;
    }
    if (target != INVALID_TIME && duration > 0) {
        if (target >= duration) { target = 0; warning = ChineseMemoryUI() ? L"保存的位置已失效，已从头播放。" : L"Saved position is no longer valid. Playing from the beginning."; }
        if (SUCCEEDED(m_pMS->SetPositions(&target, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning))) {
            if (fromHistory && target > 0) {
                CString text; text.Format(ChineseMemoryUI() ? L"已从 %02lld:%02lld 继续播放" : L"Resumed from %02lld:%02lld", target / UNITS / 60, target / UNITS % 60);
                m_OSD.DisplayMessage(OSD_TOPLEFT, text, 4000);
            }
        } else {
            memory.degradedPosition = true;
            warning = ChineseMemoryUI() ? L"未能恢复播放位置，原记录已保留。" : L"Could not restore playback position. The saved record was kept.";
        }
    }
    memory.applying = false; memory.ready = true; memory.lastSave = GetTickCount64();
    if (!warning.IsEmpty()) m_OSD.DisplayMessage(OSD_TOPLEFT, warning, 5000);
}

void CMainFrame::SaveBridgePlaybackMemory(bool force) {
    if (GetCurrentThreadId() != AfxGetApp()->m_nThreadID) {
        PostMessageW(WM_COMMAND, ID_BRIDGE_MEMORY_SAVE);
        return;
    }
    auto& memory = m_bridgeMemory; const auto& s = AfxGetAppSettings();
    if (Bridge::MemoryStore().TakeError()) m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseMemoryUI()
        ? L"播放记录暂时无法保存或读取，请检查配置目录权限和磁盘空间。" : L"Playback memory is unavailable. Check the settings folder and free disk space.", 5000);
    if (!memory.ready || memory.applying || !s.bKeepHistory || m_bridgeQualitySwitch.pending
        || m_eMediaLoadState != MLS_LOADED || !m_pMS || GetPlaybackMode() != PM_FILE) return;
    const auto now = GetTickCount64();
    if (!force && now - memory.lastSave < 5000) return;
    memory.lastSave = now;
    CString currentDirectory;
    if (AfxGetMyApp()->GetAppSavePath(currentDirectory)) memory.directory = currentDirectory.GetString();
    auto next = memory.record;
    next.session = memory.session; next.updated = Bridge::MemoryTimestamp();
    REFERENCE_TIME pos = 0, duration = 0; DWORD capabilities = 0;
    const bool position = s.bRememberFilePos && !memory.degradedPosition && SUCCEEDED(m_pMS->GetDuration(&duration)) && duration > 0
        && SUCCEEDED(m_pMS->GetCurrentPosition(&pos)) && SUCCEEDED(m_pMS->GetCapabilities(&capabilities))
        && (capabilities & AM_SEEKING_CanSeekAbsolute);
    if (position && !memory.stopped) {
        next.positionMs = std::max<REFERENCE_TIME>(0, pos) / 10000; next.durationMs = duration / 10000;
        next.completed = m_bEndOfStream || m_bGraphEventComplete; next.hasPosition = true;
    }
    if (s.bRememberVideoSettings) {
        double rate = m_PlaybackRate;
        if (m_bridgeHoldRateActive) rate = m_bridgeHoldPreviousRate;
        else if (m_bLeftLongPressSpeedActive) rate = m_leftLongPressSpeedPreviousRate;
        if (std::isfinite(rate) && rate >= .125 && rate <= 16) next.rate = rate;
        // Preserve selection intent when a reopened resource needed a fallback.
        if (!memory.found || !next.hasSettings) {
            next.qualityAutomatic = m_bridgeQualityAutomatic;
            if (const auto format = m_YtDlp.GetVFormat()) {
                next.qualityHeight = format->height; next.qualityCodec = format->codec;
                next.qualityFps = format->fps; next.qualityFormat = format->id.GetString();
            }
        }
        if (!memory.degradedAudio) { CComQIPtr<IAMStreamSelect> select = m_pSwitcherFilter.p; next.audioKey = AudioKey(select, GetAudioTrackIdx()); }
        if (!memory.degradedSubtitles) {
            next.dual = m_bridgeSubtitles; next.subtitlesVisible = s.fEnableSubtitles;
            next.subtitleDelayMs = GetBridgePrimarySubtitleDelay();
            next.subtitleKey.clear(); next.subtitlePath.clear();
            CComPtr<ISubStream> stream = m_pCurrentSubStream;
            if (!next.dual.enabled && stream != m_bridgeSubtitleStreams[0]) next.dual = {};
            if (stream) {
                next.subtitleKey = SubtitleKey(stream, stream->GetStream());
                if (std::find(m_ExternalSubstreams.begin(), m_ExternalSubstreams.end(), stream.p) != m_ExternalSubstreams.end()) {
                    if (auto text = dynamic_cast<CRenderedTextSubtitle*>(stream.p)) next.subtitlePath = Bridge::MemoryUtf8(text->m_path.GetString());
                }
            }
        }
        next.hasSettings = true;
    }
    memory.record = next;
    Bridge::MemoryStore().Save(memory.directory, std::move(next), s.nHistoryEntriesMax, position, s.bRememberVideoSettings);
}

void CMainFrame::AddBridgeMemoryMenu(CMenu& root) {
    for (int i = 0; i < root.GetMenuItemCount(); ++i) {
        auto sub = root.GetSubMenu(i); if (sub && sub->GetMenuItemID(0) == ID_BRIDGE_MEMORY_SETTINGS) return;
    }
    CMenu menu; if (!menu.CreatePopupMenu()) return;
    const bool cn = ChineseMemoryUI();
    menu.AppendMenuW(MF_STRING, ID_BRIDGE_MEMORY_SETTINGS, cn ? L"播放记忆设置…" : L"Playback memory settings...");
    menu.AppendMenuW(MF_STRING, ID_BRIDGE_MEMORY_RESTART, cn ? L"从头播放" : L"Play from beginning");
    menu.AppendMenuW(MF_STRING, ID_BRIDGE_MEMORY_CLEAR, cn ? L"清除此视频的播放记录" : L"Forget this video");
    if (root.AppendMenuW(MF_POPUP, reinterpret_cast<UINT_PTR>(menu.GetSafeHmenu()), cn ? L"播放记忆" : L"Playback memory")) menu.Detach();
}
void CMainFrame::OnUpdateBridgeMemory(CCmdUI* command) {
    command->Enable(m_bridgeMemory.ready && m_eMediaLoadState == MLS_LOADED && !m_bridgeQualitySwitch.pending);
}
void CMainFrame::OnBridgeMemoryRestart() {
    if (!m_bridgeMemory.ready || !m_pMS) return;
    m_bridgeMemory.stopped = false;
    SeekTo(0); SaveBridgePlaybackMemory(true);
}
void CMainFrame::OnBridgeMemoryClear() {
    if (!m_bridgeMemory.ready) return;
    std::list<SessionInfo> entries{m_SessionInfo};
    if (!AfxGetMyApp()->m_HistoryFile.DeleteSessions(entries)) {
        m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseMemoryUI() ? L"播放记录清除失败，请检查配置目录。" : L"Could not clear playback memory. Check the settings folder.", 5000);
        return;
    }
    m_bridgeMemory.ready = false; // don't regenerate until explicitly reopened
    m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseMemoryUI() ? L"已清除此视频的播放记录。" : L"Playback memory for this video was cleared.", 3000);
}
