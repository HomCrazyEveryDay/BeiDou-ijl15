$ErrorActionPreference = 'Stop'
$vs = 'C:/Program Files/Microsoft Visual Studio/18/Community'
$env:Path = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer;$env:Path"
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -vcvars_ver=14.29' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = [IO.Path]::GetFullPath((Join-Path $repo '../.tmp/codex-visitors/mount-set-dll/tests'))
New-Item -ItemType Directory -Force $output | Out-Null
$client = "$repo/../BeiDou-Client/BeiDou.exe"
if ((Get-FileHash $client).Hash -ne '6C4FC608E5B44708D19F07D5B88729126539E919D0B79035DA76610158F355C2') { throw 'Unsupported EXE SHA-256' }
& cl.exe /nologo /O2 /EHsc /std:c++17 /DSET_ITEM_BONUS_TEST "$PSScriptRoot/SetItemBonusTest.cpp" "/Fo$output/set-item-bonus.obj" "/Fe$output/set-item-bonus.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'Set item bonus test build failed' }
& "$output/set-item-bonus.exe" $client
if ($LASTEXITCODE -ne 0) { throw "Set item bonus test failed: $LASTEXITCODE" }
