param(
    [string]$Version = '0.2.1-preview.1',
    [string]$Installer,
    [string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[a-zA-Z0-9.-]+$') { throw 'Invalid version.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$packageRoot = Join-Path $repoRoot '_bin/bridge/packages'
if (!$Installer) { $Installer = Join-Path $packageRoot "MPC-Bridge-$Version-x64-Setup.exe" }
if (!$PackageDirectory) { $PackageDirectory = Join-Path $packageRoot "MPC-Bridge-$Version-x64" }
$manifest = Get-Content (Join-Path $PackageDirectory 'build-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.version -ne $Version) { throw 'Package version mismatch.' }
$qaRoot = Join-Path $repoRoot ('_bin/bridge/install-qa-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$installDir = Join-Path $qaRoot 'MPC-Bridge'
$keyName = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A3B0657C-8CA7-4EB2-B3A7-8DA6040BEA18}_is1'
$shortcuts = @(
    (Join-Path ([Environment]::GetFolderPath('Desktop')) 'MPC-Bridge.lnk'),
    (Join-Path ([Environment]::GetFolderPath('Programs')) 'MPC-Bridge/MPC-Bridge.lnk'),
    (Join-Path ([Environment]::GetFolderPath('Programs')) 'MPC-Bridge/Uninstall MPC-Bridge.lnk')
)
foreach ($view in [Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32) {
    $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $view)
    try {
        $existing = $baseKey.OpenSubKey($keyName)
        if ($existing) { $existing.Dispose(); throw 'An MPC-Bridge installation already exists; refusing to replace it.' }
    } finally { $baseKey.Dispose() }
}
foreach ($shortcut in $shortcuts) { if (Test-Path -LiteralPath $shortcut) { throw ('Existing shortcut: ' + $shortcut) } }
if (Test-Path -LiteralPath $qaRoot) { throw 'QA directory already exists.' }
New-Item -ItemType Directory -Path $qaRoot | Out-Null
function Invoke-Setup([string]$LogName) {
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /LANG=sc /DIR="' + $installDir + '" /LOG="' + (Join-Path $qaRoot $LogName) + '"'
    $setupProcess = Start-Process -FilePath $installer -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$setupProcess.WaitForExit(60000)) { throw 'Installer timed out.' }
    if ($setupProcess.ExitCode -ne 0) { throw ('Installer exit: ' + $setupProcess.ExitCode) }
}
function Assert-Payload {
    foreach ($file in $manifest.files) {
        if ($file.path -eq 'MPC-Bridge.ini') { continue }
        $path = Join-Path $installDir $file.path
        if (!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw ('Installed payload mismatch: ' + $file.path) }
    }
    $installedManifest = Get-Content (Join-Path $installDir 'build-manifest.json') -Raw | ConvertFrom-Json
    if ($installedManifest.sourceRevision -ne $manifest.sourceRevision) { throw 'Installed source revision mismatch.' }
}
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class BridgeInstallerNative {
 [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr window);
 [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
 [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr menu, int index);
 [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int index);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
}
"@
function Get-MenuCommands([IntPtr]$Menu) {
    for ($i = 0; $i -lt [BridgeInstallerNative]::GetMenuItemCount($Menu); $i++) {
        [BridgeInstallerNative]::GetMenuItemID($Menu, $i)
        $subMenu = [BridgeInstallerNative]::GetSubMenu($Menu, $i)
        if ($subMenu -ne [IntPtr]::Zero) {
            $messageResult = [UIntPtr]::Zero
            $sent = [BridgeInstallerNative]::SendMessageTimeout($player.MainWindowHandle, 0x117, [UIntPtr]([uint64]$subMenu.ToInt64()), [IntPtr]::Zero, 2, 5000, [ref]$messageResult)
            if ($sent -eq [IntPtr]::Zero) { throw 'Menu initialization timed out.' }
            Get-MenuCommands $subMenu
        }
    }
}
$player = $null
$uninstalled = $false
try {
    Invoke-Setup 'install.log'
    Assert-Payload
    $iniPath = Join-Path $installDir 'MPC-Bridge.ini'
    $initialIni = Get-Content -LiteralPath $iniPath -Raw
    if ($initialIni -notmatch 'YDLMaxHeight=1080' -or $initialIni -notmatch 'Language=sc') { throw 'Fresh configuration or language mismatch.' }
    $registration = Get-ItemProperty -LiteralPath ('HKCU:\' + $keyName)
    if ($registration.InstallLocation.TrimEnd('\') -ne $installDir -or $registration.DisplayVersion -ne $Version) { throw 'Uninstall registration mismatch.' }
    $shell = New-Object -ComObject WScript.Shell
    foreach ($shortcut in $shortcuts) {
        if (!(Test-Path -LiteralPath $shortcut)) { throw ('Missing shortcut: ' + $shortcut) }
        $link = $shell.CreateShortcut($shortcut)
        if (!$link.TargetPath.StartsWith($installDir + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Shortcut points outside the installation.' }
        if ($shortcut -notmatch 'Uninstall') {
            if ($link.Arguments -ne '/new' -or $link.WorkingDirectory -ne $installDir) { throw 'Shortcut launch configuration mismatch.' }
        }
    }
    Write-Output ('PASS: installed payload (' + $manifest.files.Count + ' files), default configuration, shortcuts and uninstall registration')
    $launchLink = $shell.CreateShortcut($shortcuts[0])
    $player = Start-Process -FilePath $launchLink.TargetPath -ArgumentList ($launchLink.Arguments + ' /minimized') -WorkingDirectory $launchLink.WorkingDirectory -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds(20)
    do { Start-Sleep -Milliseconds 200; $player.Refresh() } while (!$player.HasExited -and $player.MainWindowHandle -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline)
    if ($player.HasExited -or $player.MainWindowHandle -eq [IntPtr]::Zero) { throw 'Installed player did not create a window.' }
    Start-Sleep -Seconds 3
    $commands = @(Get-MenuCommands ([BridgeInstallerNative]::GetMenu($player.MainWindowHandle)))
    foreach ($command in 50000,50001,50002,50003,50004) { if ($command -notin $commands) { throw ('Missing Bridge menu command: ' + $command) } }
    [BridgeInstallerNative]::PostMessage($player.MainWindowHandle, 0x111, [UIntPtr][uint64]50001, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
    [BridgeInstallerNative]::PostMessage($player.MainWindowHandle, 0x111, [UIntPtr][uint64]816, [IntPtr]::Zero) | Out-Null
    if (!$player.WaitForExit(10000) -or $player.ExitCode -ne 0) { throw 'Installed player did not exit normally.' }
    if ((Get-Content -LiteralPath $iniPath -Raw) -notmatch 'ShortcutProfile=bilibili') { throw 'Installed preset persistence failed.' }
    Write-Output 'PASS: installed player starts with shortcut arguments, exposes Bridge menus, saves Bilibili preset and exits normally'
    $savedHash = (Get-FileHash -LiteralPath $iniPath -Algorithm SHA256).Hash
    Invoke-Setup 'reinstall.log'
    Assert-Payload
    if ((Get-FileHash -LiteralPath $iniPath -Algorithm SHA256).Hash -ne $savedHash) { throw 'Reinstall overwrote user settings.' }
    Write-Output 'PASS: repeat installation keeps user configuration byte-for-byte'
    $uninstaller = Join-Path $installDir 'unins000.exe'
    $uninstallArguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="' + (Join-Path $qaRoot 'uninstall.log') + '"'
    $uninstallProcess = Start-Process -FilePath $uninstaller -ArgumentList $uninstallArguments -WindowStyle Hidden -PassThru
    if (!$uninstallProcess.WaitForExit(60000) -or $uninstallProcess.ExitCode -ne 0) { throw 'Uninstall failed.' }
    $uninstalled = $true
    if (Test-Path -LiteralPath ('HKCU:\' + $keyName)) { throw 'Uninstall registration remains.' }
    foreach ($shortcut in $shortcuts) { if (Test-Path -LiteralPath $shortcut) { throw 'Shortcut remains after uninstall.' } }
    foreach ($file in $manifest.files) {
        if ($file.path -ne 'MPC-Bridge.ini' -and (Test-Path -LiteralPath (Join-Path $installDir $file.path))) { throw ('Payload remains after uninstall: ' + $file.path) }
    }
    if ((Get-FileHash -LiteralPath $iniPath -Algorithm SHA256).Hash -ne $savedHash) { throw 'Uninstall changed user settings.' }
    Write-Output 'PASS: uninstall removes application, shortcuts and registration while keeping configuration'
    Write-Output "Installer QA passed. Evidence: $qaRoot"
} finally {
    if ($player -and !$player.HasExited) {
        $player.CloseMainWindow() | Out-Null
        if (!$player.WaitForExit(10000)) { $player.Kill(); $player.WaitForExit() }
    }
    # Only clean up the new isolated installation created by this test.
    if (!$uninstalled -and (Test-Path -LiteralPath (Join-Path $installDir 'unins000.exe'))) {
        $cleanup = Start-Process -FilePath (Join-Path $installDir 'unins000.exe') -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -WindowStyle Hidden -PassThru
        $cleanup.WaitForExit(60000) | Out-Null
    }
}
