$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDirectory = Join-Path $PSScriptRoot ('..\out\tests\death-dialog-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDirectory | Out-Null
$output = Join-Path $outputDirectory 'DeathDialogDiagnosticsTest.exe'
& cl.exe /nologo /std:c++17 /O2 /EHsc (Join-Path $PSScriptRoot 'DeathDialogDiagnosticsTest.cpp') "/Fo:$outputDirectory\" "/Fe:$output" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'DeathDialogDiagnosticsTest compilation failed.' }
& $output (Join-Path $PSScriptRoot '..\..\BeiDou-Client\BeiDou.exe') (Join-Path $outputDirectory 'test.ini')
if ($LASTEXITCODE -ne 0) { throw 'DeathDialogDiagnosticsTest failed.' }
