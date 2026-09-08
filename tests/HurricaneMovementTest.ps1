param(
    [string]$ClientExe = (Join-Path $PSScriptRoot '..\..\BeiDou-Client\BeiDou.exe'),
    [string]$SourceRevision
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) {
    throw 'Visual Studio C++ build tools were not found.'
}
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null

$outputDir = Join-Path $env:TEMP ('beidou-hurricane-movement-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$sourcePath = Join-Path $PSScriptRoot '..\ezorsia\dllmain.cpp'
if ($SourceRevision) {
    $source = (& git -C (Join-Path $PSScriptRoot '..') show "${SourceRevision}:ezorsia/dllmain.cpp") -join "`n"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the requested source revision.' }
} else {
    $source = [IO.File]::ReadAllText($sourcePath)
}
$globals = [regex]::Matches($source, '(?m)^static DWORD g_Hurricane[^\r\n]+;')
$start = $source.IndexOf('static int ReadClientMovementKey(')
$end = $source.IndexOf('__declspec(naked) void SuperOctopusAttackCadenceCave()', $start)
if ($globals.Count -eq 0 -or $start -lt 0 -or $end -le $start) {
    throw 'Cannot locate the production movement hooks.'
}
$underTest = (($globals | ForEach-Object Value) -join "`n") + "`n" + $source.Substring($start, $end - $start)
# Rebase the original absolute client addresses to the isolated test image.
$underTest = [regex]::Replace($underTest, '(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)', '0x30')
[IO.File]::WriteAllText((Join-Path $outputDir 'HurricaneMovementUnderTest.h'), $underTest)
$output = Join-Path $outputDir 'HurricaneMovementTest.exe'
$testSource = Join-Path $PSScriptRoot 'HurricaneMovementTest.cpp'
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$outputDir" $testSource "/Fo:$outputDir\" "/Fe:$output" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'HurricaneMovementTest compilation failed.' }
& $output (Resolve-Path -LiteralPath $ClientExe).Path
if ($LASTEXITCODE -ne 0) { throw "HurricaneMovementTest failed with exit code $LASTEXITCODE." }
