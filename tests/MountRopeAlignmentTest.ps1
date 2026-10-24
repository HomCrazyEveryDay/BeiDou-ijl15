param([string]$ClientExe = (Join-Path $PSScriptRoot '../../BeiDou-Client/BeiDou.exe'))
$ErrorActionPreference = 'Stop'
$mapPath = Join-Path $PSScriptRoot '../../BeiDou-Server/gms-server/wz/Map.wz/Map/Map2/222000001.img.xml'
$mapXml = [xml](Get-Content -LiteralPath $mapPath -Raw)
$wellRope = $mapXml.SelectSingleNode('/imgdir/imgdir[@name="ladderRope"]/imgdir[@name="1"]')
if (-not $wellRope) { throw 'Well rope is missing from map 222000001' }
$wellCoordinates = foreach ($field in @('x', 'y1', 'y2')) {
    $node = $wellRope.SelectSingleNode("int[@name='$field']")
    if (-not $node) { throw "Well rope coordinate is missing: $field" }
    [int]$node.value
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output = Join-Path $PSScriptRoot '../out/tests/mount-rope-alignment'
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /utf-8 /O2 "$PSScriptRoot/MountRopeAlignmentTest.cpp" "/Fo$output/" "/Fe$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
if ($LASTEXITCODE -ne 0) { throw 'Mount rope alignment test build failed' }
# Windows may place an unrelated process mapping at a required original code
# address. Retry only that reservation failure, never an assertion or crash.
for ($attempt = 0; $attempt -lt 5; $attempt++) {
    & "$output/test.exe" $ClientExe @wellCoordinates
    if ($LASTEXITCODE -eq 0) { return }
    if ($LASTEXITCODE -ne 2) { throw "Mount rope alignment test failed: $LASTEXITCODE" }
}
throw 'Native test addresses were occupied in all five isolated processes'
