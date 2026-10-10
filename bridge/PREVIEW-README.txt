mpc-bridge0.1.0 预览版 · Windows x64

解压到可写目录，双击 Launch-MPC-Bridge.cmd 启动。

1. 在「查看」菜单的「快捷键预设」中选择 Bilibili（预览）或 YouTube。
2. 「文件 → 打开在线视频」可粘贴完整的 Bilibili、YouTube 视频网页链接。
3. 「设置 → 按键」中可导入自定义 JSON，并通过「键位…」查看当前预设。
4. 选择「MPC-BE 原生」即可恢复使用原生键位。平台预设不修改原生键位表。

预览版使用目录中的 MPC-Bridge.ini 保存配置，并以新实例启动。
随包包含 yt-dlp；无需自行配置路径。在线播放需要当前网络能访问视频和媒体地址。
暂不支持 b23.tv 分享短链、长按临时倍速或账号登录。Bilibili 键位仍属于预览，未完成网页全部行为对照。
个别网络格式可能无法跳转或调整速度。清晰度和格式可通过播放器的在线服务设置调整。

已完成：完整 x64 构建、预设和配置校验测试、播放器启动/预设持久化测试、公开在线视频样例测试。
尚未完成：完整手动 UI 回归、所有视频/清晰度兼容性验证和正式安装包。

这是基于 MPC-BE 的非官方扩展预览。解码与渲染继续使用 MPC-BE。
MPC-BE 源码：https://github.com/Aleksoid1978/MPC-BE
MPC-Bridge fork：https://github.com/JiaqiZhang-NJU/MPC-Bridge
本预览对应源码随附在 source.zip 中，确切提交和依赖见 SOURCE.txt、build-manifest.json。
yt-dlp 源码与发行版：https://github.com/yt-dlp/yt-dlp
播放器许可见 LICENSE.txt，原项目贡献者见 Authors.txt；yt-dlp 的许可文件见随包对应文件。
