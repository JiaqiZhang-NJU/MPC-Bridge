# mpc-bridge

基于 [MPC-BE](https://github.com/Aleksoid1978/MPC-BE) 的非官方扩展：保留播放器的解码、渲染和播放能力，增加 Bilibili / YouTube 快捷键预设与在线视频入口。

## 下载与安装

**[下载 Windows x64 一键安装包](https://github.com/JiaqiZhang-NJU/MPC-Bridge/releases/download/v0.2.1-preview.1/MPC-Bridge-0.2.1-preview.1-x64-Setup.exe)** · [发布页面、便携版和完整源码](https://github.com/JiaqiZhang-NJU/MPC-Bridge/releases/tag/v0.2.1-preview.1)

支持 Windows 10 / 11 x64。打开安装包，点击「安装」，完成后从桌面或开始菜单启动。安装到当前用户目录，无需管理员权限；包含 yt-dlp，无需另行下载。升级保留配置，卸载保留 `MPC-Bridge.ini`，重新安装可继续使用。当前安装包未做代码签名，Windows 可能显示未知发布者提示。

便携版解压到可写目录，双击 `Launch-MPC-Bridge.cmd`。安装版和便携版均使用各自目录中的 INI 保存配置。

## 使用

1. 「查看 → 快捷键预设」选择 Bilibili（预览）、YouTube 或 MPC-BE 原生。
2. 「文件 → 打开在线视频」粘贴完整的 Bilibili / YouTube 视频网页链接。
3. Bilibili 预设：右方向键短按前进 5 秒；按住 300 毫秒临时 3 倍速，松开恢复原速度。
4. 「设置 → 按键」可导入自定义 JSON 预设。其余未覆盖的按键沿用原生设置。

新配置默认优先 1080p，打开后显示本次选中及可用高度。已有配置若仍限于 720p，可在「设置 → 在线服务」中改为 1080p。实际清晰度和倍速能力取决于视频格式、账号限制及网络访问条件。

## 预览版范围

本版增加「播放记忆」：再次打开同一视频自动续播，并恢复倍速、清晰度和字幕。新配置默认开启，已有配置中的关闭选项得到保留。[使用与保存规则](bridge/PLAYBACK-MEMORY.md)。

支持两路字幕对照：从「文件 → 双字幕对照…」选择本地 SRT/ASS/SSA/VTT 或内嵌文字/PGS 轨道，支持独立延迟、文字样式、图片缩放、隐藏与交换。提供底部双层和顶部/底部两种布局，本地 MKV 在后台准备完整字幕，改善前后跳转恢复。本版修复 PGS 合成画布提前降采样造成的模糊。[使用方法与支持范围](bridge/DUAL-SUBTITLES.md)。

本版同时包含独立「清晰度」菜单和有界 MP4 缓存预加载。[缓存与清晰度说明](bridge/CACHE-VALIDATION.md)。字幕需使用内置字幕渲染器；内嵌 ASS/SSA 在对照模式按文字显示，不保留特效。VobSub 等其他图片格式继续使用普通单字幕。AI 翻译、OCR 和听写尚未提供可用入口。

这是 Windows x64 预览版本。受控播放和字幕画面已纳入自动回归；完整人工多显示器、DPI、HDR 和全部媒体兼容性尚未覆盖。此前 YouTube 的本机网络播放限制、签名地址自动刷新等缓存限制仍存在。安装器未签名，原生窗口保留 MPC-BE 品牌。

## 开发与许可

[功能、预设格式和构建方法](bridge/README.md) · [开发及验证记录](bridge/DEVELOPMENT.md) · [安装包构建与验证](tools/bridge/INSTALLER.md)

采用 [GPL-3.0-or-later](LICENSE.txt)，保留 [MPC-BE 贡献者](docs/Authors.txt) 与第三方声明。Release 中的 `source.zip` 包含本次提交和固定版本子模块的完整源码；确切提交、依赖与 SHA-256 见包内 `SOURCE.txt`、`build-manifest.json` 和发布校验清单。yt-dlp 的源码与许可见 [第三方声明](bridge/third-party/yt-dlp)。上游项目说明保留在 [docs/README.md](docs/README.md)。
