$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo=Split-Path $PSScriptRoot
$output=Join-Path $env:TEMP ('evan-blaze-dispatch-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /DEVAN_RUNTIME_TEST "$PSScriptRoot/EvanBlazeDispatchTest.cpp" "$repo/ezorsia/EvanRuntime.cpp" "/Fo$output\" "/Fe$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
if($LASTEXITCODE -ne 0){throw 'Build failed'}
& "$output/test.exe" "$repo/../BeiDou-Client/BeiDou.exe"
if($LASTEXITCODE -ne 0){throw "Dispatch test failed: $LASTEXITCODE"}
