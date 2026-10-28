param([string]$ClientRoot=(Join-Path $PSScriptRoot '../../BeiDou-Client'),[string]$DataRoot='')
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $env:TEMP ('beidou-modern-fashion-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /O2 "/I$PSScriptRoot/../ezorsia" "$PSScriptRoot/ModernFashionResourcesTest.cpp" "/Fo$output\" "/Fe$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO ole32.lib
if($LASTEXITCODE -ne 0){throw 'Modern fashion test compilation failed'}
if($DataRoot){& "$output/test.exe" (Resolve-Path $ClientRoot).Path (Resolve-Path $DataRoot).Path}
else{& "$output/test.exe" (Resolve-Path $ClientRoot).Path}
if($LASTEXITCODE -ne 0){throw "Modern fashion tests failed: $LASTEXITCODE"}
