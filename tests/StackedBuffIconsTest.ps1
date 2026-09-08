param([switch]$WithoutContextRecovery)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installationPath) { throw 'Visual Studio C++ build tools were not found.' }
Import-Module (Join-Path $installationPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-buff-icons-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\ezorsia\StackedBuffIcons.cpp'))
function Get-Block([string]$start, [string]$end) {
    $first = $source.IndexOf($start)
    if ($first -lt 0) { throw "Cannot locate production block: $start" }
    $last = $source.IndexOf($end, $first + $start.Length)
    if ($last -le $first) { throw "Cannot locate production block end: $end" }
    return $source.Substring($first, $last - $first)
}
$types = Get-Block 'constexpr DWORD kOpcodeUpdateStackedBuffIcons' 'struct OverlayVertex'
[IO.File]::WriteAllText((Join-Path $outputDir 'BuffIconTypes.h'), $types)
$implementation = Get-Block 'int NativeTypeForIcon(' 'void __fastcall NativeAddIconHook('
$implementation += Get-Block 'void __fastcall NativeDrawHook(' 'void DebugLog(const char* format, ...)'
$implementation += Get-Block 'unsigned short ReadUInt16LE(' 'int EstimateNativeIconX('
$implementation += Get-Block 'std::unordered_map<unsigned long long, int> CountNativeIconsByKey(' 'bool TryCallNativeAddIcon('
$implementation += Get-Block 'int PruneStackedNativeIcons(' 'void DrawNativeTemporaryStatView('
$sync = Get-Block 'int ReadNativeIconCount(' 'namespace StackedBuffIcons'
$implementation += [regex]::Replace($sync, '\}\s*\z', '')
$implementation += "`nnamespace StackedBuffIcons {`n"
$implementation += Get-Block 'bool HandlePacket(' 'void DrawCountdownOverlay('
$first = $source.IndexOf('void OnFieldUpdate()')
if ($first -lt 0) { throw 'Cannot locate field lifecycle callbacks.' }
$implementation += $source.Substring($first)
if ($WithoutContextRecovery) {
    $resolver = Get-Block 'DWORD ResolveTemporaryStatView()' 'DWORD GetNativeListHead('
    $implementation = $implementation.Replace($resolver, "DWORD ResolveTemporaryStatView() { return g_observedTemporaryStatView; }`n")
}
[IO.File]::WriteAllText((Join-Path $outputDir 'BuffIconsUnderTest.h'), $implementation)
$output = Join-Path $outputDir 'StackedBuffIconsTest.exe'
& cl.exe /nologo /std:c++17 /O2 /EHsc "/I$outputDir" (Join-Path $PSScriptRoot 'StackedBuffIconsTest.cpp') "/Fo:$outputDir\" "/Fe:$output"
if ($LASTEXITCODE -ne 0) { throw 'StackedBuffIconsTest compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'StackedBuffIconsTest failed.' }
