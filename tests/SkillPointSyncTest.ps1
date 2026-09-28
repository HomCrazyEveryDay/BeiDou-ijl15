$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $env:TEMP ('skill-point-sync-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
& cl.exe /nologo /O2 /EHsc /std:c++17 /DSKILL_POINT_SYNC_TEST /I"$PSScriptRoot/../ezorsia" "$PSScriptRoot/SkillPointSyncTest.cpp" "$PSScriptRoot/../ezorsia/SkillPointSync.cpp" "/Fo$output\" "/Fe$output/test.exe" /link /BASE:0x10000000
if($LASTEXITCODE -ne 0){throw 'Build failed'}
& "$output/test.exe"
if($LASTEXITCODE -ne 0){throw "SP test failed: $LASTEXITCODE"}
