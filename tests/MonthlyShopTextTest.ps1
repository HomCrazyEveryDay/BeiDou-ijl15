param([string]$ClientRoot=(Join-Path $PSScriptRoot '../../BeiDou-Client'))
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $PSScriptRoot '../out/tests/monthly-shop-text'
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /utf-8 /O2 "$PSScriptRoot/MonthlyShopTextTest.cpp" "/Fo$output\" "/Fe$output/test.exe"
if($LASTEXITCODE -ne 0){throw 'Build failed'}
& "$output/test.exe" (Resolve-Path $ClientRoot).Path (Join-Path (Resolve-Path $output).Path 'shop-tabs-preview.png')
if($LASTEXITCODE -ne 0){throw 'Monthly shop text tests failed'}
