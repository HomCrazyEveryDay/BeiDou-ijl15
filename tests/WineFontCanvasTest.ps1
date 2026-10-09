param([string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\wine-font-tests'))
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio x86 C++ tools not found.' }
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDirectory).ProviderPath
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT /Y- `
    (Join-Path $PSScriptRoot 'WineFontCanvasTest.cpp') `
    "/Fo:$output\" "/Fe:$output\WineFontCanvasTest.exe" `
    /link (Join-Path $PSScriptRoot '..\detours\detours.lib') Advapi32.lib User32.lib Gdi32.lib OleAut32.lib
if ($LASTEXITCODE -ne 0) { throw 'Wine font canvas test build failed.' }
Write-Output "Run $output\WineFontCanvasTest.exe under Wine with the client directory, baseline|patched, and output PPM."
