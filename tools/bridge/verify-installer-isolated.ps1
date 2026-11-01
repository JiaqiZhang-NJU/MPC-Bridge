param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [string]$Version = '0.2.1-preview.2',
    [string]$FileVersion = '0.2.1.2'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[a-zA-Z0-9.-]+$' -or $FileVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid version.' }
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$scratch = Join-Path $repoRoot '_bin/bridge'
$package = Join-Path $scratch "packages/MPC-Bridge-$Version-x64"
$releaseInstaller = Join-Path $scratch "packages/MPC-Bridge-$Version-x64-Setup.exe"
$releaseHash = (Get-FileHash -LiteralPath $releaseInstaller -Algorithm SHA256).Hash
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$output = Join-Path $scratch "installer-identity-qa-$stamp"
New-Item -ItemType Directory -Path $output | Out-Null
# Keep the scripts two levels below the repository so their relative resources
# resolve exactly as in tools/bridge. Payload and install code are unchanged.
$iss = Join-Path $scratch "installer-identity-$stamp.iss"
$verify = Join-Path $scratch "verify-identity-$stamp.ps1"
$originalId = 'A3B0657C-8CA7-4EB2-B3A7-8DA6040BEA18'
$isolatedId = '94D2F24E-9F4B-4F98-A6BD-3ACB477EA410'
$source = Get-Content (Join-Path $PSScriptRoot 'installer.iss') -Raw -Encoding UTF8
$source = $source.Replace($originalId, $isolatedId)
$source = $source.Replace('AppName=MPC-Bridge', 'AppName=MPC-Bridge QA')
$source = $source.Replace('DefaultGroupName=MPC-Bridge', 'DefaultGroupName=MPC-Bridge QA')
$source = $source.Replace('"{autodesktop}\MPC-Bridge"', '"{autodesktop}\MPC-Bridge QA"')
$source = $source.Replace('"{autoprograms}\MPC-Bridge\', '"{autoprograms}\MPC-Bridge QA\')
$source | Set-Content -LiteralPath $iss -Encoding utf8
$checks = Get-Content (Join-Path $PSScriptRoot 'verify-installer.ps1') -Raw -Encoding UTF8
$checks = $checks.Replace($originalId, $isolatedId)
$checks = $checks.Replace("'MPC-Bridge.lnk'", "'MPC-Bridge QA.lnk'")
$checks = $checks.Replace("'MPC-Bridge/MPC-Bridge.lnk'", "'MPC-Bridge QA/MPC-Bridge.lnk'")
$checks = $checks.Replace("'MPC-Bridge/Uninstall MPC-Bridge.lnk'", "'MPC-Bridge QA/Uninstall MPC-Bridge.lnk'")
$checks | Set-Content -LiteralPath $verify -Encoding utf8
$compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
& $compilerPath '/Qp' "/DPackageDir=$package" "/DOutputDir=$output" "/DBridgeVersion=$Version" "/DFileVersion=$FileVersion" $iss
if ($LASTEXITCODE -ne 0) { throw 'Isolated installer compilation failed.' }
& powershell -ExecutionPolicy Bypass -File $verify -Version $Version -Installer (Join-Path $output "MPC-Bridge-$Version-x64-Setup.exe") -PackageDirectory $package
if ($LASTEXITCODE -ne 0) { throw 'Isolated installer verification failed.' }
if ((Get-FileHash -LiteralPath $releaseInstaller -Algorithm SHA256).Hash -ne $releaseHash) { throw 'Release installer changed during QA.' }
Write-Output 'PASS: identical release payload and install/uninstall code with isolated AppId and shortcuts; production installer unchanged.'
