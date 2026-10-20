# 下载网站

目标地址：`https://jqzhang.top/mpc/`，静态目录 `/var/www/website4self/mpc/`。首页增加播放器入口，四份正式 Release 文件发布至独立版本目录。不需要新增 DNS、证书或重载服务。

先生成本地负载并验证 Release SHA-256：

```bash
powershell -ExecutionPolicy Bypass -File tools/bridge/deploy-site.ps1 -PrepareOnly
```

通过现有 `jd-personal` 别名发布：

```bash
powershell -ExecutionPolicy Bypass -File tools/bridge/deploy-site.ps1
```

脚本先检查 SSH、安装包校验和与 Nginx 配置，再将文件写入独立版本目录，最后替换下载页和首页。相同版本禁止覆盖不同内容。备份保存在非公开的 `/var/backups/website4self/mpc/`，失败会恢复旧页面。成功条件是主站页面、下载页面和四份完整文件的公共 HTTPS SHA-256 全部匹配。

备份目录含 `homepage.html` 和已有下载页 `mpc.html`（首次发布不存在）。如需手动回退，只恢复这些页面；保留已分发版本文件，避免破坏旧下载链接。不要将备份、临时上传负载或整个工作区放入公开目录。
