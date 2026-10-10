param(
    [Parameter(Mandatory = $true)][string]$Player,
    [Parameter(Mandatory = $true)][string]$Parser,
    [Parameter(Mandatory = $true)][string]$Url,
    [int]$TimeoutSeconds = 60,
    [switch]$PreferH264,
    [int]$MaxHeight = 1080,
    [int]$ExpectedHeight = 0
)
$ErrorActionPreference = 'Stop'
if ($Url.Contains('"') -or $Url -match '[\s\x00-\x1f]' -or $Url -notmatch '^https://(www\.)?(bilibili\.com/video/|youtube\.com/watch\?)') { throw 'Use a complete public sample video URL.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$sourcePlayer = (Resolve-Path $Player).Path
$parserPath = (Resolve-Path $Parser).Path
$testDirectory = Join-Path $repoRoot ('_bin/bridge/playback/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$testPlayer = Join-Path $testDirectory 'mpc-bridge-playback.exe'
Copy-Item -LiteralPath $sourcePlayer -Destination $testPlayer
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$ini = @"
[MPC-Bridge]
ShortcutProfile=youtube
CustomProfile={"schemaVersion":1,"id":"rate-test","displayName":"Rate test","bindings":[{"key":"R","action":"rate.relative","args":{"step":0.5}}]}
[Audio]
Mute=1
Volume=30
[OnlineServices]
YDLEnable=1
YDLExePath=$parserPath
YDLMaxHeight=$MaxHeight
[WebServer]
EnableWebServer=1
LocalhostOnly=1
Port=$port
"@
$ini | Set-Content (Join-Path $testDirectory 'mpc-bridge-playback.ini') -Encoding Unicode
if ($PreferH264) {
    # Append a repeated section; the player's INI reader merges its values.
    "[OnlineServices]`r`nYDLVideoCodec=H264`r`nYDLAudioCodec=AAC`r`n" | Add-Content (Join-Path $testDirectory 'mpc-bridge-playback.ini') -Encoding Unicode
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BridgePlaybackNative {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr SendMessageTimeout(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
}
'@
function Get-PlaybackState {
    $request = [Net.HttpWebRequest]::Create("http://127.0.0.1:$port/variables.html")
    $request.Proxy = $null
    # A remote seek can briefly occupy the UI thread while it opens a new range request.
    $request.Timeout = 15000
    $response = $request.GetResponse()
    try {
        $reader = [IO.StreamReader]::new($response.GetResponseStream())
        try { $html = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $state = @{}
        foreach ($match in [regex]::Matches($html, '<p id="([^"]+)">([^<]*)</p>')) { $state[$match.Groups[1].Value] = $match.Groups[2].Value }
        return $state
    } finally { $response.Dispose() }
}
function Send-Key([int]$Key, [bool]$Up = $false, [bool]$Repeat = $false) {
    $keyFlags = if ($Up) { -1073741823 } elseif ($Repeat) { 0x40000001 } else { 1 }
    $message = if ($Up) { 0x101 } else { 0x100 }
    if (![BridgePlaybackNative]::PostMessage($testProcess.MainWindowHandle, $message, [UIntPtr][uint64]$Key, [IntPtr]$keyFlags)) { throw 'Could not post shortcut.' }
}
function Post-Key([int]$Key, [bool]$Repeat = $false) {
    Send-Key $Key $false $Repeat
    Start-Sleep -Milliseconds 60
    Send-Key $Key $true
    Start-Sleep -Milliseconds 440
}
function Post-Command([int]$Command) {
    [BridgePlaybackNative]::PostMessage($testProcess.MainWindowHandle, 0x111, [UIntPtr][uint64]$Command, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 200
}
function Assert-Rate([double]$Rate, [string]$Message) {
    $candidate = Wait-Playback { param($state) [Math]::Abs([double]$state.playbackrate - $Rate) -lt 0.01 } 5
    Assert-Playback ([Math]::Abs([double]$candidate.playbackrate - $Rate) -lt 0.01) $Message
}
function Start-Hold {
    Send-Key 39
    Assert-Rate 3 'Right hold did not start temporary 3x playback.'
    Send-Key 39 $false $true
    Assert-Rate 3 'Repeated Right restarted or changed temporary speed.'
}
function Assert-Playback([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
function Wait-Playback([scriptblock]$Condition, [int]$Seconds = 30) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $candidate = Get-PlaybackState
        if (& $Condition $candidate) { return $candidate }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $candidate
}
$testProcess = $null
$verifiedPlayback = $false
try {
    $testProcess = Start-Process -FilePath $testPlayer -ArgumentList '/new', '/minimized', '/play', ('"' + $Url + '"') -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $state = @{}
    do {
        Start-Sleep -Milliseconds 500
        $testProcess.Refresh()
        if ($testProcess.HasExited) { throw 'Player exited while opening sample.' }
        try { $state = Get-PlaybackState } catch { continue }
    } while (([int]$state.duration -le 0 -or [int]$state.state -ne 2) -and (Get-Date) -lt $deadline)
    if ([int]$state.duration -le 0 -or [int]$state.state -ne 2) {
        $state | ConvertTo-Json | Set-Content (Join-Path $testDirectory 'last-state.json') -Encoding UTF8
        Write-Output "Last player state: state=$($state.state), duration=$($state.duration), position=$($state.position)"
        Write-Output "Diagnostic files: $testDirectory"
        throw 'Sample did not start playing within the timeout.'
    }
    Write-Output "Selected online quality: $($state.onlineheight)p; available: $($state.onlineheights)"
    if ($ExpectedHeight -gt 0) { Assert-Playback ([int]$state.onlineheight -eq $ExpectedHeight) 'Selected online quality does not match expected height.' }
    $firstPosition = [int]$state.position
    Start-Sleep -Milliseconds 1500
    $state = Get-PlaybackState
    Assert-Playback ([int]$state.position -gt $firstPosition) 'Playback position did not advance.'
    Write-Output "Playback running: duration=$($state.duration) ms; advancing position verified."
    Post-Key 75
    $state = Get-PlaybackState
    Assert-Playback ([int]$state.state -eq 1) 'K did not pause playback.'
    Post-Key 75 $true
    Assert-Playback ([int](Get-PlaybackState).state -eq 1) 'Repeated K toggled playback.'
    $beforeSeek = [int](Get-PlaybackState).position
    Post-Key 76
	# Network range requests can complete asynchronously, especially while paused.
    $state = Wait-Playback { param($candidate) [int]$candidate.state -eq 1 -and [int]$candidate.position -ge $beforeSeek + 8000 }
	Write-Output "Relative seek: before=$beforeSeek ms; after=$($state.position) ms; state=$($state.state)"
    Assert-Playback ([int]$state.state -eq 1 -and [int]$state.position -ge $beforeSeek + 8000) 'L did not seek forward while remaining paused.'
    Post-Key 53
    $state = Wait-Playback { param($candidate) [Math]::Abs([int]$candidate.position - [int]$candidate.duration / 2) -lt 1500 }
    Assert-Playback ([Math]::Abs([int]$state.position - [int]$state.duration / 2) -lt 1500) '5 did not seek to 50 percent.'
    Post-Key 38
    Assert-Playback ([int](Get-PlaybackState).volumelevel -eq 35) 'YouTube volume step is incorrect.'
    [BridgePlaybackNative]::PostMessage($testProcess.MainWindowHandle, 0x111, [UIntPtr][uint64]50001, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 200
    Post-Key 38
    Assert-Playback ([int](Get-PlaybackState).volumelevel -eq 45) 'Bilibili volume step is incorrect after switching.'
    Post-Key 32
    Assert-Playback ([int](Get-PlaybackState).state -eq 2) 'Space did not resume playback.'
    # A saved speed must survive the temporary scope, including cancellation paths.
    Post-Command 50003
    Post-Key 82
    Assert-Rate 1.5 'Custom rate action did not set 1.5x.'
    Post-Command 50001
    Post-Key 32
    $beforeTap = [int](Get-PlaybackState).position
    Send-Key 39
    Start-Sleep -Milliseconds 70
    $pending = Get-PlaybackState
    Assert-Playback ([Math]::Abs([int]$pending.position - $beforeTap) -lt 500) 'Right sought on key-down before tap/hold arbitration.'
    Send-Key 39 $true
    $state = Wait-Playback { param($candidate) [Math]::Abs([int]$candidate.position - $beforeTap - 5000) -lt 1000 }
    Assert-Playback ([Math]::Abs([int]$state.position - $beforeTap - 5000) -lt 1000) 'Short Right did not seek 5 seconds on release.'
    Send-Key 39
    Start-Sleep -Milliseconds 450
    Send-Key 39 $true
    Assert-Playback ([int](Get-PlaybackState).state -eq 1) 'Long Right unexpectedly resumed paused playback.'
    Assert-Rate 1.5 'Paused hold changed saved playback rate.'
    Post-Key 32
    Start-Hold
    $heldPosition = [int](Get-PlaybackState).position
    Start-Sleep -Milliseconds 1200
    Assert-Playback ([int](Get-PlaybackState).position -gt $heldPosition + 1800) 'Temporary speed did not advance media faster.'
    Send-Key 39 $true
    Assert-Rate 1.5 'Right release did not restore 1.5x.'
    Start-Hold
    Post-Key 32
    Assert-Rate 1.5 'Pause did not cancel temporary speed.'
    Send-Key 39 $true
    Post-Key 32
    Start-Hold
    Post-Command 50002
    Assert-Rate 1.5 'Preset change did not restore saved speed.'
    Send-Key 39 $true
    Post-Command 50001
    Start-Hold
    # Exercise the native activation/focus/menu handlers without operating other apps.
    [BridgePlaybackNative]::PostMessage($testProcess.MainWindowHandle, 0x1c, [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Assert-Rate 1.5 'App deactivation did not restore saved speed.'
    Send-Key 39 $false $true
    Assert-Rate 1.5 'A repeat after deactivation reactivated the canceled hold.'
    Send-Key 39 $true
    Start-Hold
    [BridgePlaybackNative]::PostMessage($testProcess.MainWindowHandle, 8, [UIntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Assert-Rate 1.5 'Focus loss did not restore saved speed.'
    Send-Key 39 $true
    Start-Hold
    $menuResult = [UIntPtr]::Zero
    $sent = [BridgePlaybackNative]::SendMessageTimeout($testProcess.MainWindowHandle, 0x211, [UIntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$menuResult)
    Assert-Playback ($sent -ne [IntPtr]::Zero) 'Menu handler message timed out.'
    Assert-Rate 1.5 'Entering menus did not restore saved speed.'
    [BridgePlaybackNative]::SendMessageTimeout($testProcess.MainWindowHandle, 0x11f, [UIntPtr][uint64]4294901760, [IntPtr]::Zero, 2, 5000, [ref]$menuResult) | Out-Null
    Send-Key 39 $true
    Write-Output 'PASS: actual online quality, playback, tap/hold, 3x progress, restoration to 1.5x, pause/preset/focus/deactivation/menu cancellation and existing shortcuts.'
    $verifiedPlayback = $true
    Write-Output "Isolated files: $testDirectory"
} finally {
    if ($testProcess -and !$testProcess.HasExited) {
        $testProcess.CloseMainWindow() | Out-Null
        if (!$testProcess.WaitForExit(10000)) { $testProcess.Kill(); $testProcess.WaitForExit(); if ($verifiedPlayback) { throw "Player shutdown timed out." } }
        if ($verifiedPlayback -and $testProcess.ExitCode -ne 0) { throw "Player shutdown failed: $($testProcess.ExitCode)" }
    }
}
