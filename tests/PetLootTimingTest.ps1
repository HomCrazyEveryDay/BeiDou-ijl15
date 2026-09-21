$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-pet-timing-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'PetLootTimingTest.exe'
& cl.exe /nologo /std:c++17 /O2 /EHsc (Join-Path $PSScriptRoot 'PetLootTimingTest.cpp') "/Fo:$outputDir\" "/Fe:$output"
if ($LASTEXITCODE -ne 0) { throw 'PetLootTimingTest compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'PetLootTimingTest failed.' }
