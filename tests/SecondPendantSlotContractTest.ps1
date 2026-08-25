param(
    [string]$ClientExe = (Join-Path $PSScriptRoot '..\..\BeiDou-Client\BeiDou.exe'),
    [string]$SourceFile = (Join-Path $PSScriptRoot '..\ezorsia\SecondPendantSlot.cpp')
)

$ErrorActionPreference = 'Stop'
$clientPath = (Resolve-Path -LiteralPath $ClientExe).Path
$sourcePath = (Resolve-Path -LiteralPath $SourceFile).Path
$bytes = [IO.File]::ReadAllBytes($clientPath)
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

function Read-Point([uint32]$TableAddress, [int]$Slot) {
    $offset = (Get-FileOffset $TableAddress) + ($Slot - 1) * 8
    return [pscustomobject]@{
        X = [BitConverter]::ToInt32($bytes, $offset)
        Y = [BitConverter]::ToInt32($bytes, $offset + 4)
    }
}

function Assert-Point([uint32]$TableAddress, [int]$Slot, [int]$X, [int]$Y, [string]$Label) {
    $point = Read-Point $TableAddress $Slot
    if ($point.X -ne $X -or $point.Y -ne $Y) {
        throw ('{0}: slot {1} expected ({2},{3}), actual ({4},{5})' -f
            $Label, $Slot, $X, $Y, $point.X, $point.Y)
    }
}

function Assert-SourceContains([string]$Pattern, [string]$Label) {
    if ($sourceText -notmatch $Pattern) {
        throw ('Source contract missing: {0}' -f $Label)
    }
}

$constructor = [byte[]](0xB8, 0xAD, 0xF5, 0xAB, 0x00)
$constructorFlagRead = [byte[]](0x8B, 0x86, 0xE8, 0x05, 0x00, 0x00)
$constructorWidthDelta = [byte[]](0x83, 0xE0, 0x21, 0x05, 0x9B, 0x00, 0x00, 0x00)
$extraFlagStore = [byte[]](0x89, 0x86, 0xE8, 0x05, 0x00, 0x00)
$mouseButton = [byte[]](0xB8, 0xAD, 0xF7, 0xAB, 0x00)
$singleClickHitCall = [byte[]](0xE8, 0x36, 0x07, 0x00, 0x00)
$doubleClickHitCall = [byte[]](0xE8, 0x31, 0x02, 0x00, 0x00)
$hitTest = [byte[]](0x33, 0xD2, 0xB9, 0x64, 0x22, 0xBE, 0x00)
$drawInitialLimit = [byte[]](0x83, 0xC1, 0x32)
$slot51DrawBranch = [byte[]](0x83, 0x7D, 0x08, 0x33)
$slot51BlockedFill = [byte[]](0xC7, 0x45, 0xE8, 0x01, 0x00, 0x00, 0x00)
$drawLoopLimit = [byte[]](0x83, 0xC0, 0x32)
$normalCoordinateSelection = [byte[]](0x8D, 0x04, 0xC5, 0xF0, 0x23, 0xBE, 0x00)
$petAnchor = [byte[]](0x05, 0xAC, 0x00, 0x00, 0x00)
$secondPendantExpirationReject = [byte[]](0x0F, 0x8C, 0xA3, 0x0E, 0x00, 0x00)

Assert-Bytes 0x007FDE7C $constructor 'CUIEquip constructor'
Assert-Bytes 0x007FDE8B $constructorFlagRead 'constructor expanded-state read'
Assert-Bytes 0x007FDE9D $constructorWidthDelta 'constructor native-width formula'
Assert-Bytes 0x007FDEFB $extraFlagStore 'native expanded-state assignment'
Assert-Bytes 0x007FE4C6 $mouseButton 'CUIEquip mouse-button handler'
Assert-Bytes 0x007FE4F7 $singleClickHitCall 'single-click hit-test path'
Assert-Bytes 0x007FE9FC $doubleClickHitCall 'double-click hit-test path'
Assert-Bytes 0x007FEC32 $hitTest 'CUIEquip hit-test'
Assert-Bytes 0x007FEE54 $drawInitialLimit 'initial native draw limit'
Assert-Bytes 0x007FEE93 $slot51DrawBranch 'native slot-51 draw branch'
Assert-Bytes 0x007FEEB5 $slot51BlockedFill 'native slot-51 blocked fill'
Assert-Bytes 0x007FEFBC $drawLoopLimit 'loop native draw limit'
Assert-Bytes 0x007FEFFC $normalCoordinateSelection 'normal coordinate table selection'
Assert-Bytes 0x007FE142 $petAnchor 'pet initial anchor'
Assert-Bytes 0x007FEC1A $petAnchor 'pet move anchor'
Assert-Bytes 0x007FFD46 $petAnchor 'pet show anchor'
Assert-Bytes 0x004F1CCA $secondPendantExpirationReject 'native slot-51 expansion-expiration rejection'

