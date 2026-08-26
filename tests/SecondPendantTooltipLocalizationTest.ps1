param(
    [string]$ClientExe = (Join-Path $PSScriptRoot '..\..\BeiDou-Client\BeiDou.exe'),
    [string]$ReplacementFile = (Join-Path $PSScriptRoot '..\ezorsia\ReplacementFuncs.h'),
    [string]$SecondPendantSource = (Join-Path $PSScriptRoot '..\ezorsia\SecondPendantSlot.cpp')
)

$ErrorActionPreference = 'Stop'
$clientPath = (Resolve-Path -LiteralPath $ClientExe).Path
$replacementPath = (Resolve-Path -LiteralPath $ReplacementFile).Path
$secondPendantPath = (Resolve-Path -LiteralPath $SecondPendantSource).Path
$bytes = [IO.File]::ReadAllBytes($clientPath)
$replacementText = [Text.Encoding]::GetEncoding(936).GetString(
    [IO.File]::ReadAllBytes($replacementPath))
$sourceText = Get-Content -LiteralPath $secondPendantPath -Raw

function Assert-SourceContains([string]$Pattern, [string]$Label) {
    if ($sourceText -notmatch $Pattern) {
        throw ('Final-call localization contract missing: {0}' -f $Label)
    }
}

function Get-CppNamedBytes([string]$Name) {
    $pattern = '(?s)const\s+char\s+' + [regex]::Escape($Name) +
        '\[\]\s*=\s*(?<body>(?:"(?:\\x[0-9A-Fa-f]{2}|[^"])*"\s*)+);'
    $matches = [regex]::Matches($sourceText, $pattern)
    if ($matches.Count -ne 1) {
        throw ('Expected exactly one byte-string constant {0}, found {1}.' -f
            $Name, $matches.Count)
    }

    $decodedBytes = [Collections.Generic.List[byte]]::new()
    foreach ($literal in [regex]::Matches($matches[0].Groups['body'].Value, '"(?<text>[^"]*)"')) {
        $text = $literal.Groups['text'].Value
        for ($index = 0; $index -lt $text.Length;) {
            if ($index + 3 -lt $text.Length -and $text[$index] -eq '\' -and
                $text[$index + 1] -eq 'x') {
                $decodedBytes.Add([Convert]::ToByte($text.Substring($index + 2, 2), 16))
                $index += 4
                continue
            }
            $codePoint = [int]$text[$index]
            if ($codePoint -gt 0x7F) {
                throw ('Non-ASCII source byte in {0}.' -f $Name)
            }
            $decodedBytes.Add([byte]$codePoint)
            $index++
        }
    }
    return $decodedBytes.ToArray()
}

$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
$sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
$optionalHeaderSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
$optionalHeader = $peOffset + 24
$imageBase = [BitConverter]::ToUInt32($bytes, $optionalHeader + 28)
$sectionTable = $optionalHeader + $optionalHeaderSize

$sections = for ($index = 0; $index -lt $sectionCount; $index++) {
    $header = $sectionTable + $index * 40
    [pscustomobject]@{
        VirtualSize = [BitConverter]::ToUInt32($bytes, $header + 8)
        VirtualAddress = [BitConverter]::ToUInt32($bytes, $header + 12)
        RawSize = [BitConverter]::ToUInt32($bytes, $header + 16)
        RawAddress = [BitConverter]::ToUInt32($bytes, $header + 20)
    }
}

function Get-FileOffset([uint32]$VirtualAddress) {
    $rva = $VirtualAddress - $imageBase
    foreach ($section in $sections) {
        $size = [Math]::Max($section.VirtualSize, $section.RawSize)
        if ($rva -ge $section.VirtualAddress -and $rva -lt $section.VirtualAddress + $size) {
            return [int]($section.RawAddress + ($rva - $section.VirtualAddress))
        }
    }
    throw ('Address 0x{0:X8} is outside the PE sections.' -f $VirtualAddress)
}

function Assert-Bytes([uint32]$VirtualAddress, [byte[]]$Expected, [string]$Label) {
    $offset = Get-FileOffset $VirtualAddress
    for ($index = 0; $index -lt $Expected.Length; $index++) {
        if ($bytes[$offset + $index] -ne $Expected[$index]) {
            throw ('{0}: byte mismatch at 0x{1:X8}; expected {2:X2}, actual {3:X2}' -f
                $Label, ($VirtualAddress + $index), $Expected[$index], $bytes[$offset + $index])
        }
    }
}

