#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$workspace = Split-Path $repo
$tool = Join-Path $workspace 'tools/module-audit'
& (Join-Path $tool 'Initialize-Key.ps1') -RepoRoot $repo
$output = Join-Path $workspace ('.tmp/module-audit-test-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($output) | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$fixture = Join-Path $output '测试模块.dll'
& cl.exe /nologo /LD /O2 /MT (Join-Path $PSScriptRoot 'ModuleAuditFixture.cpp') "/Fo:$output/fixture.obj" "/Fe:$fixture" /link "/IMPLIB:$output/fixture.lib"
if ($LASTEXITCODE -ne 0) { throw 'Fixture build failed' }
$sources = @('ModuleAudit.cpp','ClientLog.cpp','ClientDiagnostics.cpp') | ForEach-Object { Join-Path $repo "ezorsia/$_" }
& cl.exe /nologo /std:c++17 /O2 /EHsc /DMODULE_AUDIT_TEST (Join-Path $PSScriptRoot 'ModuleAuditTest.cpp') @sources "/I:$repo/ezorsia" "/Fo:$output/" "/Fe:$output/test.exe" /link Advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Module audit test build failed' }
& "$output/test.exe" $output $fixture
if ($LASTEXITCODE -ne 0) { throw "Module audit test failed: $LASTEXITCODE" }
$decode = Join-Path $tool 'Read-ModuleAudit.ps1'
$all = @(& $decode -Path "$output/logs/*.bda" -PassThru)
$loads = @($all | Where-Object { $_.event -eq 'load_notification' -and $_.path -eq $fixture })
$unloads = @($all | Where-Object { $_.event -eq 'unload_notification' -and $_.path -eq $fixture })
if ($loads.Count -ne 2 -or $unloads.Count -ne 2) { throw "Transient load/unload lost: $($loads.Count)/$($unloads.Count)" }
if (-not ($all | Where-Object { $_.event -eq 'initial_module' -and $_.isExe })) { throw 'Startup executable absent' }
if (-not ($all | Where-Object { $_.event -eq 'snapshot_added' -and $_.path -eq $fixture })) { throw 'Periodic reconciliation absent' }
if (-not ($all | Where-Object event -eq 'snapshot_failed')) { throw 'Failed snapshot did not report coverage loss' }
if ($all | Where-Object { $_.event -eq 'snapshot_removed' -and $_.isExe }) { throw 'Failed snapshot fabricated executable unload' }
$metadata = @($all | Where-Object { $_.event -eq 'file_metadata' -and $_.path -eq $fixture })
$hash = (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()
if ($metadata.Count -ne 1 -or $metadata[0].sha256 -cne $hash) { throw 'Missing, repeated or incorrect fixture SHA256' }
if ($metadata[0].embeddedSignatureValidOffline) { throw 'Unsigned fixture was incorrectly signature validated' }
$bytes = [IO.File]::ReadAllBytes((Get-ChildItem "$output/logs/*.bda")[0].FullName)
if ([Text.Encoding]::UTF8.GetString($bytes).Contains('file_metadata')) { throw 'Plaintext leaked into encrypted log' }
$plainLogs = Get-ChildItem "$output/logs/*.log" | Get-Content -Raw
if (($plainLogs -join '').Contains('测试模块')) { throw 'Module information leaked to ordinary logs' }
$roundtrip = @(& $decode -Path "$output/crypto.bda" -PassThru)
if ($roundtrip[1].value -cne '测试.dll') { throw 'Unicode roundtrip failed' }
function MustReject([string]$name, [scriptblock]$action) {
    $rejected = $false
    try { & $action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Did not reject $name" }
}
$original = [IO.File]::ReadAllBytes("$output/crypto.bda")
$tampered = [byte[]]$original.Clone(); $tampered[-1] = $tampered[-1] -bxor 1
[IO.File]::WriteAllBytes("$output/tampered.bda", $tampered)
MustReject 'modified tag' { & $decode -Path "$output/tampered.bda" -PassThru }
$tampered = [byte[]]$original.Clone(); $tampered[20] = $tampered[20] -bxor 1
[IO.File]::WriteAllBytes("$output/header.bda", $tampered)
MustReject 'modified header' { & $decode -Path "$output/header.bda" -PassThru }
$tampered = [byte[]]$original.Clone(); $tampered[36] = 2
[IO.File]::WriteAllBytes("$output/order.bda", $tampered)
MustReject 'record sequence' { & $decode -Path "$output/order.bda" -PassThru }
[IO.File]::WriteAllBytes("$output/truncated.bda", $original[0..($original.Length-2)])
MustReject 'truncated final record' { & $decode -Path "$output/truncated.bda" -PassThru }
$recovered = @(& $decode -Path "$output/truncated.bda" -PassThru -AllowTruncatedTail -WarningAction SilentlyContinue)
if ($recovered.Count -ne 1) { throw 'Truncated-tail recovery did not preserve authenticated records' }
$wrongKey = [byte[]]::new(32); $wrongKey[0] = 42
[IO.File]::WriteAllBytes("$output/wrong.bin", $wrongKey)
MustReject 'wrong key' { & $decode -Path "$output/crypto.bda" -KeyPath "$output/wrong.bin" -PassThru }
& $decode -Path "$output/logs/*.bda" -OutputPath "$output/decoded.jsonl" | Out-Null
if (-not (Test-Path "$output/decoded.jsonl")) { throw 'JSONL export absent' }
for ($i=0; $i -lt 18; ++$i) {
    $seed = Join-Path $output ('logs/ijl15-modules-retention-test-{0:D2}.bda' -f $i)
    [IO.File]::WriteAllBytes($seed, $original)
    [IO.File]::SetLastWriteTimeUtc($seed, [datetime]::new(2000,1,1).AddSeconds($i))
}
& "$output/test.exe" $output $fixture stress
if ($LASTEXITCODE -ne 0) { throw 'Audit bounds stress failed' }
$retained = @(Get-ChildItem "$output/logs/*.bda")
if ($retained.Count -gt 16 -or ($retained | Where-Object Length -gt 4MB)) { throw 'Rotation/retention limit exceeded' }
$stress = @(& $decode -Path "$output/logs/*.bda" -PassThru)
$gap = @($stress | Where-Object { $_.event -eq 'coverage_gap' -and $_.reason -eq 'notification_queue_overflow' })
if ($gap.Count -ne 1 -or $gap[0].dropped -ne 18) { throw 'Queue overflow not counted exactly' }
if (-not ($stress | Where-Object { $_.event -eq 'load_notification' -and $_.pathTruncated })) { throw 'Long path truncation hidden' }
if (-not ($stress | Where-Object { $_.event -eq 'initial_module' -and $_.part -eq 2 })) { throw 'Inventory not renewed after rotation' }
Write-Host "PASS: snapshot/failure recovery, transient notifications, Unicode paths, file SHA256, signature, AES interoperability, tampering, wrong key, truncation, export, overflow, rotation/retention. Artifacts: $output"
