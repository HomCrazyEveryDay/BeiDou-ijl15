$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-shadow-partner-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'ShadowPartnerDamageSyncTest.exe'
$sourceDir = Join-Path $PSScriptRoot '..\ezorsia'
$source = [IO.File]::ReadAllText((Join-Path $sourceDir 'ShadowPartnerDamageSync.cpp'))
$source = $source.Replace('0x00BEBFA4', '0x30BEBFA4').Replace('0x00441AE8', '0x30441AE8')
[IO.File]::WriteAllText((Join-Path $outputDir 'DamageSyncUnderTest.h'), $source)
$hooks = [IO.File]::ReadAllText((Join-Path $sourceDir 'HpMpAlert.cpp'))
$first = $hooks.IndexOf('static bool HandleShowMobDamagePacket(')
$last = $hooks.IndexOf('using SaveGlobal_t', $first)
if ($first -lt 0 -or $last -le $first) { throw 'Cannot locate production damage receive/display hooks.' }
[IO.File]::WriteAllText((Join-Path $outputDir 'DamageHooksUnderTest.h'), $hooks.Substring($first, $last - $first))
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$outputDir" "/I$sourceDir" (Join-Path $PSScriptRoot 'ShadowPartnerDamageSyncTest.cpp') "/Fo:$outputDir\" "/Fe:$output" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'ShadowPartnerDamageSyncTest compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'ShadowPartnerDamageSyncTest failed.' }
