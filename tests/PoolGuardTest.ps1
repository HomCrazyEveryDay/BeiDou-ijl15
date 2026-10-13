param([Parameter(Mandatory = $true)][string]$ClientExe)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo ('../.tmp/pool-guard-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& cl.exe /nologo /std:c++17 /O2 /Oy- /EHsc /MT /D_CRT_SECURE_NO_WARNINGS (Join-Path $PSScriptRoot 'PoolGuardTest.cpp') (Join-Path $repo 'ezorsia/PoolGuard.cpp') "/I$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link "$repo/detours/detours.lib" /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE) { throw 'Pool guard test compilation failed.' }
& "$output/test.exe" (Resolve-Path -LiteralPath $ClientExe).Path 2>&1 | Tee-Object -FilePath "$output/result.txt"
if ($LASTEXITCODE) { throw "Pool guard test failed: $LASTEXITCODE; artifacts: $output" }
$sources = @('PoolGuard.cpp', 'CrashReporter.cpp', 'TargetedCrashSnapshot.cpp', 'ClientLog.cpp', 'ClientDiagnostics.cpp') | ForEach-Object { Join-Path $repo "ezorsia/$_" }
& cl.exe /nologo /std:c++17 /O2 /Oy- /EHsc /MT /D_CRT_SECURE_NO_WARNINGS /DPOOL_GUARD_REAL_REPORT (Join-Path $PSScriptRoot 'PoolGuardTest.cpp') @sources "/I$repo/ezorsia" "/Fo:$output\" "/Fe:$output/capture.exe" /link "$repo/detours/detours.lib" Dbghelp.lib Advapi32.lib /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE) { throw 'Pool guard capture test compilation failed.' }
& "$output/capture.exe" (Resolve-Path -LiteralPath $ClientExe).Path
if ($LASTEXITCODE) { throw 'Pool guard real capture failed.' }
$dumpFiles = @(Get-ChildItem "$output/logs" -Filter '*_pool-guard-*.dmp')
$reports = @(Get-ChildItem "$output/logs" -Filter '*_pool-guard-*.txt')
if ($dumpFiles.Count -ne 2 -or $reports.Count -ne 2) { throw 'Expected exactly two real diagnostic artifact pairs.' }
foreach ($report in $reports) {
    $text = [IO.File]::ReadAllText($report.FullName)
    if (!$text.Contains('reportType=pool_guard_detection') -or !$text.Contains('dumpWritten=true')) { throw 'Pool guard artifact capture not confirmed.' }
}
Write-Output "PASS actual diagnostic dump files=$($dumpFiles.Count) bytes=$(($dumpFiles | Measure-Object Length -Sum).Sum)"
Write-Output "Artifacts: $output"
