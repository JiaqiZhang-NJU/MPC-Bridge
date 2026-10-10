# 开发记录

2026-10-10，基于 MPC-BE 上游提交 `9186796dd32e70dc36c924078816c7d1d9cb79d0`，开发分支 `codex/mpc-bridge`。已创建远程 fork `JiaqiZhang-NJU/MPC-Bridge`；本地提交是否已发布应以 Git 远程记录为准，预览包随附确切源码和依赖快照。

| 阶段 | 状态 | 当前证据 |
| --- | --- | --- |
| M0 构建环境 | 已建立 | VS 2022 Build Tools 17.14.41、v143 14.44、ATL/MFC、SDK 10.0.22621、MSYS GCC 15.2.1；全部子模块固定；集成源码完整 x64 Release 和语言资源构建通过 |
| M1 快捷键预设 | 预览已接入 | 原生/Bilibili/YouTube 即时切换、设置页、焦点路由和持久化；原生窗口启动测试通过 |
| M2 扩展与长按 | 部分完成 | 严格 JSON 导入、参数化动作、重复键控制和配置恢复已完成；版本 2 长按临时倍速、保存/恢复和取消已完成；导出和覆盖编辑界面待完成 |
| M3 在线视频入口 | 公开样例已验证 | 完整网页链接入口、解析器选择、Bilibili 媒体 Referer；Bilibili/YouTube 实际播放与快捷键测试通过 |
| M4 打包与回归 | 安装与便携预览 | 打包脚本固定解析器校验和许可证，附源码与 SHA-256 清单；当前用户安装器、快捷方式、配置保留与卸载验证已接入；完整手动回归待完成 |

验证证据在忽略的 `_bin/bridge/` 中：

- `core-tests.log`：148 项独立 C++ 检查（GCC 与 MSVC），包括 JSON、焦点、重复键、切换、跳转算术、视频网页 URL 和 Referer 隔离。
- `player-build-*.log`、`resources-build.log`：完整播放器与各语言资源构建。修复 Build Tools 的 vswhere 查找范围，以及 CP936 环境下 UTF-8 源文件编译设置。没有单独完成未经修改的上游基线构建，不能据此声称该基线已通过。
- `player-smoke-online.log`：隔离实例启动、动态菜单不重复、切换预设、重启后恢复、有效自定义预设和无效配置回退、在线视频对话框创建。新的回归结果见 `player-smoke-final.log`。
- `playback-bilibili.log`：公开视频 `BV13x41117TL`，播放位置持续前进；暂停、重复键抑制、相对/百分比跳转、YouTube 音量 5% 与 Bilibili 音量 10%、切换后恢复播放。
- `playback-youtube-wait.log`：公开视频 `aqz-KE-bpKQ`，同一组实际播放器检查通过。诊断证明其实际跳转能力和缓冲检查正常；网络 seek 异步完成，测试必须等待位置更新，不能只在按键后 500 ms 读取一次。最终无诊断代码的回归见 `playback-youtube-final.log`、`playback-bilibili-final.log`。

播放器中的测试实例使用独立 INI，静音、最小化，只操作自身进程；状态读取使用仅限本机的原有 WebServer。它不会接管用户已经运行的播放器。公网请求仅使用公开视频；测试未导入账号数据或 Cookies，也未保存完整视频文件。

输入覆盖层保留原生 `wmcmds`，解码器和渲染器继续使用上游代码。为 Bilibili 的选定音视频 URL 添加经过校验的公开 Referer 来源，并复用原有 HTTP 和外部源滤镜接口。预览版保留上游窗口品牌，目录中的 INI 与 `/new` 启动提供配置分离；preview.4 增加独立安装/卸载身份，不注册文件关联，播放器内部部分系统集成仍沿用上游行为。

验证边界：公开样例通过不代表所有视频、清晰度或网络都可用。Bilibili 预设尚未完成网页行为对照；全屏的真实键盘交互、逐帧、字幕、导入对话框交互和不同 DPI 还需要手动回归。暂不支持 b23.tv 短链、登录和完整预设编辑。解析过程仍沿用上游同步调用及其错误提示，Bridge 专用进度/取消和总超时尚未实现。yt-dlp 缺少受支持 JS runtime 时可能丢失部分 YouTube 格式，预览包不承诺完整格式列表。

