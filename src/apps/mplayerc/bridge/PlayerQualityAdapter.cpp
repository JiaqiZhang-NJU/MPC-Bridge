// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include "CacheSettingsProfile.h"
#include "DSUtil/HTTPAsync.h"
#include <cmath>

namespace {
bool ChineseUI() {
    const auto language = AfxGetAppSettings().iCurrentLanguage;
    return language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_SIMPLIFIED)
        || language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_TRADITIONAL);
}
}

void CMainFrame::AddBridgeQualityMenu(CMenu& root)
{
    AddBridgeMemoryMenu(root);
    for (int i = 0; i < root.GetMenuItemCount(); ++i) {
        auto sub = root.GetSubMenu(i);
        if (sub && sub->GetMenuItemID(0) == ID_BRIDGE_QUALITY_DEFAULT) return;
    }
    CMenu menu; if (!menu.CreatePopupMenu()) return;
    menu.AppendMenuW(MF_STRING, ID_BRIDGE_QUALITY_DEFAULT, L"Default");
    // The parent owns its popup, including after language/theme menu rebuilds.
    if (root.AppendMenuW(MF_POPUP, reinterpret_cast<UINT_PTR>(menu.GetSafeHmenu()), ChineseUI() ? L"清晰度" : L"Quality")) menu.Detach();
}

void CMainFrame::SetupBridgeQualityMenu(CMenu& menu)
{
    while (menu.DeleteMenu(0, MF_BYPOSITION)) {}
    m_bridgeQualityCommands.clear();
    const bool available = (m_eMediaLoadState == MLS_LOADED || m_bridgeQualitySwitch.pending) && m_YtDlp.GetVFormat();
    CString text;
    text.Format(ChineseUI() ? L"按默认设置（优先 %dp）" : L"Default settings (up to %dp)", AfxGetAppSettings().iYdlMaxHeight);
    menu.AppendMenuW(MF_STRING | (available ? 0 : MF_GRAYED) | (m_bridgeQualityAutomatic ? MF_CHECKED : 0), ID_BRIDGE_QUALITY_DEFAULT, text);
    if (available) {
        const UINT current = m_bridgeQualitySwitch.pending ? m_bridgeQualitySwitch.oldIndex : m_YtDlp.GetSelectedIndex();
        const auto formats = m_YtDlp.GetVideoFormats();
        const int height = current < formats.size() ? formats[current].height : 0;
        UINT command = ID_BRIDGE_QUALITY_START;
        for (int candidate : m_YtDlp.GetVideoHeights()) {
            const auto index = m_YtDlp.GetHeightIndex(candidate);
            if (index == UINT_MAX || command >= ID_BRIDGE_QUALITY_START + 128) continue;
            text.Format(L"%dp", candidate);
            menu.AppendMenuW(MF_STRING | (height == candidate ? MF_CHECKED : 0), command, text);
            m_bridgeQualityCommands[command++] = index;
        }
        CMenu more; more.CreatePopupMenu();
        command = ID_BRIDGE_QUALITY_START + 128;
        for (UINT index = 0; index < formats.size() && command <= ID_BRIDGE_QUALITY_END; ++index) {
            const auto& format = formats[index];
            if (format.height <= 0) continue;
            text.Format(L"%s  [ID %hs]", format.desc.GetString(), format.id.GetString());
            more.AppendMenuW(MF_STRING | (current == index ? MF_CHECKED : 0), command, text);
            m_bridgeQualityCommands[command++] = index;
        }
        if (menu.AppendMenuW(MF_POPUP, reinterpret_cast<UINT_PTR>(more.GetSafeHmenu()), ChineseUI() ? L"更多格式" : L"More formats")) more.Detach();
        if (m_bridgeQualitySwitch.pending) {
            const auto target = m_bridgeQualitySwitch.targetIndex;
            text.Format(ChineseUI() ? L"正在切换到 %dp…" : L"Switching to %dp...", target < formats.size() ? formats[target].height : 0);
            menu.AppendMenuW(MF_STRING | MF_GRAYED, ID_BRIDGE_QUALITY_STATUS, text);
        }
    } else menu.AppendMenuW(MF_STRING | MF_GRAYED, ID_BRIDGE_QUALITY_STATUS, ChineseUI() ? L"加载在线视频后可选择" : L"Open an online video to choose quality");
    menu.AppendMenuW(MF_SEPARATOR);
    menu.AppendMenuW(MF_STRING, ID_BRIDGE_CACHE_SETTINGS, ChineseUI() ? L"缓存与预加载设置…" : L"Cache and preload settings...");
}

