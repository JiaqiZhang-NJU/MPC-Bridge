param([Parameter(Mandatory = $true)][string]$Player)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$sourcePlayer = (Resolve-Path $Player).Path
$smokeDirectory = Join-Path $repoRoot ('_bin/bridge/smoke/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $smokeDirectory -Force | Out-Null
$smokePlayer = Join-Path $smokeDirectory 'mpc-bridge-smoke.exe'
$smokeIni = Join-Path $smokeDirectory 'mpc-bridge-smoke.ini'
Copy-Item -LiteralPath $sourcePlayer -Destination $smokePlayer

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BridgeSmokeNative {
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int index);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr menu, uint id, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr GetLastActivePopup(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SendMessageTimeout(
        IntPtr window, uint message, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
}
'@

function Send-PlayerMessage([IntPtr]$Window, [uint32]$Message, [uint64]$Argument) {
    $messageResult = [UIntPtr]::Zero
    $sent = [BridgeSmokeNative]::SendMessageTimeout($Window, $Message, [UIntPtr]$Argument, [IntPtr]::Zero, 2, 5000, [ref]$messageResult)
    if ($sent -eq [IntPtr]::Zero) { throw "Player message timed out: $Message" }
}

function Start-SmokePlayer {
    $script:smokeProcess = Start-Process -FilePath $smokePlayer -ArgumentList '/new', '/minimized' -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $script:smokeProcess.Refresh()
        if ($script:smokeProcess.HasExited) { throw 'Player exited before creating its main window.' }
        $script:smokeWindow = $script:smokeProcess.MainWindowHandle
    } while ($script:smokeWindow -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline)
    if ($script:smokeWindow -eq [IntPtr]::Zero) { throw 'Player main window was not created.' }
}

function Get-ProfileMenu {
    $mainMenu = [BridgeSmokeNative]::GetMenu($script:smokeWindow)
    for ($index = 0; $index -lt [BridgeSmokeNative]::GetMenuItemCount($mainMenu); $index++) {
        $popup = [BridgeSmokeNative]::GetSubMenu($mainMenu, $index)
        if ([BridgeSmokeNative]::GetMenuState($popup, 815, 0) -eq [uint32]::MaxValue) { continue }
        Send-PlayerMessage $script:smokeWindow 0x117 $popup.ToInt64()
        $count = [BridgeSmokeNative]::GetMenuItemCount($popup)
        Send-PlayerMessage $script:smokeWindow 0x117 $popup.ToInt64()
        if ([BridgeSmokeNative]::GetMenuItemCount($popup) -ne $count) { throw 'Profile menu was duplicated.' }
        for ($sub = 0; $sub -lt $count; $sub++) {
            $profileMenu = [BridgeSmokeNative]::GetSubMenu($popup, $sub)
            if ($profileMenu -eq [IntPtr]::Zero) { continue }
            if ([BridgeSmokeNative]::GetMenuState($profileMenu, 50000, 0) -ne [uint32]::MaxValue) {
                Send-PlayerMessage $script:smokeWindow 0x117 $profileMenu.ToInt64()
                return $profileMenu
            }
        }
    }
    throw 'Shortcut preset menu not found.'
}

function Assert-Profile([uint32]$Id) {
    $menu = Get-ProfileMenu
    foreach ($candidate in 50000..50003) {
        $checked = ([BridgeSmokeNative]::GetMenuState($menu, $candidate, 0) -band 8) -ne 0
        if ($checked -ne ($candidate -eq $Id)) { throw "Incorrect profile checkmark: $candidate (expected $Id)." }
    }
}

function Test-OnlineEntry {
    $mainMenu = [BridgeSmokeNative]::GetMenu($script:smokeWindow)
    $found = $false
    for ($index = 0; $index -lt [BridgeSmokeNative]::GetMenuItemCount($mainMenu); $index++) {
        $popup = [BridgeSmokeNative]::GetSubMenu($mainMenu, $index)
        if ($popup -eq [IntPtr]::Zero) { continue }
        Send-PlayerMessage $script:smokeWindow 0x117 $popup.ToInt64()
        if ([BridgeSmokeNative]::GetMenuState($popup, 50004, 0) -ne [uint32]::MaxValue) { $found = $true }
    }
    if (!$found) { throw 'Online video entry not found in menus.' }
    if (![BridgeSmokeNative]::PostMessage($script:smokeWindow, 0x111, [UIntPtr][uint64]50004, [IntPtr]::Zero)) { throw "Could not open online dialog. Win32 error: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
    $deadline = (Get-Date).AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $dialog = [BridgeSmokeNative]::GetLastActivePopup($script:smokeWindow)
    } while (($dialog -eq $script:smokeWindow -or $dialog -eq [IntPtr]::Zero) -and (Get-Date) -lt $deadline)
    if ($dialog -eq $script:smokeWindow -or $dialog -eq [IntPtr]::Zero) { throw 'Online video dialog was not created.' }
    [BridgeSmokeNative]::ShowWindow($dialog, 0) | Out-Null
    if ([BridgeSmokeNative]::GetDlgItem($dialog, 10241) -eq [IntPtr]::Zero -or [BridgeSmokeNative]::GetDlgItem($dialog, 10301) -eq [IntPtr]::Zero) { throw 'Online video dialog controls were not created.' }
    Send-PlayerMessage $dialog 0x111 2
}

function Stop-SmokePlayer {
    if ($script:smokeProcess -and !$script:smokeProcess.HasExited) {
        # Queue exit like a normal UI command; synchronous cross-process SendMessage can
        # re-enter shutdown inside a shell COM callback during initial window creation.
        if (![BridgeSmokeNative]::PostMessage($script:smokeWindow, 0x111, [UIntPtr][uint64]816, [IntPtr]::Zero)) { throw 'Could not queue player exit.' }
        if (!$script:smokeProcess.WaitForExit(10000)) { throw 'Player did not exit normally.' }
        if ($script:smokeProcess.ExitCode -ne 0) { throw "Player exit code: $($script:smokeProcess.ExitCode)" }
    }
}

try {
    "[MPC-Bridge]`r`nShortcutProfile=native`r`n" | Set-Content -LiteralPath $smokeIni -Encoding Unicode
    Start-SmokePlayer
    Assert-Profile 50000
    Test-OnlineEntry
    foreach ($id in 50001, 50002, 50000, 50002) {
        Send-PlayerMessage $script:smokeWindow 0x111 $id
        Assert-Profile $id
    }
    Stop-SmokePlayer
    if ((Get-Content -LiteralPath $smokeIni -Raw) -notmatch 'ShortcutProfile=youtube') { throw 'Profile was not persisted.' }
    Start-SmokePlayer
    Assert-Profile 50002
    Stop-SmokePlayer

    $custom = Get-Content (Join-Path $repoRoot 'bridge/profiles/custom-example.json') -Raw
    $custom = $custom.Replace("`r", '').Replace("`n", '')
    "[MPC-Bridge]`r`nShortcutProfile=custom`r`nCustomProfile=$custom`r`n" | Set-Content -LiteralPath $smokeIni -Encoding Unicode
    Start-SmokePlayer
    Assert-Profile 50003
    Stop-SmokePlayer
    Start-SmokePlayer
    Assert-Profile 50003
    Stop-SmokePlayer

    "[MPC-Bridge]`r`nShortcutProfile=custom`r`nCustomProfile=invalid-json`r`n" | Set-Content -LiteralPath $smokeIni -Encoding Unicode
    Start-SmokePlayer
    Assert-Profile 50000
    Stop-SmokePlayer
    Write-Output 'PASS: player startup, menu switching, no duplicate menus, restart persistence, custom profile, invalid-profile fallback and online-dialog open/cancel.'
    Write-Output "Isolated test files: $smokeDirectory"
} finally {
    if ($script:smokeProcess -and !$script:smokeProcess.HasExited) {
        $script:smokeProcess.Kill()
        $script:smokeProcess.WaitForExit()
    }
}
