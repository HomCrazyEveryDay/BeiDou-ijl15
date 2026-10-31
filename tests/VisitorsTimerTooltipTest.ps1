param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$workspace = Split-Path $repo
if (!$OutputDirectory) { $OutputDirectory = Join-Path $workspace '.tmp/osss-timer-tooltip/tests' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$vs = 'C:/Program Files/Microsoft Visual Studio/18/Community'
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -vcvars_ver=14.29' | Out-Null
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /O2 /EHsc /std:c++17 /DVISITORS_TIMER_TOOLTIP_TEST "$PSScriptRoot/VisitorsTimerTooltipTest.cpp" "/Fo$output/timer.obj" "/Fe$output/timer.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'Timer tooltip test build failed' }
& "$output/timer.exe" "$workspace/BeiDou-Client/BeiDou.exe"
if ($LASTEXITCODE -ne 0) { throw "Timer tooltip test failed: $LASTEXITCODE" }
