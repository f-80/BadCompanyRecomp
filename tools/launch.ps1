# Launch through the same supported image check and FULL profile as the package.
[CmdletBinding()]
param([string]$Config = 'local-win-release', [string]$GameDataRoot, [switch]$PrintCommand)
$launcher = Join-Path $PSScriptRoot '..\Start_BC1.ps1'
& $launcher -Config $Config -GameDataRoot $GameDataRoot -PrintCommand:$PrintCommand
