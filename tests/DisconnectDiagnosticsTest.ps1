$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output = Join-Path $env:TEMP ('beidou-disconnect-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$repo = Split-Path $PSScriptRoot
$sources = @('DisconnectDiagnostics.cpp', 'ClientDiagnostics.cpp', 'ClientLog.cpp', 'Memory.cpp') | ForEach-Object { Join-Path $repo "ezorsia/$_" }
& cl.exe /nologo /std:c++17 /O2 /EHsc (Join-Path $PSScriptRoot 'DisconnectDiagnosticsTest.cpp') @sources "/I:$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link "$repo/detours/detours.lib" Advapi32.lib Ws2_32.lib
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic test build failed' }
& "$output/test.exe"
if ($LASTEXITCODE -ne 0) { throw "Diagnostic test failed: $LASTEXITCODE; artifacts: $output" }