void CMainFrame::OnBridgeQualitySelected(UINT id)
{
    if (m_eMediaLoadState != MLS_LOADED && !m_bridgeQualitySwitch.pending) return;
    const bool automatic = id == ID_BRIDGE_QUALITY_DEFAULT;
    const auto found = m_bridgeQualityCommands.find(id);
    const UINT index = automatic ? m_YtDlp.GetDefaultVideoIndex() : found != m_bridgeQualityCommands.end() ? found->second : UINT_MAX;
    if (index != UINT_MAX) {
        auto& record = m_bridgeMemory.record;
        record.qualityAutomatic = automatic;
        const auto formats = m_YtDlp.GetVideoFormats();
        if (index < formats.size()) {
            const auto& format = formats[index]; record.qualityFormat = format.id.GetString();
            record.qualityHeight = format.height; record.qualityCodec = format.codec; record.qualityFps = format.fps;
        }
        SwitchBridgeQuality(index, automatic);
        SaveBridgePlaybackMemory(true);
    }
}

void CMainFrame::OnUpdateBridgeQuality(CCmdUI* command)
{
    command->Enable((m_eMediaLoadState == MLS_LOADED || m_bridgeQualitySwitch.pending) && m_YtDlp.GetVFormat());
}

void CMainFrame::SwitchBridgeQuality(UINT index, bool automatic, bool force)
{
    auto& change = m_bridgeQualitySwitch;
    if (change.pending) { change.queuedIndex = index; change.queuedAutomatic = automatic; return; }
    if (m_eMediaLoadState != MLS_LOADED || !m_pMS || index >= m_YtDlp.GetFormatsCount()) return;
    CancelBridgeHold(); CancelLeftLongPressSpeed(true);
    if (!force && index == m_YtDlp.GetSelectedIndex()) { m_bridgeQualityAutomatic = automatic; return; }
    change = {};
    change.oldIndex = m_YtDlp.GetSelectedIndex(); change.targetIndex = index;
    change.automatic = automatic; change.oldAutomatic = m_bridgeQualityAutomatic;
    m_pMS->GetCurrentPosition(&change.position);
    m_pMS->GetRate(&change.rate);
    if (!std::isfinite(change.rate) || change.rate <= 0) change.rate = 1;
    change.state = GetMediaState(); change.volume = GetVolume(); change.muted = IsMuted();
    change.subtitle = GetSubtitleTrackIdx(); change.audio = GetAudioTrackIdx();
    change.subtitles = AfxGetAppSettings().fEnableSubtitles;
    change.dualSubtitles = m_bridgeSubtitles;
    change.pending = true;
    change.internalClose = true; m_bYoutubeOpened = true;
    CloseMedia(); change.internalClose = false;
    if (!change.pending) return; // a reentrant explicit close cancelled this operation
    if (!m_YtDlp.ChangeFormats(index)) { change.pending = false; return; }
    if (!OpenCurPlaylistItem(change.position, FALSE)) FinishBridgeQualitySwitch(false);
}

