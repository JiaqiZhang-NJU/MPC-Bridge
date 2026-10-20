# 下载网站

公开地址：`https://mpc.jqzhang.top/`，服务器目录 `/var/www/mpc-bridge/`。页面参考成绩网站的浅色侧栏、灰色卡片和深色按钮；仅提供介绍、下载与开源说明，不收集用户数据。

```bash
powershell -ExecutionPolicy Bypass -File tools/bridge/deploy-site.ps1 -PrepareOnly
powershell -ExecutionPolicy Bypass -File tools/bridge/deploy-site.ps1
```

前提：`mpc.jqzhang.top` 的 A 记录指向服务器，现有 `jd-personal` SSH 别名可用。脚本先验证正式发行包 SHA-256，在独立版本目录发布安装包、便携版、对应完整源码与校验文件；配置独立 Nginx 虚拟主机，通过现有 ACME webroot 签发证书并启用 HTTPS，最后更新主站项目入口。上游许可证和贡献者声明在页面、安装负载与源码中保留。

成功条件是公开 HTTPS 页面、CSS、许可文件及四份完整下载的 SHA-256 匹配，成绩站健康检查仍通过。只检查 HEAD 不算完成。备份位于非公开 `/var/backups/mpc-bridge/<时间>/`，失败恢复页面、主站入口和 Nginx 配置。版本目录禁止覆盖不同内容；保留已分发的对应源码。

重新部署前检查线上版本，不把整个工作区、内部计划、备份或暂存文件上传到公开目录。证书使用 certbot 现有续期机制和 Nginx reload hook。
