param([string]$Version = "0.2.0-preview.1")
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[a-zA-Z0-9.-]+$') { throw 'Invalid package version.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$packageRoot = Join-Path $repoRoot '_bin/bridge/packages'
$archive = Join-Path $packageRoot "MPC-Bridge-$Version-x64.zip"
$qaRoot = Join-Path $repoRoot ("_bin/bridge/package-qa-" + $Version + "-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
if (Test-Path $qaRoot) { throw 'QA directory already exists.' }
Expand-Archive -LiteralPath $archive -DestinationPath $qaRoot
$package = Join-Path $qaRoot "MPC-Bridge-$Version-x64"
$manifest = Get-Content (Join-Path $package 'build-manifest.json') -Raw | ConvertFrom-Json
foreach ($file in $manifest.files) {
    $path = Join-Path $package $file.path
    if (!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw ('Hash mismatch: ' + $file.path) }
}
$sourceArchive = Join-Path $packageRoot $manifest.sourceArchive
if ((Get-FileHash $sourceArchive -Algorithm SHA256).Hash -ne $manifest.sourceArchiveSha256) { throw 'Source ZIP hash mismatch.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$sourceZip = [IO.Compression.ZipFile]::OpenRead($sourceArchive)
try {
    $requiredSources = @('src/apps/mplayerc/bridge/PlayerShortcutAdapter.cpp','src/apps/mplayerc/bridge/HoldGesture.h','src/ExtLib/BaseClasses/streams.h','src/ExtLib/rapidjson/include/rapidjson/document.h','src/ExtLib/vvdec/vvdec/CMakeLists.txt')
    if ($Version -match '^0\.2\.') { $requiredSources += @('src/apps/mplayerc/bridge/MediaCache.cpp','src/apps/mplayerc/bridge/PlayerQualityAdapter.cpp','tests/bridge/cache_integration.py') }
    if ($Version -match '^0\.2\.' -and $Version -notmatch '-dev\.') { $requiredSources += @('src/apps/mplayerc/bridge/DualSubtitleProvider.cpp','tests/bridge/dual_subtitle_playback.py') }
    foreach ($required in $requiredSources) {
        if (!@($sourceZip.Entries | Where-Object { $_.FullName.Replace('\','/').EndsWith('/' + $required) }).Count) { throw ('Missing source: ' + $required) }
    }
    if (@($sourceZip.Entries | Where-Object { $_.FullName.Replace('\','/') -match '/(_bin|\.git)/' }).Count) { throw 'Unexpected build or Git state in source package.' }
    if (@($sourceZip.Entries | Where-Object { $_.FullName -match 'MPC-Bridge-.*-Plan-' }).Count) { throw 'Internal development plan leaked into source package.' }
} finally { $sourceZip.Dispose() }
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BridgePackageNative {
 public delegate bool ChildCallback(IntPtr window, IntPtr argument);
 [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr window);
 [DllImport("user32.dll")] public static extern IntPtr GetLastActivePopup(IntPtr window);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, ChildCallback callback, IntPtr argument);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int length);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
}
"@
$iniPath = Join-Path $package 'MPC-Bridge.ini'
if ((Get-Content $iniPath -Raw) -notmatch '(?m)^YDLMaxHeight=1080\r?$') { throw 'Portable default must be 1080p.' }
if ((Get-Content $iniPath -Raw) -notmatch '(?m)^YDLVideoCodec=H264\r?$' -or (Get-Content $iniPath -Raw) -notmatch '(?m)^YDLAudioCodec=AAC\r?$') { throw 'Portable codec preferences are incorrect.' }
$preset = Get-Content (Join-Path $package 'profiles/bilibili.json') -Raw | ConvertFrom-Json
$right = $preset.bindings | Where-Object { $_.key -eq 'Right' }
if ($preset.schemaVersion -ne 2 -or $right.hold.args.rate -ne 3 -or $right.hold.delayMs -ne 300) { throw 'Packaged hold preset is incorrect.' }
"[Settings]`r`nLanguage=sc`r`n" | Add-Content $iniPath -Encoding Unicode
$process = $null
$dialog = [IntPtr]::Zero
try {
    $process = Start-Process (Join-Path $package 'MPC-Bridge.exe') -ArgumentList '/new','/minimized' -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds(15)
    do { Start-Sleep -Milliseconds 100; $process.Refresh() } while ($process.MainWindowHandle -eq [IntPtr]::Zero -and !$process.HasExited -and (Get-Date) -lt $deadline)
    $main = $process.MainWindowHandle
    if ($process.HasExited -or $main -eq [IntPtr]::Zero -or [BridgePackageNative]::GetMenu($main) -eq [IntPtr]::Zero) { throw 'Packaged player did not open its main window.' }
    [BridgePackageNative]::PostMessage($main,0x111,[UIntPtr][uint64]50004,[IntPtr]::Zero) | Out-Null
    $deadline = (Get-Date).AddSeconds(5)
    do { Start-Sleep -Milliseconds 50; $dialog = [BridgePackageNative]::GetLastActivePopup($main) } while (($dialog -eq $main -or $dialog -eq [IntPtr]::Zero) -and (Get-Date) -lt $deadline)
    if ($dialog -eq $main -or $dialog -eq [IntPtr]::Zero) { throw 'Packaged online dialog did not open.' }
    [BridgePackageNative]::ShowWindow($dialog,0) | Out-Null
    $title = [Text.StringBuilder]::new(512)
    [BridgePackageNative]::GetWindowText($dialog,$title,512) | Out-Null
    if ($title.ToString() -ne '打开在线视频') { throw ('Chinese language resources were not loaded: ' + $title.ToString()) }
    $script:childTexts = [Collections.Generic.List[string]]::new()
    $callback = [BridgePackageNative+ChildCallback]{ param($window,$argument)
        $text = [Text.StringBuilder]::new(512)
        [BridgePackageNative]::GetWindowText($window,$text,512) | Out-Null
        $script:childTexts.Add($text.ToString())
        return $true
    }
    [BridgePackageNative]::EnumChildWindows($dialog,$callback,[IntPtr]::Zero) | Out-Null
    if (!@($script:childTexts | Where-Object { $_ -eq '解析器：yt-dlp.exe' }).Count) { throw 'Bundled parser was not detected.' }
    [BridgePackageNative]::PostMessage($dialog,0x111,[UIntPtr][uint64]2,[IntPtr]::Zero) | Out-Null
    $dialog = [IntPtr]::Zero
    Start-Sleep -Milliseconds 200
    $process.CloseMainWindow() | Out-Null
    if (!$process.WaitForExit(10000) -or $process.ExitCode -ne 0) { throw 'Packaged player did not exit normally.' }
    Write-Output "PASS: $($manifest.files.Count) packaged file hashes, source ZIP and pinned dependency contents, actual portable EXE startup, Chinese online dialog and bundled parser detection."
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
