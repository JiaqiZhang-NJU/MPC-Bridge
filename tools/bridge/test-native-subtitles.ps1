param([string[]]$SubtitlePaths = @())
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = Join-Path $repoRoot '_bin/bridge/native-subtitles'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath
if (-not $vs) { throw 'VS 2022 Build Tools with ATL/MFC is required.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$exe = Join-Path $output 'subtitle-text-native-tests.exe'
$response = Join-Path $output 'native.rsp'
$batch = Join-Path $output 'build-native.cmd'
$arguments = @('/nologo', '/std:c++17', '/utf-8', '/EHsc', '/MT', '/O2',
    '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/D_HAS_STD_BYTE=0', '/D_WINDOWS',
    '/DWINVER=0x0A00', '/D_WIN32_WINNT=0x0A00',
    "/I`"$repoRoot/include`"", "/I`"$repoRoot/src`"",
    "/I`"$repoRoot/src/apps/mplayerc`"",
    "`"$repoRoot/tests/bridge/subtitle_text_native_tests.cpp`"",
    "`"$repoRoot/src/apps/mplayerc/bridge/DualSubtitleProvider.cpp`"",
    "/Fo`"$output/`"", "/Fe`"$exe`"", '/link', '/LTCG',
    "/LIBPATH:`"$repoRoot/_bin/lib/Release_x64`"", "/LIBPATH:`"$repoRoot/lib64`"",
    'Subtitles.lib', 'DSUtil.lib', 'SubPic.lib', 'BaseClasses.lib', 'zlib.lib',
    'strmiids.lib', 'version.lib', 'winmm.lib', 'shlwapi.lib', 'ws2_32.lib',
    'imm32.lib', 'crypt32.lib', 'dwmapi.lib')
# cl's /link consumes the remainder of the same command line.
Set-Content -LiteralPath $response -Value ($arguments -join ' ') -Encoding UTF8
Set-Content -LiteralPath $batch -Encoding ASCII -Value @(
    '@echo off', "call `"$vcvars`" >nul", 'if errorlevel 1 exit /b %errorlevel%',
    "cl @`"$response`"", 'exit /b %errorlevel%')
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw 'Native subtitle test compilation failed. Build the x64 player libraries first.' }

$fixtures = Join-Path $output 'fixtures'
New-Item -ItemType Directory -Force $fixtures | Out-Null
$srt = "1`n00:00:01,000 --> 00:00:06,000`nFirst line`nSecond line`n"
$ass = @'
[Script Info]
ScriptType: v4.00+
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.00,0:00:06.00,Default,Speaker,0,0,0,,{\pos(600,20)}First line\NSecond line
'@
$ssa = @'
[Script Info]
ScriptType: v4.00
[V4 Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding
Style: Default,Arial,24,16777215,0,0,0,0,0,1,1,0,2,10,10,10,0,1
[Events]
Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: Marked=0,0:00:01.00,0:00:06.00,Default,Speaker,0,0,0,,First line\NSecond line
'@
$vtt = "WEBVTT`n`n00:00:01.000 --> 00:00:06.000`nFirst line`nSecond line`n"
$files = @('sample.srt', 'sample.ass', 'sample.ssa', 'sample.vtt', 'utf16.ass')
Set-Content -LiteralPath (Join-Path $fixtures $files[0]) -Value $srt -Encoding UTF8
Set-Content -LiteralPath (Join-Path $fixtures $files[1]) -Value $ass -Encoding UTF8
Set-Content -LiteralPath (Join-Path $fixtures $files[2]) -Value $ssa -Encoding UTF8
Set-Content -LiteralPath (Join-Path $fixtures $files[3]) -Value $vtt -Encoding UTF8
Set-Content -LiteralPath (Join-Path $fixtures $files[4]) -Value $ass -Encoding Unicode
$paths = @($files | ForEach-Object { Join-Path $fixtures $_ }) + $SubtitlePaths
& $exe @paths
if ($LASTEXITCODE -ne 0) { throw "Native subtitle tests failed with exit code $LASTEXITCODE." }
