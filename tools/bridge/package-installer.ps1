param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [string]$Version = '0.2.1-preview.2',
    [string]$FileVersion = '0.2.1.2'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[a-zA-Z0-9.-]+$' -or $FileVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid version.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$packageRoot = Join-Path $repoRoot '_bin/bridge/packages'
$package = Join-Path $packageRoot "MPC-Bridge-$Version-x64"
$manifest = Get-Content (Join-Path $package 'build-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.version -ne $Version) { throw 'Package version mismatch.' }
$revision = (& git -C $repoRoot rev-parse HEAD).Trim()
$changes = & git -C $repoRoot status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $changes -or $revision -ne $manifest.sourceRevision) { throw 'Installer source must match the committed package revision.' }
foreach ($file in $manifest.files) {
    $path = Join-Path $package $file.path
    if (!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw ('Hash mismatch: ' + $file.path) }
}
$sourceArchive = Join-Path $packageRoot $manifest.sourceArchive
if ((Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash -ne $manifest.sourceArchiveSha256) { throw 'Source archive hash mismatch.' }
$installer = Join-Path $packageRoot "MPC-Bridge-$Version-x64-Setup.exe"
if (Test-Path -LiteralPath $installer) { throw 'Installer exists; use a new version to preserve it.' }
$compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
& $compilerPath '/Qp' "/DPackageDir=$package" "/DOutputDir=$packageRoot" "/DBridgeVersion=$Version" "/DFileVersion=$FileVersion" (Join-Path $PSScriptRoot 'installer.iss')
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $installer)) { throw 'Installer compilation failed.' }
$assets = @($installer, "$package.zip", $sourceArchive)
$checksumFile = Join-Path $packageRoot "MPC-Bridge-$Version-SHA256SUMS.txt"
$assets | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + (Split-Path $_ -Leaf) } | Set-Content -LiteralPath $checksumFile -Encoding ascii
Write-Output "Installer: $installer"
Write-Output "Release checksums: $checksumFile"