2026-10-11 preview.3：把新配置/便携包的在线高度上限改为 1080，保留旧 INI 中的显式选择。显示 yt-dlp 当前选中高度及本次可用高度，不另写平台解析/解码代码。公开视频 `BV13x41117TL` 在本机匿名解析中提供 1080p，原生播放器实际选中 1080p 并播放。长按状态由纯 C++ 控制器分流，原生适配器负责计时、实际流速度和生命周期恢复；重复键不能变成跳转。版本 1 JSON 保持兼容，版本 2 只增加受限 `rate.temporary` 扩展。原有 WebServer 的速度字段改为实际 GetRate 返回值，增加在线高度字段，供状态查看和隔离回归使用。

本次证据：`core-tests-preview3.log`、`core-tests-msvc-preview3.log`、`player-build-preview3.log`、`player-build-preview3-final.log`、`player-build-preview3-shutdown.log`、`player-smoke-preview3-post-exit.log`、`playback-bilibili-preview3-release.log`、`playback-youtube-preview3-final.log`、`playback-youtube-preview3-480.log`、`playback-youtube-preview3-h264.log` 和 `package-verify-preview3.log`。Bilibili 播放测试通过，检查 1080p 格式、短按松开跳转、长按 3 倍速的实际进度、松开恢复 1.5 倍速、暂停/切换预设/失焦/失活/菜单取消。焦点和菜单取消通过自身进程消息触发，不代表已经人工完成 Alt-Tab 或全屏键盘回归。

本次 YouTube 在线回归未通过：1080p 与显式 480p 配置均未在时限内打开样例。独立 WinINet 请求也出现 12002 超时、12030 连接终止；同一个公开视频的 yt-dlp 解析成功，返回 1080p 等格式，另一路 HTTP 请求可访问格式地址。因此保留网络栈访问问题作为本次验证限制，没有把 YouTube 本次回归标记为通过，也没有更改系统代理。此前 preview.2 的播放证据仍保留。预览 INI 编码选择改用上游实际接受的字符串 `H264` / `AAC`，原来的数字 0 会回退成默认 `VP9` / `OPUS`；测试脚本同步修正。

启动/退出回归发现关闭期间的任务栏回调访问播放列表而崩溃，旧 preview.2 在同一测试中也复现（`player-smoke-preview2-comparison.log`）。使用当前 PDB 将错误 RVA 定位到 `UpdateThumbarButton` 的播放列表计数访问。新版本在关闭状态跳过任务栏创建/更新/缩略图回调，并在 `OnClose` 的界面销毁之前取消键盘临时速度。播放脚本增加成功回归后的正常退出码检查；最终测试改用 PostMessage 将退出命令排入消息队列，模拟常规 UI 操作，避免跨进程同步 SendMessage 在初始 shell COM 回调中重入关闭；启动、菜单、配置恢复、对话框和正常退出均通过，见 `player-smoke-preview3-post-exit.log`。这次测试修正与播放器的关闭防护分别记录，不能把退出成功全部归因于关闭防护。

2026-10-11 preview.4：播放器 C++ 源码与已测试的 preview.3 相同，复用其最终二进制；增加根 README、当前用户的一键安装器、构建和验证脚本。编译器为官方 Inno Setup 6.7.3，下载 SHA-256 与 Pyrsys B.V. Authenticode 签名均核验。安装器准备测试使用 preview.3 的 102 文件负载：全部负载校验、中文默认配置、桌面/开始菜单快捷方式、独立卸载登记、按快捷方式参数启动真实安装的播放器、动态菜单生成、Bilibili 预设保存、正常退出、覆盖安装字节不变、卸载移除入口及保留配置均通过，见 `installer-preflight-pass.log`。早期测试修正了动态菜单初始化与 PowerShell 5 的 UIntPtr 转换。正式 preview.4 产物发布前还须运行包验证与同一安装回归，并核对远程提交和资产 SHA-256。安装包本身没有代码签名，YouTube 与手动回归限制沿用上面的记录。