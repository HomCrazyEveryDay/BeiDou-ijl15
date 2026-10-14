param([string]$ClientDirectory = (Join-Path $PSScriptRoot '../../BeiDou-Client'))
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo ('../.tmp/string-conversion-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT (Join-Path $PSScriptRoot 'LegacyStringConversionFixTest.cpp') (Join-Path $repo 'ezorsia/LegacyStringConversionFix.cpp') "/I$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link "$repo/detours/detours.lib" /LARGEADDRESSAWARE
if ($LASTEXITCODE) { throw 'String conversion test compilation failed.' }
foreach ($mode in @('normal','earlier-acp-hook')) {
    $arguments = @((Resolve-Path -LiteralPath $ClientDirectory).Path)
    if ($mode -ne 'normal') { $arguments += '--earlier-acp-hook' }
    & "$output/test.exe" @arguments 2>&1 | Tee-Object -FilePath "$output/$mode.txt"
    if ($LASTEXITCODE) { throw "String conversion test failed: $LASTEXITCODE; artifacts: $output" }
}
Write-Output "Artifacts: $output"
