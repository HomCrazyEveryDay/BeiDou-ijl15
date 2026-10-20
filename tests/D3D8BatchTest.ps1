param([int]$Width=1280,[int]$Height=720,[double]$Seconds=3)
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $env:TEMP ('d3d8-batch-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /O2 "$PSScriptRoot/D3D8BatchTest.cpp" "$PSScriptRoot/../ezorsia/D3D8Batch.cpp" user32.lib gdi32.lib "/Fo$output\" "/Fe$output/test.exe"
if($LASTEXITCODE -ne 0){throw 'Build failed'}
& "$output/test.exe" $Width $Height $Seconds
if($LASTEXITCODE -ne 0){throw 'D3D8 batch tests failed'}
