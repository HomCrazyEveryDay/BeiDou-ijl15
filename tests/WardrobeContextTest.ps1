$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $PSScriptRoot '../out/tests/wardrobe-context'
New-Item -ItemType Directory -Force $output | Out-Null
# Keep this standalone harness away from the original client's singleton addresses.
$source=(Resolve-Path (Join-Path $PSScriptRoot '../ezorsia')).Path
$content=[IO.File]::ReadAllText("$source/WardrobePreview.cpp").Replace('0x00BEBFA0','0x30BEBFA0').Replace('0x00BED5A4','0x30BED5A4')
[IO.File]::WriteAllText("$output/WardrobePreviewUnderTest.h",$content,[Text.UTF8Encoding]::new($true))
& cl.exe /nologo /EHa /std:c++17 /utf-8 /O2 "/I$output" "/I$source" "$PSScriptRoot/WardrobeContextTest.cpp" "$PSScriptRoot/../ezorsia/WardrobeInput.cpp" "/Fo$output\" "/Fe$output/test.exe" user32.lib oleaut32.lib ole32.lib /link /BASE:0x20000000 /DYNAMICBASE:NO
if($LASTEXITCODE -ne 0){throw 'Wardrobe context test build failed'}
& "$output/test.exe"
if($LASTEXITCODE -ne 0){throw "Wardrobe context test failed: $LASTEXITCODE"}
