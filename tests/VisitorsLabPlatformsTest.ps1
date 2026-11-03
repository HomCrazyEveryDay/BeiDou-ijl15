$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$workspace = Split-Path $repo
$output = Join-Path $workspace '.tmp/visitor-lab-platform-tests'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -vcvars_ver=14.29' | Out-Null
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /O2 /EHsc /std:c++17 "$PSScriptRoot/VisitorsLabPlatformsTest.cpp" "/Fo$output/test.obj" "/Fe$output/test.exe" /link ole32.lib oleaut32.lib user32.lib /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'Laboratory platform test build failed' }
& "$output/test.exe" "$workspace/BeiDou-Client/BeiDou.exe" "$workspace/BeiDou-Client"
if ($LASTEXITCODE -ne 0) { throw "Laboratory platform test failed: $LASTEXITCODE" }
