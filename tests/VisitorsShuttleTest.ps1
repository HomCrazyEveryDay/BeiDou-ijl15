$ErrorActionPreference = 'Stop'
$vs = 'C:/Program Files/Microsoft Visual Studio/18/Community'
$env:Path = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer;$env:Path"
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -vcvars_ver=14.29' | Out-Null
$repo = Split-Path $PSScriptRoot
$output = [IO.Path]::GetFullPath((Join-Path $repo '../.tmp/codex-visitors/mount-set-dll/tests'))
New-Item -ItemType Directory -Force $output | Out-Null
$client = "$repo/../BeiDou-Client/BeiDou.exe"
if ((Get-FileHash $client).Hash -ne '6C4FC608E5B44708D19F07D5B88729126539E919D0B79035DA76610158F355C2') { throw 'Unsupported EXE SHA-256' }
foreach ($test in @(
    @{Name='shuttle'; Source='VisitorsShuttleTest.cpp'; Defines=@('/DVISITORS_SHUTTLE_TEST')},
    @{Name='dispatch'; Source='VisitorsShuttleTest.cpp'; Defines=@('/DVISITORS_SHUTTLE_DISPATCH_TEST','/DVISITORS_MORPH_TEST')},
    @{Name='morph-regression'; Source='VisitorsMorphTest.cpp'; Defines=@('/DVISITORS_MORPH_TEST')}
)) {
    & cl.exe /nologo /O2 /EHsc /std:c++17 @($test.Defines) "$PSScriptRoot/$($test.Source)" "/Fo$output/$($test.Name).obj" "/Fe$output/$($test.Name).exe" /link /BASE:0x20000000 /DYNAMICBASE:NO
    if ($LASTEXITCODE -ne 0) { throw "$($test.Name) test build failed" }
    & "$output/$($test.Name).exe" $client
    if ($LASTEXITCODE -ne 0) { throw "$($test.Name) failed: $LASTEXITCODE" }
}
