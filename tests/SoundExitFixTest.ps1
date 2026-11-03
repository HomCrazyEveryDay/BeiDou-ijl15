$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo ('../.tmp/sound-exit-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT "$PSScriptRoot/SoundExitFixTest.cpp" "$repo/ezorsia/SoundExitFix.cpp" "$repo/ezorsia/SoundAllocationFallback.cpp" "/Fo:$output\" "/Fe:$output/test.exe" /link "$repo/detours/detours.lib"
if ($LASTEXITCODE -ne 0) { throw 'Sound exit test build failed' }
$sound = (Resolve-Path "$repo/../BeiDou-Client/Sound_DX8.dll").Path
foreach ($case in @('baseline', 'fixed', 'runtime', 'unload')) {
    $log = Join-Path $output "$case.log"
    $process = Start-Process -FilePath "$output/test.exe" -ArgumentList @('"' + $sound + '"', $case) -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError "$output/$case.err"
    $finished = $process.WaitForExit(3000)
    if (!$finished) { $process.Kill(); $process.WaitForExit() } # Only this test's own child.
    $text = Get-Content -LiteralPath $log -Raw
    if ($case -eq 'baseline') {
        if ($finished -or $text -notmatch 'READY baseline') { throw "Baseline did not reproduce native exit hang: $text" }
        Write-Output 'PASS baseline: real SOUND_DX8 finalizer reproduced hang after ExitProcess'
    } else {
        if (!$finished -or $process.ExitCode -ne 0) { throw "$case failed: exit=$($process.ExitCode), finished=$finished; $text" }
        if ($case -eq 'fixed' -and $text -notmatch 'native_finalizer_skipped=1') { throw 'Fixed exit did not exercise shutdown guard' }
        if ($case -ne 'fixed' -and $text -notmatch "PASS $case nativeCleanup=1") { throw "Native cleanup was not verified: $case" }
        Write-Output "PASS $case"
    }
}
Write-Output "PASS all 4 sound exit cases; artifacts: $output"
