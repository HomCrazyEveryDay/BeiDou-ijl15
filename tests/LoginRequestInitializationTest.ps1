$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-login-request-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\ezorsia\ClientCrashFixes.cpp'))
$start = $source.IndexOf('using ConstructLogin =')
$end = $source.IndexOf('bool InstallLoginRequestInitialization()', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Cannot locate production constructor hook.' }
[IO.File]::WriteAllText((Join-Path $outputDir 'LoginRequestUnderTest.h'), $source.Substring($start, $end-$start))
$output = Join-Path $outputDir 'LoginRequestInitializationTest.exe'
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$outputDir" (Join-Path $PSScriptRoot 'LoginRequestInitializationTest.cpp') "/Fo:$outputDir\" "/Fe:$output"
if ($LASTEXITCODE -ne 0) { throw 'Login request test compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Login request test failed.' }
