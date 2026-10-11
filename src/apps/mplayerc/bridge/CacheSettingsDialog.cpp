// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MainFrm.h"
#include "CacheSettingsProfile.h"
#include <cstring>

namespace {
bool ChineseUI() {
    const auto language = AfxGetAppSettings().iCurrentLanguage;
    return language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_SIMPLIFIED)
        || language == CMPlayerCApp::GetLanguageIndex(ID_LANGUAGE_CHINESE_TRADITIONAL);
}
enum { Enabled = 200, Forward, Back, Capacity, Directory, Browse, Clear };
class CacheSettingsDialog final : public CDialog {
    std::vector<WORD> layout;
    std::vector<std::unique_ptr<CWnd>> controls;
    std::shared_ptr<Bridge::MediaCache> cache;
    void String(const wchar_t* text) { do { layout.push_back(static_cast<WORD>(*text)); } while (*text++); }
    void Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, CRect rect) {
        MapDialogRect(rect);
        // Use the matching MFC control subclass so in-process edit messages reach EDIT's procedure.
        std::unique_ptr<CWnd> control;
        if (std::wstring(type) == L"EDIT") control = std::make_unique<CEdit>();
        else if (std::wstring(type) == L"BUTTON") control = std::make_unique<CButton>();
        else control = std::make_unique<CStatic>();
        control->CreateEx(type == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, text,
            WS_CHILD | WS_VISIBLE | style, rect, this, id);
        control->SetFont(GetFont()); controls.push_back(std::move(control));
    }
public:
    CacheSettingsDialog(CWnd* parent, std::shared_ptr<Bridge::MediaCache> session) : CDialog(), cache(std::move(session)) {
        layout.resize(sizeof(DLGTEMPLATE) / sizeof(WORD));
        auto dialog = reinterpret_cast<DLGTEMPLATE*>(layout.data());
        dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
        dialog->cx = 290; dialog->cy = 206;
        layout.push_back(0); layout.push_back(0);
        String(ChineseUI() ? L"缓存与预加载" : L"Cache and preload");
        layout.push_back(9); String(L"Segoe UI");
        InitModalIndirect(reinterpret_cast<DLGTEMPLATE*>(layout.data()), parent);
    }
