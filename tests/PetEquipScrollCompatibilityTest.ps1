param([string]$ClientExe = (Join-Path $PSScriptRoot '../../BeiDou-Client/BeiDou.exe'))
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo ('../.tmp/pet-scroll-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT (Join-Path $PSScriptRoot 'PetEquipScrollCompatibilityTest.cpp') (Join-Path $repo 'ezorsia/PetEquipScrollCompatibility.cpp') "/I:$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Pet scroll test compilation failed' }
& "$output/test.exe" (Resolve-Path -LiteralPath $ClientExe).Path
if ($LASTEXITCODE -ne 0) { throw "Pet scroll test failed: $LASTEXITCODE; $output" }
Write-Output "Artifacts: $output"
