$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output = Join-Path $env:TEMP ('beidou-lifecycle-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$repo = Split-Path $PSScriptRoot
$sources = @('DisconnectDiagnostics.cpp','ClientDiagnostics.cpp','ClientLog.cpp','CrashReporter.cpp','Memory.cpp','ProcessExitMonitor.cpp') | ForEach-Object { Join-Path $repo "ezorsia/$_" }
& cl.exe /nologo /std:c++17 /EHsc (Join-Path $PSScriptRoot 'LifecycleLoggingHarness.cpp') @sources "/I$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link "$repo/detours/detours.lib" Advapi32.lib Ws2_32.lib Dbghelp.lib
if ($LASTEXITCODE -ne 0) { throw 'Harness build failed' }
Copy-Item "$repo/out/Release/BeiDouExitMonitor.exe" "$output/BeiDouExitMonitor.exe"
foreach ($mode in @('handled','terminate','crash','dumpfail','disabled','conditional','generic','limit','provenance','monitor-exit','monitor-terminate','monitor-crash','monitor-external','monitor-retention')) {
    if ($mode -eq 'monitor-retention') {
        for ($i=0;$i -lt 12;$i++) {
            $fixture=Join-Path "$output/logs" "ijl15-crash-fixture-$i.dmp"
            [IO.File]::WriteAllBytes($fixture,[byte[]](1,2,3))
        }
        $large=Join-Path "$output/logs" 'ijl15-crash-fixture-large.dmp'
        $stream=[IO.File]::Create($large);$stream.SetLength(257MB);$stream.Dispose()
        [IO.File]::WriteAllText((Join-Path "$output/logs" 'unrelated.dmp'),'preserve')
    }
    $testProcess = Start-Process "$output/test.exe" -ArgumentList $mode -WindowStyle Hidden -PassThru
    if ($mode -eq 'monitor-external') {
        $ready = $false
        for ($attempt=0;$attempt -lt 50;$attempt++) {
            $exitFile=Get-ChildItem "$output/logs" -Filter "ijl15-exit-*pid$($testProcess.Id).log" -ErrorAction SilentlyContinue
            if ($exitFile -and (Get-Content $exitFile.FullName -Raw).Contains('event=monitor_ready')) { $ready=$true;break }
            Start-Sleep -Milliseconds 100
        }
        if (!$ready) { throw 'External monitor did not become ready' }
        $readyText=Get-Content $exitFile.FullName -Raw
        if ($readyText -notmatch 'monitorPid=(\d+)') { throw 'Missing monitor pid' }
        $watchProcess=Get-Process -Id ([int]$matches[1])
        $cpuBefore=$watchProcess.TotalProcessorTime.TotalMilliseconds
        Start-Sleep -Seconds 1
        $watchProcess.Refresh()
        Write-Output "Idle monitor: privateBytes=$($watchProcess.PrivateMemorySize64) cpuMsPerSecond=$($watchProcess.TotalProcessorTime.TotalMilliseconds-$cpuBefore)"
        $testProcess.Kill() # Only our isolated test process, never the game.
    }
    if (!$testProcess.WaitForExit(20000)) { throw "Harness timeout: $mode, pid=$($testProcess.Id)" }
    $files = @(Get-ChildItem "$output/logs" -Filter "*pid$($testProcess.Id)*")
    if ($mode.StartsWith('monitor-')) {
        for ($attempt=0;$attempt -lt 50;$attempt++) {
            $exitFile=Get-ChildItem "$output/logs" -Filter "ijl15-exit-*pid$($testProcess.Id).log" -ErrorAction SilentlyContinue
            if ($exitFile -and (Get-Content $exitFile.FullName -Raw).Contains('event=process_exited')) { break }
            Start-Sleep -Milliseconds 100
        }
        $actualHex='{0:X8}' -f $testProcess.ExitCode
        if (!$exitFile -or !(Get-Content $exitFile.FullName -Raw).Contains("event=process_exited exitCode=$actualHex")) { throw "Final exit code not captured: $mode" }
        Write-Output "PASS $mode actualExitCode=$actualHex"
        if ($mode -eq 'monitor-retention') {
            $remaining=@(Get-ChildItem "$output/logs/ijl15-crash-*.dmp")
            if ($remaining.Count -gt 10 -or ($remaining | Measure-Object Length -Sum).Sum -gt 256MB) { throw 'Dump retention limit exceeded' }
            if (!(Test-Path "$output/logs/unrelated.dmp")) { throw 'Retention deleted unrelated file' }
        }
        continue
    }
    $log = ($files | Where-Object Extension -eq '.log' | Get-Content -Raw) -join "`n"
    $required = switch ($mode) {
        handled { @('first_chance_not_necessarily_fatal code=C0000094','harness_exception_handled worker=1','api=ExitProcess exitCode=00000017','skillId=22161003') }
        terminate { @('api=TerminateProcess exitCode=00000025') }
        crash { @('unhandled_exception code=C0000005','written=1 win32Error=0') }
        dumpfail { @('unhandled_exception code=C0000005','written=0 win32Error=5') }
        disabled { @() }
        conditional { @('conditional_dump_trigger','type=conditional_first_chance written=1') }
        provenance { @('kind=resource_request','kind=queued_path_candidate','input=Effect/Test.img/source','path=Effect/Test.img/source/1','pathCandidates=1') }
        limit { @('conditional_dump_skipped reason=session_limit limit=4','attempt=4') }
        generic { @('reason=observed_exception','type=conditional_first_chance written=1','conditional_dump_skipped reason=duplicate') }
    }
    foreach ($entry in $required) { if (!$log.Contains($entry)) { throw "Missing evidence: $mode / $entry; $output" } }
    if ($mode -notin @('disabled','conditional','limit') -and !$log.Contains('filterHook=1')) { throw "Hook installation failed: $output" }
    if ($mode -eq 'disabled' -and $log.Contains('diagnostic_ready')) { throw 'Disabled observer was installed' }
    if ($mode -eq 'handled' -and $testProcess.ExitCode -ne 23) { throw 'ExitProcess behavior changed' }
    if ($mode -eq 'terminate' -and $testProcess.ExitCode -ne 37) { throw 'TerminateProcess behavior changed' }
    if ($mode -in @('disabled','provenance') -and $testProcess.ExitCode -ne 0) { throw 'Disabled test failed' }
    if ($mode -eq 'crash' -and !($files | Where-Object { $_.Extension -eq '.dmp' -and $_.Length -gt 0 })) { throw 'No actual dump produced' }
    if ($mode -in @('conditional','generic')) {
        if (@($files | Where-Object Extension -eq '.dmp').Count -ne 1) { throw 'Conditional dump is not limited to one' }
        if ($testProcess.ExitCode -ne 0) { throw 'Handled exception behavior changed' }
    }
    if ($mode -eq 'limit' -and (@($files | Where-Object Extension -eq '.dmp').Count -ne 4 -or $testProcess.ExitCode -ne 0)) { throw 'Distinct exception budget failed' }
    Write-Output "PASS $mode exitCode=$($testProcess.ExitCode)"
}
Write-Output "Artifacts: $output"