bool CMainFrame::FinishBridgeQualitySwitch(bool success)
{
    auto& change = m_bridgeQualitySwitch;
    if (!change.pending) return false;
    if (!success && !change.rollback) {
        change.rollback = true; change.internalClose = true; m_bYoutubeOpened = true;
        CloseMedia(); change.internalClose = false;
        if (!change.pending) return true;
        m_YtDlp.ChangeFormats(change.oldIndex);
        if (OpenCurPlaylistItem(change.position, FALSE)) return true;
    }
    const auto saved = change; change.pending = false; change.queuedIndex = UINT_MAX;
    if (success && m_pMS) {
        m_bridgeQualityAutomatic = saved.rollback ? saved.oldAutomatic : saved.automatic;
        m_PlaybackRate = SUCCEEDED(m_pMS->SetRate(saved.rate)) ? saved.rate : 1.0;
        m_wndToolBar.SetVolume(saved.volume);
        if (IsMuted() != saved.muted) SendMessageW(WM_COMMAND, ID_VOLUME_MUTE);
        OnPlayVolume(ID_VOLUME_UP);
        SetAudioTrackIdx(saved.audio); SetSubtitleTrackIdx(saved.subtitle);
        AfxGetAppSettings().fEnableSubtitles = saved.subtitles;
        UpdateSubtitle();
        if (saved.dualSubtitles.enabled) {
            CString error;
            if (!ApplyBridgeSubtitles(saved.dualSubtitles, error)) {
                m_bridgeSubtitles.enabled = false; UpdateSubtitle();
                m_OSD.DisplayMessage(OSD_TOPLEFT, error, 5000);
            }
            AfxGetAppSettings().fEnableSubtitles = saved.subtitles;
            if (!saved.subtitles) { m_nSelSub2 = m_iSubtitleSel; m_iSubtitleSel = -1; UpdateSubtitle(); }
        }
        if (saved.state == State_Paused) OnPlayPause();
        else if (saved.state == State_Stopped) OnPlayStop();
        if (saved.rollback) m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseUI() ? L"切换失败，已恢复原清晰度。" : L"Switch failed; previous quality restored.", 5000);
        if (saved.queuedIndex != UINT_MAX && saved.queuedIndex != m_YtDlp.GetSelectedIndex()) SwitchBridgeQuality(saved.queuedIndex, saved.queuedAutomatic);
    } else m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseUI() ? L"清晰度切换及恢复失败，请重新打开视频。" : L"Quality switch and recovery failed. Reopen the video.", 5000);
    return !success;
}

void CMainFrame::AttachBridgeCache(OpenFileData& ofd)
{
    { std::lock_guard<std::mutex> lock(m_bridgeCacheMutex); if (m_bridgeCache) m_bridgeCache->Select({}); }
    const auto video = m_YtDlp.GetVFormat(); const auto audio = m_YtDlp.GetAFormat();
    if (m_bridgeCacheBypassSession || !video || m_YtDlp.mIsLive || video->ext != "mp4" || video->protocol == YT_DLP::protocol_hls
        || (audio && audio->ext != "m4a" && audio->ext != "mp4")) return;
    const auto settings = Bridge::ReadCacheSettings(); if (!settings.enabled) return;
    std::shared_ptr<Bridge::MediaCache> cache;
    { std::lock_guard<std::mutex> lock(m_bridgeCacheMutex);
        if (!m_bridgeCache) m_bridgeCache = std::make_shared<Bridge::MediaCache>(settings);
        cache = m_bridgeCache;
    }
    std::vector<std::wstring> routes;
    auto agent = m_YtDlp.mUserAgent.IsEmpty() ? http::userAgent : m_YtDlp.mUserAgent;
    const auto videoRoute = cache->Register(ofd.fi.GetPath().GetString(), agent.GetString(), m_YtDlp.mReferer.GetString());
    if (videoRoute.empty()) return;
    routes.push_back(videoRoute);
    for (auto& aud : ofd.auds) {
        const auto route = cache->Register(aud.GetPath().GetString(), agent.GetString(), m_YtDlp.mReferer.GetString());
        if (route.empty()) { cache->Select({}); return; }
        routes.push_back(route);
    }
    // Keep RenderedPath and playlist/page identity original; only graph inputs use local routes.
    ofd.fi = CStringW(videoRoute.c_str());
    std::size_t index = 1; for (auto& aud : ofd.auds) aud = CStringW(routes[index++].c_str());
    cache->Prepare(routes);
    cache->Select(routes);
    cache->Update(ofd.rtStart > 0 ? ofd.rtStart / 1e7 : 0, 1, false);
}

