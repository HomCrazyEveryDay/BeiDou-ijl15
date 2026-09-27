param([string]$ClientExe = (Join-Path $PSScriptRoot '..\..\BeiDou-Client\BeiDou.exe'))
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-resource-guards-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'ResourceReadGuardsTest.exe'
$sourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..\ezorsia')).Path
# Native relative calls remain valid at the isolated image base. Rebase the
# guard's explicit EXE addresses too; never execute the client entry point.
$guard = [IO.File]::ReadAllText((Join-Path $sourceDirectory 'ResourceReadGuards.h'))
$test = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ResourceReadGuardsTest.cpp'))
$guard = [regex]::Replace($guard, '(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)', '0x30')
$test = [regex]::Replace($test, '(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)', '0x30')
[IO.File]::WriteAllText((Join-Path $outputDir 'ResourceReadGuardsUnderTest.h'), $guard)
$testSource = Join-Path $outputDir 'ResourceReadGuardsTest.cpp'
[IO.File]::WriteAllText($testSource, $test)
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$sourceDirectory" $testSource "/Fo:$outputDir\" "/Fe:$output" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'Resource guard test compilation failed.' }
& $output (Resolve-Path -LiteralPath $ClientExe).Path
if ($LASTEXITCODE -ne 0) { throw "Resource guard test failed: $LASTEXITCODE" }
