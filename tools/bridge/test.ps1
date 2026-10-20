param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$testOutput = Join-Path $repoRoot '_bin/bridge'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
& python (Join-Path $PSScriptRoot 'generate_profiles.py') --check
if ($LASTEXITCODE -ne 0) { throw 'Embedded preset validation failed.' }
& $Compiler -std=c++17 -Wall -Wextra -Werror -Wno-error=class-memaccess -pedantic `
    -I (Join-Path $repoRoot 'src') `
    (Join-Path $repoRoot 'src/apps/mplayerc/bridge/ShortcutProfile.cpp') `
    (Join-Path $repoRoot 'src/apps/mplayerc/bridge/OnlineUrl.cpp') `
    (Join-Path $repoRoot 'tests/bridge/shortcut_profile_tests.cpp') `
    -static-libgcc -static-libstdc++ -o (Join-Path $testOutput 'shortcut-profile-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Shortcut profile compilation failed.' }
& (Join-Path $testOutput 'shortcut-profile-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Shortcut profile tests failed.' }
& $Compiler -std=c++17 -Wall -Wextra -Werror -pedantic -I (Join-Path $repoRoot 'src') `
    (Join-Path $repoRoot 'tests/bridge/cache_model_tests.cpp') `
    -static-libgcc -static-libstdc++ -o (Join-Path $testOutput 'cache-model-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Cache model compilation failed.' }
& (Join-Path $testOutput 'cache-model-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Cache/quality model tests failed.' }
& $Compiler -std=c++17 -Wall -Wextra -Werror -pedantic -I (Join-Path $repoRoot 'src') `
    (Join-Path $repoRoot 'tests/bridge/dual_subtitle_tests.cpp') `
    -static-libgcc -static-libstdc++ -o (Join-Path $testOutput 'dual-subtitle-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Dual subtitle model compilation failed.' }
& (Join-Path $testOutput 'dual-subtitle-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Dual subtitle model tests failed.' }
