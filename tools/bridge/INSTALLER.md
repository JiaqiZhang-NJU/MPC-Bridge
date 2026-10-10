# Windows 安装包

安装包从已校验的便携目录生成，播放器二进制和解析器保持相同。默认仅为当前用户安装到 `%LOCALAPPDATA%\Programs\MPC-Bridge`，创建桌面/开始菜单快捷方式，以 `/new` 启动。独立 AppId 提供独立卸载入口；不注册文件关联或 shell 扩展。安装器不主动关闭已运行的播放器，升级前应退出安装目录中的实例。

首次只需点击「安装」。不覆盖已有 `MPC-Bridge.ini`，卸载也保留它和播放器生成的用户文件。要完全清除设置，请先卸载，再自行删除安装目录中保留的配置。新配置的播放器语言与安装语言一致；默认使用原生快捷键，可在菜单选择平台预设。

需要 Inno Setup 6.5.2 或更新版本。此次使用 6.7.3，编译器安装文件的官方 SHA-256 为 `9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732`，执行前核验了 Pyrsys B.V. 的有效 Authenticode 签名。来源和验证方法见 [官方页面](https://jrsoftware.org/isdl-verify.php)。发布的 MPC-Bridge 安装包自身未签名。

先提交源文件，再生成便携版和含子模块的源码快照：

```powershell
powershell -ExecutionPolicy Bypass -File tools/bridge/package-preview.ps1 -Parser C:\path\to\yt-dlp.exe -Version 0.1.0-preview.4
powershell -ExecutionPolicy Bypass -File tools/bridge/verify-preview.ps1 -Version 0.1.0-preview.4
powershell -ExecutionPolicy Bypass -File tools/bridge/package-installer.ps1 -Compiler 'C:\path\to\ISCC.exe'
powershell -ExecutionPolicy Bypass -File tools/bridge/verify-installer.ps1
```

产物位于 `_bin/bridge/packages`：安装 EXE、便携 ZIP、完整源码 ZIP、三者的 SHA-256 清单。编译脚本拒绝源码版本不一致、被修改的包文件和已有同名安装包。发布前核验远程提交、Release 标签及上传资产的哈希。

静默安装可用 `Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`，测试可加 `/DIR="完整路径" /LOG="完整日志路径"`。安装验证脚本在仓库内创建新目录，检查所有发布文件、快捷方式、卸载登记、原生播放器菜单与退出、重复安装保留配置、卸载保留配置及移除入口。若检测到同身份的已安装版本或同名快捷方式，拒绝测试，防止覆盖现有安装。