void CMainFrame::CloseBridgeSession()
{
    if (m_bridgeQualitySwitch.internalClose && !m_bClosingState) {
        std::shared_ptr<Bridge::MediaCache> cache;
        { std::lock_guard<std::mutex> lock(m_bridgeCacheMutex); cache = m_bridgeCache; }
        if (cache) cache->CancelReaders();
        return;
    }
    m_bYoutubeOpened = false; // an explicit close cancels the switch instead of preserving its metadata
    m_bridgeQualitySwitch = {}; m_bridgeQualityAutomatic = true;
    m_bridgeCacheBypassSession = true; // prevent an aborted opening thread from creating another cache
    std::shared_ptr<Bridge::MediaCache> cache;
    { std::lock_guard<std::mutex> lock(m_bridgeCacheMutex); cache = std::move(m_bridgeCache); m_bridgeCacheStatus = {}; }
    if (cache) cache->Cancel(); // readers must wake before CloseMedia waits for the graph thread
    m_wndSeekBar.SetBridgeCache(0, 0);
}

void CMainFrame::UpdateBridgeCache()
{
    std::shared_ptr<Bridge::MediaCache> cache;
    { std::lock_guard<std::mutex> lock(m_bridgeCacheMutex); cache = m_bridgeCache; }
    if (m_eMediaLoadState != MLS_LOADED || !m_pMS) return;
    if (!cache) {
        if (m_YtDlp.GetVFormat()) m_wndInfoBar.SetLine(ChineseUI() ? L"在线缓存" : L"Online cache",
            Bridge::ReadCacheSettings().enabled ? (ChineseUI() ? L"此资源暂不支持缓存" : L"Cache unavailable for this resource")
                : (ChineseUI() ? L"缓存已关闭" : L"Cache disabled"));
        return;
    }
    REFERENCE_TIME position = 0; double rate = 1;
    if (FAILED(m_pMS->GetCurrentPosition(&position))) return;
    m_pMS->GetRate(&rate);
    cache->Update(position / 1e7, rate, GetMediaState() == State_Paused);
    const auto status = cache->Status();
    if (status.failed && !m_bridgeCacheBypassSession && !m_bridgeQualitySwitch.pending) {
        m_bridgeCacheBypassSession = true;
        cache->Cancel();
        SwitchBridgeQuality(m_YtDlp.GetSelectedIndex(), m_bridgeQualityAutomatic, true);
        m_OSD.DisplayMessage(OSD_TOPLEFT, ChineseUI() ? L"缓存读取失败，已切回直接播放。" : L"Cache read failed; switching to direct playback.", 5000);
        return;
    }
    { std::lock_guard<std::mutex> lock(m_bridgeCacheMutex); m_bridgeCacheStatus = status; }
    CString text;
    if (m_bridgeCacheBypassSession) text = ChineseUI() ? L"缓存不可用，使用直接播放" : L"Cache unavailable; direct playback";
    else if (status.active) {
        if (status.forwardSeconds >= 0) text.Format(ChineseUI() ? L"前方已缓存 %.0f 秒" : L"%.0f seconds cached ahead", status.forwardSeconds);
        else text.Format(ChineseUI() ? L"已缓存 %.1f MiB · 时长未知" : L"%.1f MiB cached; duration unknown", status.bytes / 1048576.0);
        if (status.capped) text += ChineseUI() ? L"（已达容量限制）" : L" (capacity limit)";
    } else text = ChineseUI() ? L"此资源暂不支持缓存" : L"Cache unavailable for this resource";
    m_wndInfoBar.SetLine(ChineseUI() ? L"在线缓存" : L"Online cache", text);
    m_wndSeekBar.SetBridgeCache(position, status.forwardSeconds >= 0 ? static_cast<REFERENCE_TIME>(status.forwardSeconds * 1e7) : 0);
}
