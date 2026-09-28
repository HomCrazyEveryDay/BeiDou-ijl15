$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $env:TEMP ('aran-combo-command-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
& cl.exe /nologo /O2 /EHsc /std:c++17 /DARAN_COMBO_COMMAND_TEST "$PSScriptRoot/AranComboCommandTest.cpp" "$repo/ezorsia/AranComboCommand.cpp" "/Fo$output\" "/Fe$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& "$output/test.exe" "$repo/../BeiDou-Client/BeiDou.exe"
if ($LASTEXITCODE -ne 0) { throw "Command test failed: $LASTEXITCODE" }
