param([string]$ClientRoot=(Join-Path $PSScriptRoot '../../BeiDou-Client'))
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $PSScriptRoot '../out/tests/monthly-shop-runtime'
New-Item -ItemType Directory -Force $output | Out-Null
$source=Join-Path $PSScriptRoot '../ezorsia'
# Compile the production hooks. Only omit the application-wide precompiled header.
$production=[IO.File]::ReadAllText((Join-Path $source 'MonthlyShop.cpp')).Replace('#include "stdafx.h"','')
$production=[regex]::Replace($production,'(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)','0x30')
[IO.File]::WriteAllText((Join-Path $output 'MonthlyShopUnderTest.cpp'),$production,[Text.UTF8Encoding]::new($true))
& cl.exe /nologo /EHsc /std:c++17 /utf-8 /O2 "/I$source" "/I$output" "$PSScriptRoot/MonthlyShopRuntimeTest.cpp" "/Fo$output\" "/Fe$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO "$PSScriptRoot/../detours/detours.lib" gdi32.lib user32.lib
if($LASTEXITCODE -ne 0){throw 'Monthly shop runtime test build failed'}
& "$output/test.exe" (Join-Path (Resolve-Path $ClientRoot).Path 'BeiDou.exe')
if($LASTEXITCODE -ne 0){throw 'Monthly shop runtime tests failed'}
