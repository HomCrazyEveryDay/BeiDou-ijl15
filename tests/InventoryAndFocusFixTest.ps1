$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo ('../.tmp/inventory-focus-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT (Join-Path $PSScriptRoot 'InventoryAndFocusFixTest.cpp') (Join-Path $repo 'ezorsia/InventoryRefreshFix.cpp') (Join-Path $repo 'ezorsia/CharacterSelectFocusFix.cpp') "/I:$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'Inventory/focus test compilation failed' }
& "$output/test.exe" (Resolve-Path "$repo/../BeiDou-Client/BeiDou.exe").Path
if ($LASTEXITCODE -ne 0) { throw "Inventory/focus test failed: $LASTEXITCODE; $output" }
Write-Output "Artifacts: $output"
