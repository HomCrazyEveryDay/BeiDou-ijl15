param()
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $PSScriptRoot '../out/tests/wardrobe-interaction'
New-Item -ItemType Directory -Force $output | Out-Null
$source=(Resolve-Path (Join-Path $PSScriptRoot '../ezorsia')).Path
$content=[IO.File]::ReadAllText("$source/WardrobeWnd.cpp")
$parts=@()
foreach($bounds in @(@('void ReplySound(','std::wstring Wide('),@('std::wstring Wide(','Factory FactoryFn('),@('const wchar_t* Reason(','// Catalog sources'),@('// Catalog sources','// CashShop item well'),@('int CurrentPreset()','void DialogButton('),@('bool PantsBlocked(','// Empty slots'),@('std::wstring BindingNames(','void DrawBody('),@('void HideWindowLayers()','void CloseWindow()'),@('void Popup(','void ShowItemTooltip('),@('void __fastcall Mouse(','int __fastcall MouseMove('),@('int __fastcall Wheel(','void InsertText('))){
    $start=$content.IndexOf($bounds[0]);$end=$content.IndexOf($bounds[1],$start)
    if($start -lt 0 -or $end -le $start){throw "Missing actual callback: $($bounds[0])"}
    $parts+=$content.Substring($start,$end-$start)
}
[IO.File]::WriteAllText("$output/WardrobeInteractionUnderTest.h",($parts -join "`n"),[Text.UTF8Encoding]::new($true))
$start=$content.IndexOf('std::vector<std::wstring> Wrap(');$end=$content.IndexOf('int TextLines(',$start)
if($start -lt 0 -or $end -le $start){throw 'Missing actual Wrap'}
[IO.File]::WriteAllText("$output/WardrobeWrapUnderTest.h",$content.Substring($start,$end-$start),[Text.UTF8Encoding]::new($true))
& cl.exe /nologo /EHsc /std:c++17 /O2 /utf-8 "/I$output" "$PSScriptRoot/WardrobeInteractionTest.cpp" "/Fo$output\" "/Fe$output/test.exe"
if($LASTEXITCODE -ne 0){throw 'Wardrobe interaction test build failed'}
& "$output/test.exe"
if($LASTEXITCODE -ne 0){throw 'Wardrobe interaction tests failed'}
