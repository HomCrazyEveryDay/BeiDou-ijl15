param([string]$DllPath)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools not found.' }
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$outputDir = Join-Path $env:TEMP ('beidou-launcher-parent-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'LauncherParentPolicyTest.exe'
& cl.exe /nologo /utf-8 /std:c++17 /O2 /EHsc (Join-Path $PSScriptRoot 'LauncherParentPolicyTest.cpp') "/Fo:$outputDir\" "/Fe:$output"
if ($LASTEXITCODE -ne 0) { throw 'Launcher parent policy test compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Launcher parent policy regression.' }
if ($DllPath) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $DllPath).Path)
    $text = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($name in @('ZhuMengLauncher.exe', 'ZhuMengLauncher-Dev.exe')) {
        if (!$text.Contains($name)) { throw "Built DLL is missing launcher name: $name" }
    }
    Write-Output 'PASS built DLL includes both launcher filenames.'
}
