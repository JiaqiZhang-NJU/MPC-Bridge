#!/usr/bin/env bash
# Publish a verified, self-hosted static release through the existing SSH alias.
set -euo pipefail
umask 077
stage=${1:?staging directory required}
[[ $stage =~ ^/tmp/mpc-bridge-site-[a-f0-9]{32}$ && -d $stage && ! -L $stage ]]
root=/var/www/mpc-bridge
homepage=/var/www/website4self/index.html
config=/etc/nginx/sites-available/mpc-bridge
enabled=/etc/nginx/sites-enabled/mpc-bridge
version=v0.2.1-preview.1
for path in "$root" "$root/downloads" "$root/downloads/$version" "$root/legal" "$config"; do [[ ! -L $path ]]; done
[[ ! -e $enabled || $(readlink -f "$enabled") == "$config" ]]
[[ -f $homepage && ! -L $homepage ]]
cd "$stage"
sha256sum --strict -c upload.sha256
sed 's/\r$//' MPC-Bridge-0.2.1-preview.1-SHA256SUMS.txt | sha256sum --strict -c -
nginx -t
curl -fsS --max-time 20 https://credits.jqzhang.top/health >/dev/null
getent ahostsv4 mpc.jqzhang.top | awk '{print $1}' | grep -qx '117.72.210.148'
install -d -m 700 /var/backups/mpc-bridge
backup=$(mktemp -d /var/backups/mpc-bridge/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX)
cp "$homepage" "$backup/homepage.html"
[[ ! -f $config ]] || cp "$config" "$backup/nginx.conf"
was_enabled=false
[[ ! -e $enabled ]] || was_enabled=true
for file in index.html style.css; do [[ ! -f $root/$file ]] || cp "$root/$file" "$backup/$file"; done
[[ ! -d $root/legal ]] || cp -a "$root/legal" "$backup/legal"
python3 - "$homepage" "$backup/homepage-new.html" <<'PY'
import pathlib, re, sys
source = pathlib.Path(sys.argv[1]).read_text(encoding='utf-8')
entry = '''        <li><a class="project" href="https://mpc.jqzhang.top/">
          <div><h2>mpc-bridge</h2><p>支持双字幕对照的 Windows 视频播放器。</p><span class="address">mpc.jqzhang.top</span></div>
          <span class="arrow" aria-hidden="true">↗</span>
        </a></li>'''
pattern = r'<li><a class="project" href="(?:/mpc/|https://jqzhang.top/mpc/|https://mpc.jqzhang.top/)">.*?</a></li>'
if re.search(pattern, source, re.S):
    source = re.sub(pattern, lambda m:entry.lstrip(), source, flags=re.S)
else:
    marker = '      </ul>'
    if source.count(marker) != 1 or 'class="project"' not in source:
        raise SystemExit('Unexpected homepage structure; nothing published')
    source = source.replace(marker, entry + '\n' + marker)
pathlib.Path(sys.argv[2]).write_text(source, encoding='utf-8')
PY
published=false
rollback() {
    result=$?
    set +e
    if [[ $published != true ]]; then
        for file in index.html style.css; do
            if [[ -f $backup/$file ]]; then install -m 644 "$backup/$file" "$root/$file"; else rm -f "$root/$file"; fi
        done
        # The new legal texts and versioned downloads may remain; no live page links to them after rollback.
        install -m 644 "$backup/homepage.html" "$homepage"
        if [[ -f $backup/nginx.conf ]]; then install -m 644 "$backup/nginx.conf" "$config"; else rm -f "$config"; fi
        if [[ $was_enabled == false ]]; then rm -f "$enabled"; fi
        nginx -t && systemctl reload nginx
        echo "Publication failed; previous pages/configuration restored. Backup: $backup" >&2
    fi
    exit "$result"
}
trap rollback EXIT
install -d -m 755 "$root" "$root/downloads" "$root/legal"
destination=$root/downloads/$version
if [[ -e $destination ]]; then
    (cd "$destination" && sha256sum --strict -c "$stage/asset.sha256")
else
    incoming=$(mktemp -d "$root/downloads/.incoming-XXXXXX")
    while read -r expected name; do
        [[ $name =~ ^MPC-Bridge-0\.2\.1-preview\.1-(x64-Setup\.exe|x64\.zip|source\.zip|SHA256SUMS\.txt)$ ]]
        install -m 644 "$stage/$name" "$incoming/$name"
    done < asset.sha256
    (cd "$incoming" && sha256sum --strict -c "$stage/asset.sha256")
    chmod 755 "$incoming"
    mv -T "$incoming" "$destination"
fi
if [[ ! -f /etc/letsencrypt/live/mpc.jqzhang.top/fullchain.pem ]]; then
    install -m 644 site-http.conf "$config"
    ln -sfn "$config" "$enabled"
    nginx -t
    systemctl reload nginx
fi
certbot certonly --webroot -w /var/www/letsencrypt --non-interactive --keep-until-expiring --cert-name mpc.jqzhang.top -d mpc.jqzhang.top
install -m 644 site-https.conf "$config"
ln -sfn "$config" "$enabled"
nginx -t
systemctl reload nginx
for file in index.html style.css; do
    install -m 644 "$stage/$file" "$root/.$file.new"
    mv -f "$root/.$file.new" "$root/$file"
done
for file in "$stage"/legal/*; do install -m 644 "$file" "$root/legal/"; done
install -m 644 "$backup/homepage-new.html" /var/www/website4self/.index-mpc-new
mv -f /var/www/website4self/.index-mpc-new "$homepage"
python3 - "$stage" "$homepage" <<'PY'
import hashlib, pathlib, sys, urllib.request
stage, home = map(pathlib.Path, sys.argv[1:])
def digest(url):
    h=hashlib.sha256()
    with urllib.request.urlopen(url, timeout=60) as response:
        while chunk := response.read(1024*1024): h.update(chunk)
    return h.hexdigest()
base='https://mpc.jqzhang.top/'
paths=['index.html','style.css',*[str(f.relative_to(stage)) for f in (stage/'legal').iterdir()]]
for name in paths:
    url = base if name=='index.html' else base+name
    assert digest(url)==hashlib.sha256((stage/name).read_bytes()).hexdigest(),url
    print('PASS public page/license:',name)
assert digest('https://jqzhang.top/')==hashlib.sha256(home.read_bytes()).hexdigest()
for line in (stage/'asset.sha256').read_text().splitlines():
    expected,name=line.split()
    assert digest(base+'downloads/v0.2.1-preview.1/'+name)==expected,name
    print('PASS complete public download SHA-256:',name)
with urllib.request.urlopen('https://credits.jqzhang.top/health',timeout=20) as r:
    assert r.status==200
PY
published=true
printf 'Published: https://mpc.jqzhang.top/\nBackup: %s\n' "$backup"
