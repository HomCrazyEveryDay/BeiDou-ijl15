param(
    [string]$ClientRoot = (Join-Path $PSScriptRoot '../../BeiDou-Client'),
    [string]$OutputDir = (Join-Path $env:TEMP ('buff-countdown-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path
$ClientRoot = (Resolve-Path $ClientRoot).Path
$exporter = Join-Path $PSScriptRoot '../../tools/BeiDouWzTool/bin/Release/net10.0-windows/BeiDouWzTool.exe'
if (!(Test-Path $exporter)) { throw 'Build tools/BeiDouWzTool (Release) before this test.' }
& $exporter export-canvases --source (Join-Path $ClientRoot 'Data/UI/Basic.img') --path ItemNo --output (Join-Path $OutputDir 'digits') --version GMS
if ($LASTEXITCODE -ne 0) { throw 'Native digit export failed.' }
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '../ezorsia/BuffCountdown.cpp'))
$start = $source.IndexOf('namespace {')
$end = $source.IndexOf('bool WriteCall(', $start)
$reset = $source.IndexOf('void Reset()')
if ($start -lt 0 -or $end -lt 0 -or $reset -lt 0) { throw 'Cannot find production hook blocks.' }
$implementation = $source.Substring($start, $end - $start) + "`n}`nnamespace BuffCountdown {`n" + $source.Substring($reset)
# Redirect only the game's factory pointer slot; PCOM/Canvas remain real.
$implementation = $implementation.Replace('constexpr DWORD kFactory = 0x00BF0CC0;', 'DWORD g_testFactorySlot = 0; const DWORD kFactory = reinterpret_cast<DWORD>(&g_testFactorySlot);')
[IO.File]::WriteAllText((Join-Path $OutputDir 'NativeCountdownUnderTest.h'), $implementation)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools not found.' }
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
& cl.exe /nologo /EHsc /std:c++17 "/I$OutputDir" "$PSScriptRoot/BuffCountdownTest.cpp" "/Fo$OutputDir/" "/Fe$OutputDir/test.exe" /link /DYNAMICBASE:NO /BASE:0x400000
if ($LASTEXITCODE -ne 0) { throw 'BuffCountdownTest build failed.' }
& "$OutputDir/test.exe" $ClientRoot "$OutputDir/digits" "$OutputDir/preview.png"
if ($LASTEXITCODE -ne 0) { throw 'BuffCountdownTest failed.' }
Write-Output "Offline canvas preview: $OutputDir/preview.png"
