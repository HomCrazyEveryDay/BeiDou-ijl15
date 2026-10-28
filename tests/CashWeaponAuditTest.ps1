param([string]$ClientRoot=(Join-Path $PSScriptRoot '../../BeiDou-Client'),[string]$DataRoot='',[Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
$output=Join-Path $env:TEMP ('beidou-cash-weapons-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $output | Out-Null
& cl.exe /nologo /EHsc /std:c++17 /O2 "/I$PSScriptRoot/../ezorsia" "$PSScriptRoot/CashWeaponAuditTest.cpp" "/Fo$output\" "/Fe$output/audit.exe" /link /BASE:0x20000000 /DYNAMICBASE:NO ole32.lib
if($LASTEXITCODE -ne 0){throw 'Cash weapon audit compilation failed'}
if(!$DataRoot){$DataRoot=Join-Path $ClientRoot 'Data'}
& "$output/audit.exe" (Resolve-Path $ClientRoot).Path (Resolve-Path $DataRoot).Path ([IO.Path]::GetFullPath($Report))
if($LASTEXITCODE -ne 0){throw "Cash weapon audit found unresolved resources; see $Report (exit=$LASTEXITCODE)"}
