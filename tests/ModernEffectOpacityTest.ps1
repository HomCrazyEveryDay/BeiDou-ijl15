param([string]$ClientPath=(Join-Path $PSScriptRoot '../../BeiDou-Client'))
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $PSScriptRoot '../out/tests/modern-effect-opacity'
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /O2 "$PSScriptRoot/ModernEffectOpacityTest.cpp" "/Fo$output/" "/Fe$output/test.exe" user32.lib oleaut32.lib ole32.lib
if($LASTEXITCODE -ne 0){throw 'Modern effect opacity test build failed'}
Push-Location $ClientPath
try {& "$output/test.exe";if($LASTEXITCODE -ne 0){throw "Modern effect opacity test failed: $LASTEXITCODE"}}finally{Pop-Location}
