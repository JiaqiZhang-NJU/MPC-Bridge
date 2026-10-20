#!/usr/bin/env bash
# Run through sudo on jd-personal after deploy-site.ps1 has uploaded the payload.
set -euo pipefail
umask 077
stage=${1:?staging directory required}
[[ $stage =~ ^/tmp/mpc-bridge-site-[a-f0-9]{32}$ ]]
[[ -d $stage && ! -L $stage ]]
version=v0.2.0-preview.1
root=/var/www/website4self
[[ -d $root && ! -L $root && -f $root/index.html && ! -L $root/index.html ]]
for path in "$root/mpc" "$root/mpc/downloads" "$root/mpc/downloads/$version"; do
    [[ ! -L $path ]]
done
cd "$stage"
sha256sum --strict -c upload.sha256
sha256sum --strict -c MPC-Bridge-0.2.0-preview.1-SHA256SUMS.txt
nginx -t
mkdir -p /var/backups/website4self/mpc
backup=$(mktemp -d /var/backups/website4self/mpc/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX)
cp "$root/index.html" "$backup/homepage.html"
if [[ -e $root/mpc/index.html ]]; then
    [[ -f $root/mpc/index.html && ! -L $root/mpc/index.html ]]
    cp "$root/mpc/index.html" "$backup/mpc.html"
fi
# Prepare the navigation change before publishing anything.
python3 - "$root/index.html" "$backup/homepage-new.html" <<'PY'
import pathlib, sys
source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
if 'href="/mpc/"' not in source and 'href="https://jqzhang.top/mpc/"' not in source:
    marker = '      </ul>'
    if source.count(marker) != 1 or 'class="project"' not in source:
        raise SystemExit("Unexpected homepage structure; no page replaced")
    entry = '''        <li><a class="project" href="/mpc/">
          <div><h2>MPC-Bridge</h2><p>支持双字幕对照的 Windows 视频播放器。</p><span class="address">jqzhang.top/mpc/</span></div>
          <span class="arrow" aria-hidden="true">↗</span>
        </a></li>
'''
    source = source.replace(marker, entry + marker)
pathlib.Path(sys.argv[2]).write_text(source, encoding="utf-8")
PY
install -d -o root -g root -m 755 "$root/mpc" "$root/mpc/downloads"
destination=$root/mpc/downloads/$version
if [[ -e $destination ]]; then
    [[ -d $destination ]]
    # Published versions are immutable. Never replace files with different bytes.
    (cd "$destination" && sha256sum --strict -c "$stage/asset.sha256")
else
    incoming=$(mktemp -d "$root/mpc/downloads/.incoming-XXXXXX")
    while read -r expected name; do
        [[ $name =~ ^MPC-Bridge-0\.2\.0-preview\.1-(x64-Setup\.exe|x64\.zip|source\.zip|SHA256SUMS\.txt)$ ]]
        install -o root -g root -m 644 "$stage/$name" "$incoming/$name"
    done < asset.sha256
    (cd "$incoming" && sha256sum --strict -c "$stage/asset.sha256")
    chmod 755 "$incoming"
    mv -T "$incoming" "$destination"
fi
temporary=$(mktemp "$root/mpc/.index-XXXXXX")
home_temporary=$(mktemp "$root/.index-XXXXXX")
published=false
restore_pages() {
    result=$?
    set +e
    if [[ $published != true ]]; then
        install -o root -g root -m 644 "$backup/homepage.html" "$home_temporary" && mv -f "$home_temporary" "$root/index.html"
        if [[ -f $backup/mpc.html ]]; then
            install -o root -g root -m 644 "$backup/mpc.html" "$temporary" && mv -f "$temporary" "$root/mpc/index.html"
        else
            rm -f "$root/mpc/index.html"
        fi
        echo "Publication failed; restored previous pages. Backup: $backup" >&2
    fi
    rm -f "$temporary" "$home_temporary"
    exit "$result"
}
trap restore_pages EXIT
install -o root -g root -m 644 index.html "$temporary"
mv -f "$temporary" "$root/mpc/index.html"
install -o root -g root -m 644 "$backup/homepage-new.html" "$home_temporary"
mv -f "$home_temporary" "$root/index.html"
# Verify complete downloads, rather than HEAD responses alone.
if python3 - "$stage" "$root" <<'PY'
import hashlib, pathlib, sys, urllib.request
stage, root = map(pathlib.Path, sys.argv[1:])
def digest(url):
    h = hashlib.sha256()
    with urllib.request.urlopen(url, timeout=60) as response:
        while chunk := response.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()
base = "https://jqzhang.top/"
for url, file in [(base, root / "index.html"), (base + "mpc/", stage / "index.html")]:
    assert digest(url) == hashlib.sha256(file.read_bytes()).hexdigest(), url
for line in (stage / "asset.sha256").read_text().splitlines():
    expected, name = line.split()
    url = base + "mpc/downloads/v0.2.0-preview.1/" + name
    assert digest(url) == expected, url
    print("PASS public SHA-256:", name)
PY
then
    published=true
    printf 'Published: https://jqzhang.top/mpc/\nBackup: %s\n' "$backup"
else
    echo 'Public verification failed.' >&2
    exit 1
fi