$normalTable = 0x00BE23F0
$hitTable = 0x00BE2260
$nativePositions = @{
    1 = @(38, 35); 2 = @(38, 68); 3 = @(71, 101); 4 = @(104, 101)
    5 = @(38, 134); 6 = @(38, 167); 7 = @(71, 200); 8 = @(5, 167)
    9 = @(5, 134); 10 = @(137, 134); 11 = @(104, 134); 12 = @(104, 167)
    13 = @(137, 167); 15 = @(104, 68); 16 = @(137, 68); 17 = @(71, 134)
    18 = @(5, 233); 19 = @(38, 233); 20 = @(71, 233)
    49 = @(5, 68); 50 = @(71, 167)
}

foreach ($entry in $nativePositions.GetEnumerator()) {
    Assert-Point $normalTable $entry.Key $entry.Value[0] $entry.Value[1] 'native normal table'
    Assert-Point $hitTable $entry.Key $entry.Value[0] $entry.Value[1] 'native hit table'
}

# The unpatched slot-51 entry aliases the hat. Runtime code must replace only this point.
Assert-Point $normalTable 51 38 35 'unpatched slot-51 coordinate'

$secondPendant = @(38, 101)
$faceAccessory = $nativePositions[2]
if ($secondPendant[0] -ne $faceAccessory[0] -or
    $secondPendant[1] -ne $faceAccessory[1] + 33) {
    throw 'Second pendant is not exactly one row below the face accessory.'
}

$patchedPositions = @{}
foreach ($entry in $nativePositions.GetEnumerator()) {
    $patchedPositions[$entry.Key] = $entry.Value
}
$patchedPositions[51] = $secondPendant

$occupied = @{}
foreach ($entry in $patchedPositions.GetEnumerator()) {
    $key = '{0},{1}' -f $entry.Value[0], $entry.Value[1]
    if ($occupied.ContainsKey($key)) {
        throw ('Equipment hit rectangles overlap: slots {0} and {1} at {2}' -f
            $occupied[$key], $entry.Key, $key)
    }
    $occupied[$key] = $entry.Key
}

$sourceText = Get-Content -LiteralPath $sourcePath -Raw
Assert-SourceContains 'kSecondPendantPosition\s*\{\s*38\s*,\s*101\s*\}' 'pendant coordinate (38,101)'
Assert-SourceContains 'Memory::FillBytes\(kCuiEquipExtraFlagStoreAddress,\s*0x90,\s*6\)' 'expanded-state assignment suppression'
Assert-SourceContains 'Memory::WriteByte\(kCuiEquipDrawInitialLimitImmediate,\s*kSecondPendantSlot\)' 'initial draw limit extension'
Assert-SourceContains 'Memory::WriteByte\(kCuiEquipSlot51BlockedFillImmediate,\s*0\)' 'slot-51 blocked fill suppression'
Assert-SourceContains 'Memory::WriteByte\(kCuiEquipDrawLoopLimitImmediate,\s*kSecondPendantSlot\)' 'loop draw limit extension'
Assert-SourceContains 'Memory::FillBytes\(kSecondPendantExpirationRejectJump,\s*0x90,\s*6\)' 'permanent slot-51 expiration rejection suppression'

foreach ($forbidden in @('kExpandedCoordinateTable', 'PetPanelInitialAnchorCave', 'PetPanelMoveAnchorCave',
        'PetPanelShowAnchorCave', 'Memory::CodeCave')) {
    if ($sourceText.Contains($forbidden)) {
        throw ('Expanded-layout behavior must not remain in second pendant implementation: {0}' -f $forbidden)
    }
}

'PASS SecondPendantSlotContractTest: instructionSites=17 nativeSlots=21 activeSlots=22 uniqueHitRects=22 pendant2=(38,101) nativeWindow=true petOffset=172 blockedFill=false permanent=true'
