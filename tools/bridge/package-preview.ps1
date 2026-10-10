param(
    [Parameter(Mandatory = $true)][string]$Parser,
    [string]$Version = '0.1.0-preview.1'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[a-zA-Z0-9.-]+$') { throw 'Invalid package version.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$trackedChanges = & git -C $repoRoot status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $trackedChanges) { throw 'Commit tracked source changes before packaging so the revision identifies the build.' }
$parserPath = (Resolve-Path $Parser).Path
$parserVersion = ((& $parserPath --ignore-config --version) | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $parserVersion -ne '2026.08.19' -or
    (Get-FileHash $parserPath -Algorithm SHA256).Hash -ne '66674953FE251B89F4D08C5F0E35E0728679BD67AB3D7D05C0562AF101DD3E7A') {
    throw 'This preview requires the verified yt-dlp 2026.08.19 Windows executable and matching license notices.'
}
$buildDirectory = Join-Path $repoRoot '_bin/mpc-be_x64'
$packageRoot = Join-Path $repoRoot '_bin/bridge/packages'
$packageDirectory = Join-Path $packageRoot "MPC-Bridge-$Version-x64"
if (Test-Path $packageDirectory) { throw 'Package already exists; use a new version to preserve it.' }
New-Item -ItemType Directory -Force $packageDirectory | Out-Null
Copy-Item (Join-Path $buildDirectory 'mpc-be64.exe') (Join-Path $packageDirectory 'MPC-Bridge.exe')
Copy-Item (Join-Path $buildDirectory 'mpciconlib.dll') $packageDirectory
Copy-Item (Join-Path $buildDirectory 'Lang') $packageDirectory -Recurse
Copy-Item -LiteralPath $parserPath -Destination (Join-Path $packageDirectory 'yt-dlp.exe')
Copy-Item (Join-Path $repoRoot 'bridge/third-party') $packageDirectory -Recurse
Copy-Item (Join-Path $repoRoot 'LICENSE.txt') $packageDirectory
Copy-Item (Join-Path $repoRoot 'docs/Authors.txt') $packageDirectory
Copy-Item (Join-Path $repoRoot 'docs/Authors mpc-hc team.txt') $packageDirectory
Copy-Item (Join-Path $repoRoot 'distrib/Shaders') $packageDirectory -Recurse
Copy-Item (Join-Path $repoRoot 'distrib/Shaders11') $packageDirectory -Recurse
Copy-Item (Join-Path $repoRoot 'bridge/profiles') $packageDirectory -Recurse
Copy-Item (Join-Path $repoRoot 'bridge/README.md') (Join-Path $packageDirectory 'DEVELOPER-README.md')
Copy-Item (Join-Path $repoRoot 'bridge/DEVELOPMENT.md') $packageDirectory
Copy-Item (Join-Path $repoRoot 'bridge/PREVIEW-README.txt') (Join-Path $packageDirectory 'README.txt')
$ini = @'
; MPC-Bridge portable preview
[MPC-Bridge]
ShortcutProfile=native
[OnlineServices]
YDLEnable=1
YDLExePath=yt-dlp.exe
YDLVideoCodec=0
YDLAudioCodec=0
YDLMaxHeight=720
'@
$ini | Set-Content (Join-Path $packageDirectory 'MPC-Bridge.ini') -Encoding Unicode
$launcher = @'
@echo off
cd /d "%~dp0"
if not exist "MPC-Bridge.ini" echo ; MPC-Bridge portable preview>"MPC-Bridge.ini"
start "" "%~dp0MPC-Bridge.exe" /new
'@
$launcher | Set-Content (Join-Path $packageDirectory 'Launch-MPC-Bridge.cmd') -Encoding ascii
$revision = & git -C $repoRoot rev-parse HEAD
$sourceDirectory = Join-Path $packageRoot "MPC-Bridge-$Version-source"
$sourceArchive = "$sourceDirectory.zip"
if (Test-Path $sourceDirectory) { throw 'Source package already exists.' }
New-Item -ItemType Directory -Path $sourceDirectory | Out-Null
$rootArchive = Join-Path $packageRoot "$Version-source-root.zip"
& git -C $repoRoot archive --format=zip --output=$rootArchive HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not archive source revision.' }
Expand-Archive -LiteralPath $rootArchive -DestinationPath $sourceDirectory
$submodules = @(& git -C $repoRoot submodule status --recursive)
if ($LASTEXITCODE -ne 0) { throw 'Could not read submodule revisions.' }
$submoduleIndex = 0
foreach ($line in $submodules) {
    if ($line -notmatch '^ (?<revision>[0-9a-f]{40}) (?<path>[^ ]+)') { throw 'Submodules must be initialized at their pinned revisions.' }
    $dependencyPath = $Matches.path
    $dependencyRevision = $Matches.revision
    $dependencyArchive = Join-Path $packageRoot "$Version-dependency-$submoduleIndex.zip"
    & git -C (Join-Path $repoRoot $dependencyPath) archive --format=zip "--prefix=$dependencyPath/" --output=$dependencyArchive $dependencyRevision
    if ($LASTEXITCODE -ne 0) { throw "Could not archive dependency: $dependencyPath" }
    Expand-Archive -LiteralPath $dependencyArchive -DestinationPath $sourceDirectory -Force
    $submoduleIndex++
}
Compress-Archive -Path $sourceDirectory -DestinationPath $sourceArchive
@"
This package was built from MPC-Bridge revision $($revision.Trim()).
The matching source archive is $(Split-Path $sourceArchive -Leaf), supplied alongside this portable ZIP.
It includes the pinned MPC-BE submodule sources. See bridge/README.md and docs/Compilation.txt for build requirements.
Upstream MPC-BE revision: 9186796dd32e70dc36c924078816c7d1d9cb79d0
The fork exists at https://github.com/JiaqiZhang-NJU/MPC-Bridge ; this package does not assert that the local revision has been pushed.
yt-dlp 2026.08.19 source: https://github.com/yt-dlp/yt-dlp/tree/2026.08.19
yt-dlp notices: third-party/yt-dlp/
"@ | Set-Content (Join-Path $packageDirectory 'SOURCE.txt') -Encoding UTF8
$manifest = @{
    version = $Version
    sourceRevision = $revision.Trim()
    sourceRepository = 'https://github.com/JiaqiZhang-NJU/MPC-Bridge'
    sourceBranch = 'codex/mpc-bridge'
    builtAt = (Get-Item (Join-Path $buildDirectory 'mpc-be64.exe')).LastWriteTimeUtc.ToString('o')
    packagedAt = (Get-Date).ToUniversalTime().ToString('o')
    parserVersion = $parserVersion
    parserSource = 'https://github.com/yt-dlp/yt-dlp/releases/tag/2026.08.19'
    sourceArchive = Split-Path $sourceArchive -Leaf
    sourceArchiveSha256 = (Get-FileHash $sourceArchive -Algorithm SHA256).Hash
    submoduleRevisions = $submodules
    files = @(Get-ChildItem $packageDirectory -File -Recurse | ForEach-Object {
        @{ path = $_.FullName.Substring($packageDirectory.Length + 1); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $packageDirectory 'build-manifest.json') -Encoding UTF8
$archivePath = "$packageDirectory.zip"
Compress-Archive -Path $packageDirectory -DestinationPath $archivePath
Write-Output "Preview directory: $packageDirectory"
Write-Output "Preview ZIP: $archivePath"
Write-Output "Source ZIP: $sourceArchive"
