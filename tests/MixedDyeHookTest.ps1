param([string]$ClientRoot=(Join-Path $PSScriptRoot '../../BeiDou-Client'),[switch]$Window)
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $env:TEMP ('beidou-mixed-hook-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
$source=Join-Path $PSScriptRoot '../ezorsia'
$production=[IO.File]::ReadAllText((Join-Path $source 'MixedDye.cpp')).Replace('#include "stdafx.h"','')
$production=[regex]::Replace($production,'(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)','0x30')
[IO.File]::WriteAllText((Join-Path $output 'MixedDyeUnderTest.cpp'),$production)
$entry=[IO.File]::ReadAllText((Join-Path $source 'dllmain.cpp'))
$start=$entry.IndexOf('static DWORD g_ScriptedResetItemNativeContinue')
$end=$entry.IndexOf('static void InstallScriptedResetItemRedirect()',$start)
$entry=[regex]::Replace($entry.Substring($start,$end-$start),'(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)','0x30')
[IO.File]::WriteAllText((Join-Path $output 'MixedDyeEntryUnderTest.h'),$entry)
$caves=[IO.File]::ReadAllText((Join-Path $source 'codecaves.h'),[Text.Encoding]::GetEncoding(28591))
$start=$caves.IndexOf('DWORD faceRtn =')
$end=$caves.IndexOf('DWORD', $caves.IndexOf('jmp hairRtn',$start))
$classification=$caves.Substring($start,$end-$start)
# Only this ASCII block is extracted; do not transcode the original legacy file.
[IO.File]::WriteAllText((Join-Path $output 'StyleClassificationUnderTest.h'),$classification)
$types=[IO.File]::ReadAllText((Join-Path $source 'AutoTypes.h'))
$start=$types.IndexOf('static bool IsKnownFaceId(')
$end=$types.IndexOf('typedef void(__thiscall* _AvatarLayerBuild_t)',$start)
[IO.File]::WriteAllText((Join-Path $output 'PreviewClassificationUnderTest.h'),$types.Substring($start,$end-$start))
$extra=@()
if($Window) {
    $windowSource=[IO.File]::ReadAllText((Join-Path $source 'MixedDyeWnd.cpp')).Replace('#include "stdafx.h"','class Client { public: static int m_nGameWidth,m_nGameHeight; }; int Client::m_nGameWidth=800; int Client::m_nGameHeight=600;')
    $windowSource=[regex]::Replace($windowSource,'(?i)0x00(?=[4-9ab][0-9a-f]{5}\b)','0x30')
    [IO.File]::WriteAllText((Join-Path $output 'MixedDyeWindowUnderTest.cpp'),$windowSource,[Text.UTF8Encoding]::new($true))
    $extra=@('/DMIXED_DYE_WINDOW_TEST')
}
& cl.exe /nologo /EHsc /std:c++17 /O2 @extra "/I$source" "/I$output" "$PSScriptRoot/MixedDyeHookTest.cpp" "/Fo$output\" "/Fe$output/test.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO "$PSScriptRoot/../detours/detours.lib" gdi32.lib user32.lib
if($LASTEXITCODE -ne 0){throw 'Hook test build failed'}
& "$output/test.exe" (Resolve-Path $ClientRoot).Path
if($LASTEXITCODE -ne 0){throw 'Hook tests failed'}
