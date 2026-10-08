$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo ('../.tmp/pcom-exit-fix-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT (Join-Path $PSScriptRoot 'PcomExitFixTest.cpp') (Join-Path $repo 'ezorsia/PcomExitFix.cpp') "/I:$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link "$repo/detours/detours.lib" /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'PCOM exit fix test build failed' }
$client = (Resolve-Path (Join-Path $repo '../BeiDou-Client')).Path
foreach ($case in @('baseline-guard', 'fixed-guard', 'fixed-heap', 'initialized-guard', 'initialized-heap', 'uninitialized', 'propagate')) {
    $result = & "$output/test.exe" $client $case 2>&1
    $exit = $LASTEXITCODE
    $result | Set-Content -LiteralPath (Join-Path $output "$case.log") -Encoding utf8
    $result | Where-Object { $_ -match '^(PASS|FAIL|pcom\.exit\.fix)' }
    if ($exit -ne 0 -or -not ($result -match "^PASS PcomExitFixTest $case ")) {
        throw "PCOM exit fix test failed: $case exit=$exit; artifacts: $output"
    }
}
Write-Output "PASS all 7 PCOM exit cases; artifacts: $output"
