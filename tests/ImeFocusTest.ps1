param([switch]$Baseline, [switch]$ChatBoundsOnly, [string]$BaselineRef)
$ErrorActionPreference = 'Stop'
if ($Baseline -and [string]::IsNullOrWhiteSpace($BaselineRef)) {
    throw 'Baseline requires -BaselineRef <commit> for the defect being reproduced.'
}
if (-not $Baseline -and $BaselineRef) { throw '-BaselineRef requires -Baseline.' }
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo '../.tmp/ime-focus-20261018'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
if ($Baseline) {
    # Capture git bytes directly; FixIme.h contains legacy GBK comments.
    & node -e 'const fs=require("fs"),cp=require("child_process");const ref=cp.execFileSync("git",["-C",process.argv[1],"rev-parse","--verify","--end-of-options",process.argv[3]+"^{commit}"]).toString().trim();fs.writeFileSync(process.argv[2],cp.execFileSync("git",["-C",process.argv[1],"show",ref+":ezorsia/FixIme.h"]));' $repo "$output/ImeUnderTest.h" $BaselineRef
    if ($LASTEXITCODE -ne 0) { throw 'Baseline extraction failed' }
} else {
    Copy-Item -LiteralPath "$repo/ezorsia/FixIme.h" -Destination "$output/ImeUnderTest.h" -Force
}
$variant = if ($Baseline) { 'baseline' } else { 'patched' }
$defines = @(if ($Baseline) { '/DIME_BASELINE' })
foreach ($pair in @(@{Source="$output/ImeUnderTest.h";Target="$output/ImeUnderTest.h"}, @{Source="$PSScriptRoot/ImeFocusTest.cpp";Target="$output/Test.cpp"})) {
    $content = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($pair.Source))
    # Rebase source addresses; do not change fixture validation bounds.
    $content = [regex]::Replace($content, '(?i)0x00(?=[4-9ab][0-9a-f]{5}(?:u)?\b)', '0x30')
    $content = $content.Replace('*value >= 0x30B00000 && *value < 0x00C00000', '*value >= 0x00B00000 && *value < 0x00C00000')
    [IO.File]::WriteAllBytes($pair.Target, [Text.Encoding]::GetEncoding(28591).GetBytes($content))
}
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT @defines "$output/Test.cpp" "/I$output" "/I$repo/ezorsia" "/Fo$output/$variant.obj" "/Fe$output/$variant.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO /MANIFEST:EMBED user32.lib
if ($LASTEXITCODE -ne 0) { throw 'IME test build failed' }
$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path "$output/$variant.exe").Path)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.ArgumentList.Add((Resolve-Path "$repo/../BeiDou-Client/BeiDou.exe").Path)
$start.ArgumentList.Add($variant)
if ($ChatBoundsOnly) { $start.ArgumentList.Add('chat-bounds') }
$test = [Diagnostics.Process]::Start($start)
$stdout = $test.StandardOutput.ReadToEnd()
$stderr = $test.StandardError.ReadToEnd()
$test.WaitForExit()
Write-Output $stdout
if ($stderr) { Write-Output $stderr }
if ($test.ExitCode -ne 0) { throw "IME test failed: $($test.ExitCode)" }