protected:
    BOOL OnInitDialog() override {
        CDialog::OnInitDialog();
        const bool cn = ChineseUI(); const auto settings = Bridge::ReadCacheSettings();
        Control(L"BUTTON", cn ? L"启用在线视频缓存与预加载" : L"Enable online cache and preload", BS_AUTOCHECKBOX | WS_TABSTOP, Enabled, {10, 9, 280, 24});
        CheckDlgButton(Enabled, settings.enabled ? BST_CHECKED : BST_UNCHECKED);
        Control(L"STATIC", cn ? L"前方预加载（秒）" : L"Preload ahead (seconds)", 0, -1, {10, 31, 157, 45});
        Control(L"EDIT", L"", ES_NUMBER | WS_TABSTOP, Forward, {175, 30, 268, 44});
        SetDlgItemInt(Forward, settings.forwardSeconds);
        Control(L"STATIC", cn ? L"保留回退范围（秒）" : L"Keep behind (seconds)", 0, -1, {10, 50, 157, 64});
        Control(L"EDIT", L"", ES_NUMBER | WS_TABSTOP, Back, {175, 49, 268, 63});
        SetDlgItemInt(Back, settings.backSeconds);
        Control(L"STATIC", cn ? L"本视频缓存上限（MiB）" : L"Cache limit per video (MiB)", 0, -1, {10, 69, 170, 83});
        Control(L"EDIT", L"", ES_NUMBER | WS_TABSTOP, Capacity, {175, 68, 268, 82});
        SetDlgItemInt(Capacity, static_cast<UINT>(settings.diskBytes / 1048576));
        Control(L"STATIC", cn ? L"缓存目录（留空使用默认目录）" : L"Cache folder (empty uses the default)", 0, -1, {10, 90, 280, 104});
        Control(L"EDIT", settings.directory.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, Directory, {10, 105, 227, 120});
        Control(L"BUTTON", cn ? L"浏览…" : L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, Browse, {231, 105, 280, 121});
        CString status;
        const auto usage = cache ? cache->Status().bytes / 1048576.0 : 0;
        status.Format(cn ? L"当前占用 %.1f MiB；代理沿用 Windows 设置。" : L"Using %.1f MiB. Uses your Windows proxy settings.", usage);
        Control(L"STATIC", status, 0, -1, {10, 128, 280, 143});
        Control(L"STATIC", cn ? L"修改设置后，下次打开在线视频生效。关闭视频时自动清理缓存。倍速播放会增加预加载范围，仍受容量限制。"
            : L"Changes apply to the next online video. Closing the video clears its cache. Faster playback increases preloading within the limit.", 0, -1, {10, 145, 280, 178});
        Control(L"BUTTON", cn ? L"清理当前缓存" : L"Clear current cache", BS_PUSHBUTTON | WS_TABSTOP, Clear, {10, 185, 118, 199});
        GetDlgItem(Clear)->EnableWindow(cache != nullptr);
        Control(L"BUTTON", cn ? L"保存" : L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, {175, 185, 225, 199});
        Control(L"BUTTON", cn ? L"取消" : L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, {230, 185, 280, 199});
        return TRUE;
    }
    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override {
        // Runtime-created controls need an explicit save route, including keyboard IDOK.
        if (LOWORD(wParam) == IDOK && HIWORD(wParam) == BN_CLICKED) { OnOK(); return TRUE; }
        if (LOWORD(wParam) == Browse) {
            CFolderPickerDialog picker(nullptr, 0, this);
            if (picker.DoModal() == IDOK) SetDlgItemTextW(Directory, picker.GetPathName());
            return TRUE;
        }
        if (LOWORD(wParam) == Clear) {
            if (cache) cache->Clear();
            GetDlgItem(Clear)->SetWindowTextW(ChineseUI() ? L"已清理" : L"Cleared");
            return TRUE;
        }
        return CDialog::OnCommand(wParam, lParam);
    }
    void OnOK() override {
        BOOL validForward, validBack, validCapacity;
        const auto forward = GetDlgItemInt(Forward, &validForward);
        const auto back = GetDlgItemInt(Back, &validBack);
        const auto capacity = GetDlgItemInt(Capacity, &validCapacity);
        if (!validForward || !validBack || !validCapacity || forward < 1 || forward > 120 || back > 120 || capacity < 16 || capacity > 256) {
            AfxMessageBox(ChineseUI() ? L"前方秒数须为 1–120，回退秒数须为 0–120，容量须为 16–256 MiB。"
                : L"Ahead: 1–120 seconds. Behind: 0–120 seconds. Capacity: 16–256 MiB.", MB_ICONWARNING); return;
        }
        CString directory; GetDlgItemTextW(Directory, directory); directory.Trim();
        if (!directory.IsEmpty() && PathIsRelativeW(directory)) {
            AfxMessageBox(ChineseUI() ? L"请填写完整的缓存目录路径。" : L"Enter an absolute cache folder path.", MB_ICONWARNING); return;
        }
        auto& profile = AfxGetProfile();
        const auto ini = profile.GetIniPath();
        if (!ini.IsEmpty()) {
            const auto file = CreateFileW(ini, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                AfxMessageBox(ChineseUI() ? L"配置文件无法写入，请检查目录权限。" : L"Cannot write the settings file. Check folder permissions.", MB_ICONWARNING); return;
            }
            CloseHandle(file);
        }
        const auto previous = Bridge::ReadCacheSettings();
        const bool saved = profile.WriteInt(L"MPC-Bridge Cache", L"Enabled", IsDlgButtonChecked(Enabled) == BST_CHECKED)
            && profile.WriteInt(L"MPC-Bridge Cache", L"ForwardSeconds", forward)
            && profile.WriteInt(L"MPC-Bridge Cache", L"BackSeconds", back)
            && profile.WriteInt(L"MPC-Bridge Cache", L"DiskMiB", capacity)
            && profile.WriteString(L"MPC-Bridge Cache", L"Directory", directory);
        if (!saved) {
            profile.WriteInt(L"MPC-Bridge Cache", L"Enabled", previous.enabled);
            profile.WriteInt(L"MPC-Bridge Cache", L"ForwardSeconds", previous.forwardSeconds);
            profile.WriteInt(L"MPC-Bridge Cache", L"BackSeconds", previous.backSeconds);
            profile.WriteInt(L"MPC-Bridge Cache", L"DiskMiB", static_cast<int>(previous.diskBytes / 1048576));
            profile.WriteString(L"MPC-Bridge Cache", L"Directory", previous.directory.c_str());
            AfxMessageBox(ChineseUI() ? L"设置保存失败，原配置已保留。" : L"Could not save settings. Previous values were kept.", MB_ICONWARNING); return;
        }
        profile.Flush(false);
        CDialog::OnOK();
    }
};
}
void CMainFrame::OnBridgeCacheSettings()
{
    if (!CanShowDialog()) return;
    CancelBridgeHold(); CancelLeftLongPressSpeed(true);
    std::shared_ptr<Bridge::MediaCache> cache;
    { std::lock_guard<std::mutex> lock(m_bridgeCacheMutex); cache = m_bridgeCache; }
    CacheSettingsDialog dialog(this, cache); dialog.DoModal();
}
