# mpc-bridge

基于 [MPC-BE](https://github.com/Aleksoid1978/MPC-BE) 的非官方扩展：保留播放器的解码、渲染和播放能力，增加 Bilibili / YouTube 快捷键预设与在线视频入口。

## 下载与安装

**[下载 Windows x64 一键安装包](https://github.com/JiaqiZhang-NJU/MPC-Bridge/releases/download/v0.1.0-preview.4/MPC-Bridge-0.1.0-preview.4-x64-Setup.exe)** · [发布页面、便携版和完整源码](https://github.com/JiaqiZhang-NJU/MPC-Bridge/releases/tag/v0.1.0-preview.4)

支持 Windows 10 / 11 x64。打开安装包，点击「安装」，完成后从桌面或开始菜单启动。安装到当前用户目录，无需管理员权限；包含 yt-dlp，无需另行下载。升级保留配置，卸载保留 `MPC-Bridge.ini`，重新安装可继续使用。当前安装包未做代码签名，Windows 可能显示未知发布者提示。

便携版解压到可写目录，双击 `Launch-MPC-Bridge.cmd`。安装版和便携版均使用各自目录中的 INI 保存配置。

## 使用

1. 「查看 → 快捷键预设」选择 Bilibili（预览）、YouTube 或 MPC-BE 原生。
2. 「文件 → 打开在线视频」粘贴完整的 Bilibili / YouTube 视频网页链接。
3. Bilibili 预设：右方向键短按前进 5 秒；按住 300 毫秒临时 3 倍速，松开恢复原速度。
4. 「设置 → 按键」可导入自定义 JSON 预设。其余未覆盖的按键沿用原生设置。

新配置默认优先 1080p，打开后显示本次选中及可用高度。已有配置若仍限于 720p，可在「设置 → 在线服务」中改为 1080p。实际清晰度和倍速能力取决于视频格式、账号限制及网络访问条件。

## 预览版范围

开发分支新增独立「清晰度」菜单和 MP4 缓存预加载，使用方式和验证边界见 [缓存与清晰度说明](bridge/CACHE-VALIDATION.md)。上面的公开下载仍是 preview.4；开发构建尚未发布为正式预览版本。

Bilibili 公开样例的 1080p 播放与长按恢复已通过实测，148 项核心检查及播放器启动、预设保存测试通过。此次 YouTube 样例因本机 WinINet 网络连接失败未完成播放回归；可解析链接不代表一定能播放。当前不支持账号登录、b23.tv 短链或完整预设编辑，完整手动全屏与 DPI 回归尚未完成。

当前版本是 `0.1.0-preview.4`，播放器功能与 preview.3 相同，新增安装与发布流程。原生窗口仍保留 MPC-BE 品牌。安装器有独立的安装和卸载身份，不注册文件关联；播放器内部部分系统集成仍沿用上游行为。

## 开发与许可

[功能、预设格式和构建方法](bridge/README.md) · [开发及验证记录](bridge/DEVELOPMENT.md) · [安装包构建与验证](tools/bridge/INSTALLER.md)

采用 [GPL-3.0-or-later](LICENSE.txt)，保留 [MPC-BE 贡献者](docs/Authors.txt) 与第三方声明。Release 中的 `source.zip` 包含本次提交和固定版本子模块的完整源码；确切提交、依赖与 SHA-256 见包内 `SOURCE.txt`、`build-manifest.json` 和发布校验清单。yt-dlp 的源码与许可见 [第三方声明](bridge/third-party/yt-dlp)。上游项目说明保留在 [docs/README.md](docs/README.md)。
