param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) {
    throw 'Visual Studio C++ build tools were not found.'
}

$devShell = Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShell
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null

$outputDir = Join-Path $env:TEMP 'beidou-second-pendant-rules-test'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'SecondPendantRulesTest.exe'
$source = Join-Path $PSScriptRoot 'SecondPendantRulesTest.cpp'

& cl.exe /nologo /std:c++17 /O2 /DNDEBUG /EHsc $source "/Fo:$outputDir\" "/Fe:$output"
if ($LASTEXITCODE -ne 0) {
    throw "SecondPendantRulesTest compilation failed with exit code $LASTEXITCODE."
}

& $output
if ($LASTEXITCODE -ne 0) {
    throw "SecondPendantRulesTest failed with exit code $LASTEXITCODE."
}

'PASS SecondPendantRulesTest: only slot 51 ignores the native expansion-expiration gate'
