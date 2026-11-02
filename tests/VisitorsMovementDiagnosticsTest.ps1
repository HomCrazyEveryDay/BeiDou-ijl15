$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = [IO.Path]::GetFullPath((Join-Path $repo '../.tmp/visitor-escort-20261102/diagnostics-test'))
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /O2 /EHsc /std:c++17 "$PSScriptRoot/VisitorsMovementDiagnosticsTest.cpp" "/Fo$output/" "/Fe$output/test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Visitors movement diagnostics test build failed' }
& "$output/test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Visitors movement diagnostics test failed' }
