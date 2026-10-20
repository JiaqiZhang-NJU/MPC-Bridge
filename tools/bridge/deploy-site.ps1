param(
    [string]$PackageDirectory = '',
    [string]$SshHost = 'jd-personal',
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $root '_bin/bridge/packages' }
$version = '0.2.0-preview.1'
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
$uploadNames = @($names) + @('index.html', 'deploy-site.sh', 'asset.sha256')
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
& ssh -o BatchMode=yes -o StrictHostKeyChecking=yes $SshHost "umask 077 && mkdir '$remote'"
if ($LASTEXITCODE -ne 0) { throw 'Remote staging directory creation failed.' }
$files = $uploadNames + @('upload.sha256') | ForEach-Object { Join-Path $stage $_ }
& scp -o BatchMode=yes -o StrictHostKeyChecking=yes @files "${SshHost}:$remote/"
if ($LASTEXITCODE -ne 0) { throw 'Website upload failed; live pages have not been changed.' }
& ssh -o BatchMode=yes -o StrictHostKeyChecking=yes $SshHost "sudo -n bash '$remote/deploy-site.sh' '$remote'"
if ($LASTEXITCODE -ne 0) { throw 'Website publication or public verification failed. Inspect the remote output.' }
