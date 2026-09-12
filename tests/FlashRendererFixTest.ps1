$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output = Join-Path $env:TEMP ('beidou-flash-fix-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$repo = Split-Path $PSScriptRoot
& cl.exe /nologo /std:c++17 /O2 /EHsc (Join-Path $PSScriptRoot 'FlashRendererFixTest.cpp') (Join-Path $repo 'ezorsia/FlashRendererFix.cpp') "/I:$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link "$repo/detours/detours.lib"
if ($LASTEXITCODE -ne 0) { throw 'Flash fix test build failed' }
$client = (Resolve-Path (Join-Path $repo '../BeiDou-Client')).Path
& "$output/test.exe" $client
if ($LASTEXITCODE -ne 0) { throw "Flash fix test failed: $LASTEXITCODE; artifacts: $output" }
