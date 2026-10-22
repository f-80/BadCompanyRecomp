# Run an isolated, bounded startup check without automating game input.
[CmdletBinding()]
param(
    [ValidateRange(5, 300)][int]$TimeoutSeconds = 25,
    [string]$Config = 'local-win-release',
    [string]$GameDataRoot
)
$ErrorActionPreference = 'Stop'
$projectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$launcher = Join-Path $projectDir 'Start_BC1.ps1'
$json = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $launcher -Config $Config -GameDataRoot $GameDataRoot -PrintCommand
if ($LASTEXITCODE -ne 0) { throw 'Launcher validation failed.' }
$command = $json | ConvertFrom-Json
$probeRoot = Join-Path $projectDir ('out\probes\' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($probeRoot) | Out-Null
$argsList = foreach ($arg in $command.arguments) {
    if ($arg -match '^--(user_data_root|cache_root|metadata_root|update_data_root)=') {
        '--' + $Matches[1] + '=' + (Join-Path $probeRoot $Matches[1])
    } else { $arg }
}
function Quote-NativeArgument([string]$Value) {
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}
$startInfo = New-Object Diagnostics.ProcessStartInfo
$startInfo.FileName = $command.executable
$startInfo.WorkingDirectory = $projectDir
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.Arguments = ($argsList | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
$process = [Diagnostics.Process]::Start($startInfo)
$exited = $process.WaitForExit($TimeoutSeconds * 1000)
if ($exited) {
    Write-Host "Process exited with code $($process.ExitCode). Inspect logs; an exit alone does not identify a crash."
    exit 1
}
$process.Kill()
$process.WaitForExit()
Write-Host "Bounded check complete: process remained running for $TimeoutSeconds seconds and was stopped by this script."
Write-Host "Isolated writable data: $probeRoot"
Write-Host 'Review logs and visual behavior before claiming a successful render or campaign check.'
