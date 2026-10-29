param([string[]]$MoviePaths = @())
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = Join-Path $repoRoot '_bin/bridge/subtitle-reader'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath
if (-not $vs) { throw 'VS 2022 Build Tools with ATL/MFC is required.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$exe = Join-Path $output 'subtitle-reader-tests.exe'
$response = Join-Path $output 'reader.rsp'
$batch = Join-Path $output 'build-reader.cmd'
$arguments = @('/nologo', '/std:c++17', '/utf-8', '/EHsc', '/MT', '/O2', '/Zi',
    '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/D_HAS_STD_BYTE=0', '/D_WINDOWS',
    '/DWINVER=0x0A00', '/D_WIN32_WINNT=0x0A00',
    "/I`"$repoRoot/include`"", "/I`"$repoRoot/src`"", "/I`"$repoRoot/src/apps/mplayerc`"",
    "`"$repoRoot/tests/bridge/matroska_subtitle_reader_tests.cpp`"",
    "`"$repoRoot/src/apps/mplayerc/bridge/MatroskaSubtitleReader.cpp`"",
    "`"$repoRoot/src/apps/mplayerc/bridge/MatroskaSubtitleSource.cpp`"",
    "`"$repoRoot/src/apps/mplayerc/bridge/SubtitleFilePreparation.cpp`"",
    "/Fo`"$output/`"", "/Fd`"$output/reader-compile.pdb`"", "/Fe`"$exe`"", '/link', '/LTCG', '/DEBUG',
    "/PDB:`"$output/reader-tests.pdb`"",
    "/LIBPATH:`"$repoRoot/_bin/lib/Release_x64`"", "/LIBPATH:`"$repoRoot/lib64`"",
    'MatroskaSplitter.lib', 'BaseSplitter.lib', 'Subtitles.lib', 'SubPic.lib', 'DSUtil.lib', 'BaseClasses.lib', 'zlib.lib',
    'strmiids.lib', 'version.lib', 'winmm.lib', 'shlwapi.lib', 'ws2_32.lib',
    'imm32.lib', 'crypt32.lib', 'dwmapi.lib', 'wininet.lib')
Set-Content -LiteralPath $response -Value ($arguments -join ' ') -Encoding UTF8
Set-Content -LiteralPath $batch -Encoding ASCII -Value @(
    '@echo off', "call `"$vcvars`" >nul", 'if errorlevel 1 exit /b %errorlevel%',
    "cl @`"$response`"", 'exit /b %errorlevel%')
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw 'Subtitle reader test compilation failed. Build the x64 player libraries first.' }
$fixtures = Join-Path $output 'fixtures'
& python (Join-Path $repoRoot 'tests/bridge/matroska_subtitle_fixtures.py') $fixtures
if ($LASTEXITCODE -ne 0) { throw 'Failed to prepare subtitle extraction fixtures.' }
& $exe $fixtures @MoviePaths
if ($LASTEXITCODE -ne 0) { throw "Subtitle reader tests failed with exit code $LASTEXITCODE." }
