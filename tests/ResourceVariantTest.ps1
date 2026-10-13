param([string]$ClientRoot=(Join-Path $PSScriptRoot '../../BeiDou-Client'))
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $env:TEMP ('beidou-resource-variant-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
foreach($mode in @('system','host')) {
    $options=@()
    if($mode -eq 'host') {$options=@('/DRESOURCE_HOST_ALLOCATOR')}
    & cl.exe /nologo /EHsc /std:c++17 /O2 @options "$PSScriptRoot/ResourceVariantTest.cpp" "/Fo$output/$mode.obj" "/Fe$output/$mode.exe"
    if($LASTEXITCODE -ne 0){throw "Resource variant $mode build failed"}
    & "$output/$mode.exe" (Resolve-Path $ClientRoot).Path
    if($LASTEXITCODE -ne 0){throw "Resource variant $mode tests failed"}
}
