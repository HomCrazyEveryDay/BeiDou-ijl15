$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null

$outputDir = Join-Path $env:TEMP ('beidou-client-log-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'ClientLogTest.exe'
$source = Join-Path $PSScriptRoot 'ClientLogTest.cpp'
$implementation = Join-Path $PSScriptRoot '..\ezorsia\ClientLog.cpp'
$crashReporter = Join-Path $PSScriptRoot '..\ezorsia\CrashReporter.cpp'
$diagnostics = Join-Path $PSScriptRoot '..\ezorsia\ClientDiagnostics.cpp'
& cl.exe /nologo /std:c++17 /O2 /EHsc $source $implementation $crashReporter $diagnostics "/Fo:$outputDir\" "/Fe:$output" /link Dbghelp.lib Advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'ClientLogTest compilation failed.' }
$runOne = & $output '--run-id'
if ($LASTEXITCODE -ne 0) { throw 'First run ID process failed.' }
$runTwo = & $output '--run-id'
if ($LASTEXITCODE -ne 0) { throw 'Second run ID process failed.' }
if ($runOne -notmatch '^[0-9a-f]{32}$' -or $runTwo -notmatch '^[0-9a-f]{32}$' -or $runOne -eq $runTwo) {
    throw 'Independent processes must have distinct secure run IDs.'
}
& $output (Join-Path $outputDir 'logs')
if ($LASTEXITCODE -ne 0) { throw 'Primary directory test failed.' }

# A file occupying the logs directory forces the same fallback as an unwritable installation.
$fallbackDir = Join-Path $outputDir 'fallback'
New-Item -ItemType Directory -Path $fallbackDir | Out-Null
Copy-Item -LiteralPath $output -Destination (Join-Path $fallbackDir 'ClientLogTest.exe')
New-Item -ItemType File -Path (Join-Path $fallbackDir 'logs') | Out-Null
$originalLocalAppData = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = $outputDir
    & (Join-Path $fallbackDir 'ClientLogTest.exe') (Join-Path $outputDir 'ZhuMengLauncher\logs') 'pending'
    if ($LASTEXITCODE -ne 0) { throw 'LocalAppData fallback test failed.' }
} finally {
    $env:LOCALAPPDATA = $originalLocalAppData
}
"PASS ClientLogTest artifacts: $outputDir"
