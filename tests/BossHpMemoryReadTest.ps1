$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-boss-hp-read-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\ezorsia\BossHP.cpp'))
# Compile the production bodies, with only the native singleton and tooltip replaced.
$functions = @(
    'bool BossHP::TryGetMiniMapWidth\(int& width\)',
    'void BossHP::DrawBossHpNumberIfNeed\(\)'
)
$bodies = foreach ($signature in $functions) {
    $match = [regex]::Match($source, '(?s)' + $signature + '\s*\{.*?\r?\n\}')
    if (-not $match.Success) { throw "Cannot locate production function: $signature" }
    $match.Value
}
[IO.File]::WriteAllText((Join-Path $outputDir 'BossHpReadUnderTest.h'), ($bodies -join "`r`n"))
$output = Join-Path $outputDir 'BossHpMemoryReadTest.exe'
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$outputDir" (Join-Path $PSScriptRoot 'BossHpMemoryReadTest.cpp') "/Fo:$outputDir\" "/Fe:$output"
if ($LASTEXITCODE -ne 0) { throw 'Boss HP test compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Boss HP test failed.' }
