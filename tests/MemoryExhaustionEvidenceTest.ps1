$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null

$sound = Join-Path $PSScriptRoot '..\..\BeiDou-Client\Sound_DX8.dll'
if (-not (Test-Path -LiteralPath $sound)) { throw "Sound_DX8.dll not found: $sound" }
$outputDir = Join-Path $env:TEMP ('beidou-memory-evidence-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'MemoryExhaustionEvidenceTest.exe'
$ezorsia = Join-Path $PSScriptRoot '..\ezorsia'
$detours = Join-Path $PSScriptRoot '..\detours'
$sources = @(
    (Join-Path $PSScriptRoot 'MemoryExhaustionEvidenceTest.cpp'),
    (Join-Path $ezorsia 'SoundAllocationFallback.cpp'),
    (Join-Path $ezorsia 'MemoryTelemetry.cpp'),
    (Join-Path $ezorsia 'ClientLog.cpp'),
    (Join-Path $ezorsia 'CrashReporter.cpp'),
    (Join-Path $ezorsia 'TargetedCrashSnapshot.cpp'),
    (Join-Path $ezorsia 'ClientDiagnostics.cpp')
)
& cl.exe /nologo /std:c++17 /O2 /EHa /DBEIDOU_SOUND_FALLBACK_TEST $sources "/Fo:$outputDir\" "/Fe:$output" /link /LARGEADDRESSAWARE (Join-Path $detours 'detours.lib') Dbghelp.lib Advapi32.lib User32.lib
if ($LASTEXITCODE -ne 0) { throw 'MemoryExhaustionEvidenceTest compilation failed.' }

& $output '--sound' (Resolve-Path -LiteralPath $sound).Path
if ($LASTEXITCODE -ne 0) { throw 'Sound predecode fallback test failed.' }
& $output '--dump'
if ($LASTEXITCODE -ne 0) { throw 'Dump reserve / memory evidence test failed.' }
"PASS MemoryExhaustionEvidenceTest artifacts: $outputDir"
