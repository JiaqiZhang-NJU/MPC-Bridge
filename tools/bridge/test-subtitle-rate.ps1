param([int]$Cycles = 3)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $root '_bin/bridge/subtitle-rate-qa'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -property installationPath
if (-not $vs) { throw 'VS 2022 Build Tools with ATL/MFC is required.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
function Build-Player([bool]$probe) {
    $batch = Join-Path $output 'build-rate.cmd'
    $lines = @('@echo off', "call `"$vcvars`" >nul", 'if errorlevel 1 exit /b %errorlevel%')
    foreach ($project in @('src/SubPic/SubPic.vcxproj', 'src/Subtitles/Subtitles.vcxproj',
        'src/filters/renderer/VideoRenderers/VideoRenderers.vcxproj', 'src/apps/mplayerc/mpc-be.vcxproj')) {
        $flag = if ($probe) { 'true' } else { 'false' }
        $lines += "MSBuild `"$root/$project`" /p:SolutionDir=`"$root/`" /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /p:BridgeSubtitleProbe=$flag /m /v:minimal /nologo"
        $lines += 'if errorlevel 1 exit /b %errorlevel%'
    }
    Set-Content -LiteralPath $batch -Value $lines -Encoding ASCII
    & $env:ComSpec /d /c $batch
    if ($LASTEXITCODE -ne 0) { throw 'Player build failed.' }
}
try {
    Build-Player $true
    & python (Join-Path $root 'tests/bridge/subtitle_rate_matrix.py') --cycles $Cycles
    if ($LASTEXITCODE -ne 0) { throw 'Subtitle rate regression failed; inspect the retained probe logs.' }
} finally {
    # GPU readback is test-only. Always restore the ordinary release build.
    Build-Player $false
}
