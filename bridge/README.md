# mpc-bridge

在 MPC-BE 上增加平台快捷键预设和在线视频入口，复用原播放器的解码、渲染和播放功能。

## 当前功能

- 菜单中的「快捷键预设」可选择原生、Bilibili（预览）、YouTube 和自定义。
- 设置 → 按键中可选择预设、导入 JSON、查看预设键位；点击应用后生效。
- 预设只接管声明的键位，其余键位沿用 MPC-BE 设置。选择原生即可关闭覆盖层。
- 输入框、菜单和设置窗口保留正常键盘操作；声明为不重复的按键不会因长按反复触发。
- JSON 预设只允许已定义的播放器动作，校验失败不会替换当前预设。

「文件 → 打开在线视频」接受完整 Bilibili、YouTube 视频网页链接，以及 youtu.be 链接。沿用 MPC-BE 的 yt-dlp 格式选择与播放链路；新增入口可选择本机解析器。预览包附带已校验的解析器，使用便携 INI 和 `/new` 启动。为 Bilibili 媒体请求补充解析器提供的公开 Referer 来源，不处理账号登录或 Cookies。

Bilibili 右方向键短按松开后前进 5 秒，按住 300 毫秒临时切到 3 倍速，松开恢复此前速度。暂停、停止、关闭视频、失焦、打开菜单/设置、切换全屏或预设会取消加速；取消后的重复键和松开不会触发跳转。长按仅在非直播视频正在播放且流支持倍速时生效。预设导出、覆盖编辑界面和 b23.tv 分享短链尚未完成。清晰度、播放速度、字幕和跳转能力取决于实际流及播放器支持。在线播放需要本机网络能够访问对应网站和媒体地址。

新配置和 preview.3 便携包默认优先 1080p，格式不足时沿用原有选流回退。打开视频后屏幕提示及信息栏显示当前格式高度和可用高度；它们来自本次解析结果。已有 INI 中的手动清晰度设置保留：从旧包升级时，若仍为 720p，可在设置 → 在线服务中改为 1080p，或使用新包的新 INI。仅调整上限不能获得解析器未提供的账号限定格式。

## 自定义预设

参考 [custom-example.json](profiles/custom-example.json)。文件使用 UTF-8，最大 128 KiB；`schemaVersion` 支持 1 和 2；旧的版本 1 文件可继续使用。`id` 使用小写字母、数字、`-` 或 `_`。

```json
{
  "schemaVersion": 1,
  "id": "my-profile",
  "displayName": "我的快捷键",
  "bindings": [
    {"key": "K", "action": "play.toggle"},
    {"key": "Right", "action": "seek.relative", "args": {"seconds": 15}, "repeat": true},
    {"key": "J", "action": "none"}
  ]
}
```

支持 A–Z、数字行、Space、Escape、方向键、Home、End、Enter、Comma、Period。`modifiers` 可组合 Shift、Control、Alt。`repeat` 默认 false。`trigger` 当前仅支持 press。

动作包括 `none`、`play.toggle`、`fullscreen.toggle`、`fullscreen.exit`、`mute.toggle`、`subtitles.toggle`、`seek.relative`、`seek.percent`、`volume.relative`、`rate.relative`、`frame.forward`、`frame.backward`、`playlist.next`、`playlist.previous`。带参数的动作使用 `args` 对象：跳转为 `seconds`（−3600 到 3600）、百分比跳转为 `percent`（0–100）、音量为 `percent`（非零整数，−100 到 100）、速度为 `step`（非零增量，−4 到 4）。相同键位和修饰键不能重复声明。

版本 2 可给普通动作附加 `hold`，此时普通动作在短按松开后执行，长按和普通动作互斥；不得同时设置 `repeat: true`。目前长按动作只允许 `rate.temporary`，`delayMs` 为 100–2000 毫秒，`rate` 为 0.25–4.0。运行时使用实际流的速度保存/恢复，并检查设置倍速是否成功。

```json
{
  "schemaVersion": 2,
  "id": "my-hold",
  "displayName": "长按加速",
  "bindings": [
    {"key": "Right", "action": "seek.relative", "args": {"seconds": 5},
     "hold": {"action": "rate.temporary", "args": {"rate": 3}, "delayMs": 300}}
  ]
}
```

## 构建和测试

完整播放器的环境要求见上游 `docs/Compilation.txt`。独立核心测试不依赖 MFC：

```powershell
powershell -ExecutionPolicy Bypass -File tools/bridge/test.ps1 -Compiler C:\msys64\mingw64\bin\g++.exe
python tools/bridge/generate_profiles.py --check
```

修改内置 JSON 后运行 `python tools/bridge/generate_profiles.py`，再提交生成的 `BuiltinProfiles.h`。当前 148 项独立检查覆盖配置校验、焦点和重复键路由、短按/长按阈值及取消、预设切换、跳转边界、网页链接校验以及媒体 Referer 隔离；它不代替播放器 UI 验证。

Windows x64 完整构建使用 VS 2022 Build Tools、v143、ATL/MFC、Windows SDK 10.0.22621 和上游规定的 MSYS 工具包。初始化全部固定子模块，配置 `environments.bat`，运行 `update_gcc.bat` 后：

```powershell
cmd /c build.bat Build x64 Main Release VS2022 NoWait
cmd /c build.bat Build x64 Resources Release VS2022 NoWait
powershell -ExecutionPolicy Bypass -File tools/bridge/smoke-player.ps1 -Player _bin/mpc-be_x64/mpc-be64.exe
powershell -ExecutionPolicy Bypass -File tools/bridge/smoke-playback.ps1 -Player _bin/mpc-be_x64/mpc-be64.exe -Parser C:/path/to/yt-dlp.exe -Url https://www.bilibili.com/video/BV13x41117TL -PreferH264 -ExpectedHeight 1080
```

自动播放测试仅操作它启动的隔离实例，静音播放公开视频；使用临时便携 INI 和仅限本机的 WebServer 读取状态。测试还检查实际选中格式高度、3 倍速时播放位置前进、恢复 1.5 倍速及各取消事件；焦点/失活/菜单事件通过消息调用自身原生处理器。它不会修改现有播放器的配置。测试结束后关闭自己的实例。测试日志与二进制位于忽略的 `_bin/bridge/`。

## 开发状态

见 [DEVELOPMENT.md](DEVELOPMENT.md)。远程 fork 为 [JiaqiZhang-NJU/MPC-Bridge](https://github.com/JiaqiZhang-NJU/MPC-Bridge)。当前交付形态是 Windows x64 便携预览，界面仍保留上游播放器名称；正式品牌隔离、安装包和完整手动 UI 回归留待后续阶段。