$expiredTooltipBytes = [byte[]](Get-CppNamedBytes 'kSecondPendantExpiredTooltipText')
$activeTooltipBytes = [byte[]](Get-CppNamedBytes 'kSecondPendantActiveTooltipText')
$lockedTooltipBytes = [byte[]](Get-CppNamedBytes 'kSecondPendantLockedTooltipText')
$purchaseConfirmationBytes = [byte[]](Get-CppNamedBytes 'kSecondPendantPurchaseConfirmationText')
$expiredTooltipHex = [BitConverter]::ToString($expiredTooltipBytes).Replace('-', '')
$activeTooltipHex = [BitConverter]::ToString($activeTooltipBytes).Replace('-', '')
$lockedTooltipHex = [BitConverter]::ToString($lockedTooltipBytes).Replace('-', '')
$purchaseConfirmationHex = [BitConverter]::ToString($purchaseConfirmationBytes).Replace('-', '')
$expectedExpiredHex = 'C0A9B3E4CFEEC1B4C0B8CEBBD2D1B9FDC6DAA3ACB8C3D7B0B1B8B5C4CAF4D0D4B2BBBBE1C9FAD0A7A1A3'
$expectedActiveHex = 'C0A9B3E4CFEEC1B4C0B8CEBBD3D0D0A7C6DAD6C12025303464C4EA25303264D4C225303264C8D520253032643A25303264A3ACB5BDC6DABAF3B8C3D7B0B1B8B5C4CAF4D0D4BDABCAA7D0A7A1A3'
$expectedLockedHex = 'BBF1B5C3CFEEC1B4C0A9B3E4B5C0BEDFBAF3A3ACB8C3D7B0B1B8BDABBBD6B8B4C9FAD0A7A1A3'
$expectedPurchaseConfirmationHex = 'B9BAC2F22573D0E8D2AA2564B5E3C8AFA3ACBFC9CAB9D3C3C0A9B3E4CFEEC1B4C0B8CEBB2564CCECA1A30D0AB9BAC2F2BAF3CEDEB7A8CDCBBFEEA1A3'
if ($expiredTooltipHex -ne $expectedExpiredHex) {
    throw ('Unexpected expired tooltip CP936 bytes: {0}' -f $expiredTooltipHex)
}
if ($activeTooltipHex -ne $expectedActiveHex) {
    throw ('Unexpected active tooltip CP936 bytes: {0}' -f $activeTooltipHex)
}
if ($lockedTooltipHex -ne $expectedLockedHex) {
    throw ('Unexpected locked tooltip CP936 bytes: {0}' -f $lockedTooltipHex)
}
if ($purchaseConfirmationHex -ne $expectedPurchaseConfirmationHex) {
    throw ('Unexpected purchase confirmation CP936 bytes: {0}' -f $purchaseConfirmationHex)
}

Assert-Bytes 0x008F1F65 ([byte[]](0x68, 0x5B, 0x14, 0x00, 0x00)) 'expired source StringPool ID 0x145B'
Assert-Bytes 0x008F1F7F ([byte[]](0xE8, 0x45, 0x62, 0xB2, 0xFF)) 'expired final assignment call'
Assert-Bytes 0x008F1F90 ([byte[]](0x68, 0x5C, 0x14, 0x00, 0x00)) 'active source StringPool ID 0x145C'
Assert-Bytes 0x008F1FC6 ([byte[]](0xE8, 0x80, 0x3B, 0xB5, 0xFF)) 'active final format call'

Assert-SourceContains 'AssignSecondPendantExpiredTooltip' 'expired final-assignment wrapper'
Assert-SourceContains 'FormatSecondPendantActiveTooltip' 'active final-format wrapper'
Assert-SourceContains 'PatchTooltipCall\(kSecondPendantExpiredTooltipAssignCall' 'expired call-site patch'
Assert-SourceContains 'PatchTooltipCall\(kSecondPendantActiveTooltipFormatCall' 'active call-site patch'
Assert-SourceContains 'tooltip localized state=expired finalAssign=008F1F7F' 'expired runtime hit log'
Assert-SourceContains 'tooltip localized state=active finalFormat=008F1FC6' 'active runtime hit log'
Assert-SourceContains '(?s)tooltip patch installed.*verified=%d' 'install-time target verification log'
Assert-SourceContains 'ReadRelativeCallTarget' 'patched target read-back'
Assert-SourceContains 'LocalizeStringPoolTooltip' 'content-based StringPool localizer'
Assert-SourceContains 'strstr\(text,\s*"cslot"\)' 'cslot source-text detection'
Assert-SourceContains 'strstr\(text,\s*"slot extender"\)' 'locked placeholder-template detection'
if ($sourceText -match 'strstr\(text,\s*"Pendant slot extender"\)') {
    throw 'Locked tooltip detection must not depend on the formatted Pendant argument.'
}
Assert-SourceContains 'tooltip StringPool candidate id=%u' 'actual StringPool ID and source-text log'
Assert-SourceContains 'tooltip StringPool replaced id=%u state=active' 'active StringPool replacement log'
Assert-SourceContains 'tooltip StringPool replaced id=%u state=locked' 'locked StringPool replacement log'
Assert-SourceContains 'IsPurchaseConfirmationTemplate\(text\)' 'purchase confirmation template detection'
Assert-SourceContains 'tooltip StringPool replaced id=%u state=purchase' 'purchase confirmation replacement log'
Assert-SourceContains 'InterlockedCompareExchange\(logFlag,\s*1,\s*0\)' 'per-state log throttling'

if ($replacementText -notmatch 'SecondPendantSlot::LocalizeStringPoolTooltip\(nIdx,\s*text\)') {
    throw 'Existing StringPool hook does not call the second-pendant content localizer.'
}

foreach ($obsoleteId in @(5211, 5212)) {
    if ($replacementText -match ('\{' + $obsoleteId + ',')) {
        throw ('Obsolete StringPool table entry remains: {0}' -f $obsoleteId)
    }
}

'PASS SecondPendantTooltipLocalizationTest: contentMatch=cslot,pendant-extender states=active,expired,locked logThrottle=per-state'
