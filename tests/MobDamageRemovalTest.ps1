param([switch]$WithoutRemovalFlush)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null

$outputDir = Join-Path $env:TEMP ('beidou-mob-damage-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\ezorsia\HpMpAlert.cpp'))
function Get-Block([string]$start, [string]$end) {
    $first = $source.IndexOf($start)
    $last = $source.IndexOf($end, $first + $start.Length)
    if ($first -lt 0 -or $last -le $first) { throw "Cannot locate production block: $start" }
    return $source.Substring($first, $last - $first)
}
$types = ([regex]::Matches($source, '(?m)^constexpr (?:DWORD|WORD|size_t) k(?:QueuedDamageTtlMs|MaxQueuedMobDamage|MaxMobDamagePerFrame|OpcodeKillMonster|MobPoolPtr)[^\r\n]+;') | ForEach-Object Value) -join "`n"
$types += "`n" + (Get-Block 'struct CInPacket {' 'static_assert(offsetof(CInPacket, DataLen)')
$types += Get-Block 'struct QueuedMobDamage {' 'struct BossVenomVisualTarget {'
$types += Get-Block 'static bool g_renderingServerMobDamage' 'static BossVenomVisualTarget g_bossVenomTargets'
[IO.File]::WriteAllText((Join-Path $outputDir 'MobDamageTypes.h'), $types)

$implementation = Get-Block 'static bool IsQueuedMobDamageExpired(' 'static bool ReadDamageResources('
$implementation += Get-Block 'static bool IsCurrentMobDamageField(' 'static unsigned char ClampAlert('
$implementation += Get-Block 'static void __fastcall ProcessPacket_Hook(' '} // namespace'
$implementation += Get-Block 'void UpdateQueuedMobDamageDisplay()' 'void OnMobDamageFieldInit()'
if ($WithoutRemovalFlush) {
    $implementation = $implementation.Replace('        FlushQueuedMobDamageBeforeRemoval(packet);', '')
}
[IO.File]::WriteAllText((Join-Path $outputDir 'MobDamageUnderTest.h'), $implementation)
$output = Join-Path $outputDir 'MobDamageRemovalTest.exe'
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$outputDir" (Join-Path $PSScriptRoot 'MobDamageRemovalTest.cpp') "/Fo:$outputDir\" "/Fe:$output"
if ($LASTEXITCODE -ne 0) { throw 'MobDamageRemovalTest compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'MobDamageRemovalTest failed.' }
