$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-tooltip-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'SkillTooltipLayoutTest.exe'
$sourceDir = Join-Path $PSScriptRoot '..\ezorsia'
$source = [IO.File]::ReadAllText((Join-Path $sourceDir 'SkillTooltipLayout.cpp'))
$source = [regex]::Replace($source, '(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)', '0x30')
[IO.File]::WriteAllText((Join-Path $outputDir 'SkillTooltipLayoutUnderTest.h'), $source)
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$outputDir" "/I$sourceDir" (Join-Path $PSScriptRoot 'SkillTooltipLayoutTest.cpp') "/Fo:$outputDir\" "/Fe:$output" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'SkillTooltipLayoutTest compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'SkillTooltipLayoutTest failed.' }
