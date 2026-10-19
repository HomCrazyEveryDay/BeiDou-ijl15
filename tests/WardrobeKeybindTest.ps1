param([string]$ClientExe=(Join-Path $PSScriptRoot '../../BeiDou-Client/BeiDou.exe'))
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $PSScriptRoot '../out/tests/wardrobe-key'
New-Item -ItemType Directory -Force $output | Out-Null
$source=(Resolve-Path (Join-Path $PSScriptRoot '../ezorsia')).Path
foreach($pair in @(@{Source="$source/WardrobeKeybind.h";Target='WardrobeKeybindUnderTest.h'},@{Source="$PSScriptRoot/WardrobeKeybindTest.cpp";Target='Test.cpp'},@{Source="$source/WardrobeWnd.cpp";Target='WardrobeRootKeyUnderTest.h'})){
    $content=[IO.File]::ReadAllText($pair.Source)
    if($pair.Target -eq 'WardrobeRootKeyUnderTest.h'){
        # Compile the actual entry callback. Stub its UI/network dependencies only.
        $start=$content.IndexOf('void __fastcall RootKey(')
        $end=$content.IndexOf('int __fastcall RootMouse(', $start)
        if($start -lt 0 -or $end -le $start){throw 'Wardrobe RootKey boundaries not found'}
        $content=$content.Substring($start,$end-$start)
    }
    $content=[regex]::Replace($content,'(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)','0x30')
    [IO.File]::WriteAllText((Join-Path $output $pair.Target),$content)
}
& cl.exe /nologo /EHsc /std:c++17 /O2 "/I$source" "$output/Test.cpp" "/Fo$output\" "/Fe$output/test.exe" /link user32.lib
if($LASTEXITCODE -ne 0){throw 'Wardrobe key test build failed'}
& "$output/test.exe" (Resolve-Path $ClientExe).Path
if($LASTEXITCODE -ne 0){throw 'Wardrobe key tests failed'}
