[CmdletBinding()]
param([string]$GameDataRoot, [switch]$PrintCommand, [string]$Config = 'local-win-release')
$ErrorActionPreference = 'Stop'
$releaseRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

function Quote-NativeArgument([string]$Value) {
    # Apply the Windows CommandLineToArgvW backslash/quote convention.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

try {
    if (-not $GameDataRoot) {
        $GameDataRoot = Join-Path $releaseRoot 'assets'
        if (-not (Test-Path -LiteralPath (Join-Path $GameDataRoot 'default.xex') -PathType Leaf)) {
            foreach ($name in @('game-path.local.txt', 'game-path.txt')) {
                $pathFile = Join-Path $releaseRoot $name
                if (Test-Path -LiteralPath $pathFile -PathType Leaf) {
                    $GameDataRoot = [IO.File]::ReadAllText($pathFile).Trim().Trim('"')
                    break
                }
            }
        }
    }
    if (-not [IO.Path]::IsPathRooted($GameDataRoot)) {
        $GameDataRoot = Join-Path $releaseRoot $GameDataRoot
    }
    $GameDataRoot = [IO.Path]::GetFullPath($GameDataRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $GameDataRoot 'default.xex') -PathType Leaf)) {
        throw 'Game files not found. Place the extracted game in assets, or set game-path.txt. See README.md.'
    }
    $exe = Join-Path $releaseRoot 'badcompanyrecomp.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        $exe = Join-Path $releaseRoot "out\build\$Config\badcompanyrecomp.exe"
        if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            throw 'badcompanyrecomp.exe not found. Extract a release or run tools/build.ps1 first.'
        }
    }
    $buildInfo = Get-Content -LiteralPath (Join-Path $releaseRoot 'game_version.json') -Raw | ConvertFrom-Json
    $xexStream = [IO.File]::OpenRead((Join-Path $GameDataRoot 'default.xex'))
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $actualXexHash = [BitConverter]::ToString($sha256.ComputeHash($xexStream)).Replace('-', '')
    } finally {
        $xexStream.Dispose()
        $sha256.Dispose()
    }
    if ($actualXexHash -ne $buildInfo.game_xex_sha256) {
        throw 'Unsupported default.xex version. This build requires the game version identified in BUILD_INFO.json.'
    }
    $launchArgs = @(
        "--game_data_root=$GameDataRoot",
        "--user_data_root=$(Join-Path $releaseRoot 'runtime\user')",
        "--cache_root=$(Join-Path $releaseRoot 'runtime\cache')",
        "--metadata_root=$(Join-Path $releaseRoot 'runtime\metadata')",
        "--update_data_root=$(Join-Path $releaseRoot 'runtime\update')",
        '--readback_resolve=full',
        '--badco_shadow_array_fetch_fix=true',
        '--badco_shadow_depth_readback_fast=false',
        '--badco_hdr_readback_fast=false',
        '--badco_performance_diagnostics=false',
        '--badco_diagnostic_hotkeys=false',
        '--badco_startup_self_tests=false',
        '--badco_timing_diagnostics=false',
        '--dev_debug_runtime=false',
        '--badco_fps_60=false',
        '--resolution_scale=1', '--draw_resolution_scale_x=1', '--draw_resolution_scale_y=1'
    )
    $nativeArguments = ($launchArgs | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    if ($PrintCommand) {
        [Console]::OutputEncoding = New-Object Text.UTF8Encoding $false
        [pscustomobject]@{ executable=$exe; working_directory=$releaseRoot; arguments=$launchArgs; native_arguments=$nativeArguments } | ConvertTo-Json -Depth 4
        exit 0
    }
    foreach ($name in @('runtime\user','runtime\cache','runtime\metadata','runtime\update','logs')) {
        [IO.Directory]::CreateDirectory((Join-Path $releaseRoot $name)) | Out-Null
    }
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $exe
    $startInfo.WorkingDirectory = $releaseRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.Arguments = $nativeArguments
    [Diagnostics.Process]::Start($startInfo) | Out-Null
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
