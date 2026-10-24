$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$env:Path = "$(Split-Path $vswhere);$env:Path"
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo 'out/tests/visitors-scroll'
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /O2 /EHsc /std:c++17 /DVISITORS_SCROLL_TEST "$PSScriptRoot/VisitorsScrollTest.cpp" "/Fo$output/" "/Fe$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO OleAut32.lib
if ($LASTEXITCODE -ne 0) { throw 'Visitors scroll test build failed' }
& "$output/test.exe" "$repo/../BeiDou-Client/BeiDou.exe" "$repo/../BeiDou-Client"
if ($LASTEXITCODE -ne 0) { throw "Visitors scroll test failed: $LASTEXITCODE" }
