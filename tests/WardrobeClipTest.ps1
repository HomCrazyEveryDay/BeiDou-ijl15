param([string]$ClientPath=(Join-Path $PSScriptRoot '../../BeiDou-Client'))
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $PSScriptRoot '../out/tests/wardrobe-clip'
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /utf-8 /O2 "$PSScriptRoot/WardrobeClipTest.cpp" "$PSScriptRoot/../ezorsia/WardrobeClip.cpp" "/Fo$output\" "/Fe$output/test.exe" user32.lib oleaut32.lib ole32.lib "$PSScriptRoot/../detours/detours.lib"
if($LASTEXITCODE -ne 0){throw 'Wardrobe clipping test build failed'}
Push-Location $ClientPath
try {& "$output/test.exe";if($LASTEXITCODE -ne 0){throw "Wardrobe clipping test failed: $LASTEXITCODE"}}finally{Pop-Location}
