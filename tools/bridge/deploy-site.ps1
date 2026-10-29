param(
    [string]$PackageDirectory = '',
    [string]$SshHost = 'jd-personal',
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $root '_bin/bridge/packages' }
$version = '0.2.1-preview.1'
$id = [Guid]::NewGuid().ToString('N')
$stage = Join-Path $root "_bin/bridge/site-stage-$id"
New-Item -ItemType Directory -Path $stage | Out-Null
$names = @(
    "MPC-Bridge-$version-x64-Setup.exe",
    "MPC-Bridge-$version-x64.zip",
    "MPC-Bridge-$version-source.zip",
    "MPC-Bridge-$version-SHA256SUMS.txt"
)
foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $PackageDirectory $name) -Destination $stage }
Copy-Item -LiteralPath (Join-Path $root 'bridge/site/index.html') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'bridge/site/style.css') -Destination $stage
$legal = Join-Path $stage 'legal'
New-Item -ItemType Directory -Path $legal | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'LICENSE.txt') -Destination $legal
Copy-Item -LiteralPath (Join-Path $root 'docs/Authors.txt') -Destination $legal
Copy-Item -LiteralPath (Join-Path $root 'docs/Authors mpc-hc team.txt') -Destination (Join-Path $legal 'Authors-mpc-hc.txt')
Copy-Item -LiteralPath (Join-Path $root 'bridge/third-party/yt-dlp/LICENSE.txt') -Destination (Join-Path $legal 'yt-dlp-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'bridge/third-party/yt-dlp/THIRD_PARTY_LICENSES.txt') -Destination (Join-Path $legal 'yt-dlp-THIRD_PARTY_LICENSES.txt')
foreach ($config in @('site-http.conf','site-https.conf')) {
    $data = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $config)).Replace("`r`n", "`n")
    [IO.File]::WriteAllText((Join-Path $stage $config), $data, (New-Object Text.UTF8Encoding($false)))
}
# Bash scripts must use LF when copied from a Windows checkout.
$utf8 = New-Object Text.UTF8Encoding($false)
$script = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'deploy-site.sh')).Replace("`r`n", "`n")
[IO.File]::WriteAllText((Join-Path $stage 'deploy-site.sh'), $script, $utf8)
$assets = foreach ($name in $names) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $stage $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllText((Join-Path $stage 'asset.sha256'), ($assets -join "`n") + "`n", $utf8)
$checksums = Get-Content -LiteralPath (Join-Path $stage "MPC-Bridge-$version-SHA256SUMS.txt")
foreach ($line in $checksums) {
    if ($line -notmatch '^([a-f0-9]{64})  (MPC-Bridge-[A-Za-z0-9.\-]+)$') { throw 'Invalid release checksum file.' }
    $file = Join-Path $stage $Matches[2]
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Matches[1]) { throw "Release checksum mismatch: $file" }
}
if ($checksums.Count -ne 3) { throw 'Expected checksums for three release assets.' }
$uploadNames = @($names) + @('index.html', 'style.css', 'deploy-site.sh', 'asset.sha256', 'site-http.conf', 'site-https.conf') + @(Get-ChildItem -LiteralPath $legal -File | ForEach-Object { 'legal/' + $_.Name })
$upload = foreach ($name in $uploadNames) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $stage $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllText((Join-Path $stage 'upload.sha256'), ($upload -join "`n") + "`n", $utf8)
Write-Output "Prepared verified website payload: $stage"
if ($PrepareOnly) { return }
$remote = "/tmp/mpc-bridge-site-$id"
& ssh -o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=10 $SshHost 'whoami; sudo -n whoami'
if ($LASTEXITCODE -ne 0) { throw 'SSH connection failed. Verified local payload has been retained.' }
& ssh -o BatchMode=yes -o StrictHostKeyChecking=yes $SshHost "umask 077 && mkdir -p '$remote/legal'"
if ($LASTEXITCODE -ne 0) { throw 'Remote staging directory creation failed.' }
$files = $uploadNames + @('upload.sha256') | ForEach-Object { Join-Path $stage $_ }
$rootFiles = @($uploadNames + @('upload.sha256') | Where-Object { $_ -notlike 'legal/*' } | ForEach-Object { Join-Path $stage $_ })
& scp -o BatchMode=yes -o StrictHostKeyChecking=yes @rootFiles "${SshHost}:$remote/"
if ($LASTEXITCODE -ne 0) { throw 'Website upload failed.' }
$legalFiles = @(Get-ChildItem -LiteralPath $legal -File | ForEach-Object FullName)
& scp -o BatchMode=yes -o StrictHostKeyChecking=yes @legalFiles "${SshHost}:$remote/legal/"
if ($LASTEXITCODE -ne 0) { throw 'Website upload failed; live pages have not been changed.' }
& ssh -o BatchMode=yes -o StrictHostKeyChecking=yes $SshHost "sudo -n bash '$remote/deploy-site.sh' '$remote'"
if ($LASTEXITCODE -ne 0) { throw 'Website publication or public verification failed. Inspect the remote output.' }
