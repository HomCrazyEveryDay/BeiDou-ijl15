param([string]$OutputDirectory, [string]$PatchedImage)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$workspace = Split-Path $repo
if (!$OutputDirectory) { $OutputDirectory = Join-Path $workspace '.tmp/codex-visitors/set-tooltip/tests' }
if (!$PatchedImage) { $PatchedImage = Join-Path $workspace '.tmp/codex-visitors/set-tooltip/patched/Data/Etc/SetItemInfo.img' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$client = Join-Path $workspace 'BeiDou-Client'
$vs = 'C:/Program Files/Microsoft Visual Studio/18/Community'
$env:Path = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer;$env:Path"
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -vcvars_ver=14.29' | Out-Null
New-Item -ItemType Directory -Force $output | Out-Null
if ((Get-FileHash "$client/BeiDou.exe").Hash -ne '6C4FC608E5B44708D19F07D5B88729126539E919D0B79035DA76610158F355C2') { throw 'Unsupported EXE SHA-256' }
if (!(Test-Path -LiteralPath $PatchedImage)) { throw 'Generate the reviewed scratch IMG before the native resource test' }
$fixture = Join-Path $output 'fixture'
New-Item -ItemType Directory -Force "$fixture/Etc" | Out-Null
Copy-Item -LiteralPath $PatchedImage -Destination "$fixture/Etc/SetItemInfo.img"
foreach ($group in @(
    @{Folder='Cap'; Start=1003115}, @{Folder='Longcoat'; Start=1052277},
    @{Folder='Shoes'; Start=1072449}, @{Folder='Glove'; Start=1082277}
)) {
    New-Item -ItemType Directory -Force "$fixture/Character/$($group.Folder)" | Out-Null
    foreach ($item in $group.Start..($group.Start+4)) {
        $name = '{0:D8}.img' -f $item
        Copy-Item -LiteralPath "$client/Data/Character/$($group.Folder)/$name" -Destination "$fixture/Character/$($group.Folder)/$name"
    }
}
& cl.exe /nologo /O2 /EHsc /std:c++17 /DSET_ITEM_TOOLTIP_TEST "$PSScriptRoot/SetItemTooltipTest.cpp" "/Fo$output/tooltip.obj" "/Fe$output/tooltip.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO OleAut32.lib Ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Tooltip test build failed' }
& "$output/tooltip.exe" "$client/BeiDou.exe" $client $fixture $output
if ($LASTEXITCODE -ne 0) { throw "Tooltip test failed: $LASTEXITCODE" }
