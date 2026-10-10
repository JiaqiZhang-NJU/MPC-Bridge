param(
    [Parameter(Mandatory = $true)][string]$Player,
    [Parameter(Mandatory = $true)][string]$Parser,
    [Parameter(Mandatory = $true)][string]$Url,
    [int]$TimeoutSeconds = 60,
    [switch]$PreferH264
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
[Audio]
Mute=1
Volume=30
[OnlineServices]
YDLEnable=1
YDLExePath=$parserPath
YDLMaxHeight=480
[WebServer]
EnableWebServer=1
LocalhostOnly=1
Port=$port
"@
$ini | Set-Content (Join-Path $testDirectory 'mpc-bridge-playback.ini') -Encoding Unicode
if ($PreferH264) {
    # Append a repeated section; the player's INI reader merges its values.
    "[OnlineServices]`r`nYDLVideoCodec=0`r`nYDLAudioCodec=0`r`n" | Add-Content (Join-Path $testDirectory 'mpc-bridge-playback.ini') -Encoding Unicode
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BridgePlaybackNative {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
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
function Post-Key([int]$Key, [bool]$Repeat = $false) {
    $keyFlags = if ($Repeat) { 0x40000001 } else { 1 }
    if (![BridgePlaybackNative]::PostMessage($testProcess.MainWindowHandle, 0x100, [UIntPtr][uint64]$Key, [IntPtr]$keyFlags)) { throw 'Could not post shortcut.' }
    Start-Sleep -Milliseconds 500
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
    Write-Output 'PASS: online playback progress, pause, repeat suppression, relative/percent seeking, preset switching and volume steps.'
    Write-Output "Isolated files: $testDirectory"
} finally {
    if ($testProcess -and !$testProcess.HasExited) {
        $testProcess.CloseMainWindow() | Out-Null
        if (!$testProcess.WaitForExit(10000)) { $testProcess.Kill(); $testProcess.WaitForExit() }
    }
}
