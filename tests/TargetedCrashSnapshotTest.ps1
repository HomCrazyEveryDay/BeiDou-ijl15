$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo ('../.tmp/targeted-snapshot-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT (Join-Path $PSScriptRoot 'TargetedCrashSnapshotTest.cpp') "$repo/ezorsia/ClientLog.cpp" "$repo/ezorsia/ClientDiagnostics.cpp" "/I:$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link Advapi32.lib Dbghelp.lib
if ($LASTEXITCODE -ne 0) { throw 'Targeted snapshot test build failed' }
foreach ($mode in @('disabled','normal','writer-failure')) {
    $caseDir = Join-Path $output $mode
    New-Item -ItemType Directory -Path $caseDir | Out-Null
    Copy-Item "$output/test.exe" "$caseDir/test.exe"
    & "$caseDir/test.exe" (Resolve-Path "$repo/../BeiDou-Client/BeiDou.exe").Path $mode
    if ($LASTEXITCODE -ne 0) { throw "Targeted snapshot test failed: $mode / $LASTEXITCODE; $output" }
}
Write-Output "Artifacts: $output"
