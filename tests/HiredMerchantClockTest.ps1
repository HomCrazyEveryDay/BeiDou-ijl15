$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output = Join-Path $env:TEMP ('hired-merchant-clock-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
& cl.exe /nologo /O2 /EHsc /std:c++17 /DHIRED_MERCHANT_CLOCK_TEST /I"$PSScriptRoot/../ezorsia" "$PSScriptRoot/HiredMerchantClockTest.cpp" "$PSScriptRoot/../ezorsia/HiredMerchantClock.cpp" "/Fo$output\" "/Fe$output/test.exe" /link /BASE:0x10000000
if ($LASTEXITCODE -ne 0) { throw 'Clock test build failed' }
& "$output/test.exe"
if ($LASTEXITCODE -ne 0) { throw "Clock test failed: $LASTEXITCODE" }
