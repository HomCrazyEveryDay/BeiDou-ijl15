$ErrorActionPreference = 'Stop'

$repo = Split-Path $PSScriptRoot
$client = Join-Path $repo '../BeiDou-Client/BeiDou.exe'
$source = Get-Content (Join-Path $repo 'ezorsia/dllmain.cpp') -Raw
$bytes = [IO.File]::ReadAllBytes($client)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$optional = $pe + 24
if ([BitConverter]::ToUInt16($bytes, $optional) -ne 0x10B) { throw 'Expected x86 PE client' }

$imageBase = [BitConverter]::ToUInt32($bytes, $optional + 28)
$sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
$sectionTable = $optional + [BitConverter]::ToUInt16($bytes, $pe + 20)
$patchRva = 0x008CBECA - $imageBase
$expected = [byte[]](0x8B, 0xDA, 0x4B, 0x4B, 0xF7, 0xDB, 0x1B, 0xDB, 0x83, 0xE3, 0xF6, 0x03, 0xD9)
$patchOffset = -1
for ($i = 0; $i -lt $sectionCount; $i++) {
    $section = $sectionTable + $i * 40
    $rva = [BitConverter]::ToUInt32($bytes, $section + 12)
    $rawSize = [BitConverter]::ToUInt32($bytes, $section + 16)
    if ($patchRva -ge $rva -and $patchRva + $expected.Length -le $rva + $rawSize) {
        $patchOffset = [BitConverter]::ToUInt32($bytes, $section + 20) + $patchRva - $rva
        break
    }
}
if ($patchOffset -lt 0) { throw 'AP reset budget instruction is not in a client section' }
for ($i = 0; $i -lt $expected.Length; $i++) {
    if ($bytes[$patchOffset + $i] -ne $expected[$i]) {
        throw ('AP reset budget signature mismatch at byte {0}' -f $i)
    }
}

$cave = [regex]::Match($source, '(?s)__declspec\(naked\) void EvanApResetBudgetCave\(\).*?static void InstallEvanApResetBudget\(\)').Value
if (!$cave) { throw 'Evan AP reset budget cave missing' }
$instructions = @(
    'mov ebx, edx', 'dec ebx', 'dec ebx', 'neg ebx', 'sbb ebx, ebx',
    'and ebx, 0FFFFFFF6h', 'add ebx, ecx', 'pushfd', 'cmp eax, 221',
    'jne done', 'cmp edx, 8', 'ja done', 'lea ebx, [edx + edx * 4]',
    'popfd', 'jmp dword ptr [g_EvanApResetBudgetReturn]'
)
$last = -1
foreach ($instruction in $instructions) {
    $next = $cave.IndexOf($instruction, $last + 1, [StringComparison]::Ordinal)
    if ($next -lt 0) { throw "Cave instruction missing or out of order: $instruction" }
    $last = $next
}
if ($source -notmatch 'g_EvanApResetBudgetReturn\s*=\s*0x008CBED7' -or
    $source -notmatch 'patchAddress\s*=\s*0x008CBECA' -or
    $source -notmatch 'memcmp\(reinterpret_cast<const void\*>\(patchAddress\), expected, sizeof\(expected\)\)' -or
    $source -notmatch 'Memory::CodeCave\(EvanApResetBudgetCave, patchAddress, sizeof\(expected\)\)' -or
    $source -notmatch 'InstallEvanApResetBudget\(\);') {
    throw 'AP reset budget installation or signature guard missing'
}

function Get-NativeBonus([int] $job) {
    if ($job % 10 -eq 1) { return 5 }
    if ($job % 10 -eq 2) { return 10 }
    return 0
}
function Get-PatchedBonus([int] $job) {
    $suffix = $job % 10
    if ([math]::Truncate($job / 10) -eq 221 -and $suffix -le 8) { return $suffix * 5 }
    return Get-NativeBonus $job
}

foreach ($job in 2210..2218) {
    if ((Get-PatchedBonus $job) -ne (($job - 2210) * 5)) {
        throw "Evan AP budget mismatch for job $job"
    }
}
foreach ($job in @(0, 100, 112, 2001, 2112, 2200, 2219, 222, 512)) {
    if ((Get-PatchedBonus $job) -ne (Get-NativeBonus $job)) {
        throw "Non-Evan AP budget changed for job $job"
    }
}
$threshold = 5 * 133 + 20 + (Get-PatchedBonus 2217)
if ($threshold -ne 720 -or 712 -ge $threshold -or 712 + 8 -ne $threshold) {
    throw 'Level 133 Evan with eight HP/MP AP does not meet the client budget rule'
}

Write-Host 'PASS Evan AP reset budget: native signature, guarded cave, job ranges, level 133 case'
