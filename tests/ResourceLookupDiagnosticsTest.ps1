param([Parameter(Mandatory = $true)][string]$ClientExe)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found.' }
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = Join-Path $env:TEMP ('beidou-resource-lookup-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
# Rebase explicit addresses in the observer and harness; the fixture's original
# instruction bytes remain intact for signature validation. No game entry runs.
$source = [IO.File]::ReadAllText("$repo/ezorsia/ResourceLookupDiagnostics.cpp")
$test = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ResourceLookupDiagnosticsTest.cpp'))
$test = $test.Replace('../ezorsia/ResourceLookupDiagnostics.cpp', 'ResourceLookupDiagnosticsUnderTest.cpp')
foreach ($name in @('source', 'test')) {
    $value = Get-Variable -Name $name -ValueOnly
    $value = [regex]::Replace($value, '(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)', '0x30')
    $value = [regex]::Replace($value, '(?i)\b00(?=[4-9ab][0-9a-f]{5}h\b)', '30')
    Set-Variable -Name $name -Value $value
}
$test = $test.Replace('../ezorsia/ClientLog.h', 'ClientLog.h').Replace('../ezorsia/ClientDiagnostics.h', 'ClientDiagnostics.h')
[IO.File]::WriteAllText("$output/ResourceLookupDiagnosticsUnderTest.cpp", $source)
[IO.File]::WriteAllText("$output/ResourceLookupDiagnosticsTest.cpp", $test)
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT "$output/ResourceLookupDiagnosticsTest.cpp" "/I$repo/ezorsia" "/Fo:$output\" "/Fe:$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE) { throw 'Resource lookup diagnostic test compilation failed.' }
& "$output/test.exe" (Resolve-Path -LiteralPath $ClientExe).Path
if ($LASTEXITCODE) { throw "Resource lookup diagnostic test failed: $LASTEXITCODE" }
Write-Output "Artifacts: $output"
