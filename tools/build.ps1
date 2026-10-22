# Initialize Visual Studio before code generation, configuration and compilation.
[CmdletBinding()]
param(
    [string]$Config = 'local-win-release',
    [ValidateRange(1, 128)][int]$Jobs = 2,
    [string]$VisualStudioPath,
    [string]$LLVMRoot = $env:LLVM_ROOT,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$projectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio discovery failed. Supply -VisualStudioPath with the C++ tools installed.'
    }
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VisualStudioPath) { throw 'Visual Studio C++ x64 tools were not found.' }
}
$devShellDll = Join-Path $VisualStudioPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellDll
Enter-VsDevShell -VsInstallPath $VisualStudioPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
if (-not $LLVMRoot) { $LLVMRoot = Join-Path $env:ProgramFiles 'LLVM' }
$clang = Join-Path $LLVMRoot 'bin\clang-cl.exe'
if (-not (Test-Path -LiteralPath $clang)) { throw 'clang-cl not found. Supply -LLVMRoot for LLVM 18.1.8.' }
$clangVersion = (& $clang --version | Out-String)
if ($clangVersion -notmatch 'clang version 18\.1\.8\b') {
    throw 'This profile is validated with LLVM 18.1.8. Select that toolchain with -LLVMRoot.'
}
$ninjaDir = Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$cmakeDir = Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$env:PATH = "$(Join-Path $LLVMRoot 'bin');$ninjaDir;$env:PATH;$cmakeDir"
Get-Command cmake, ninja -ErrorAction Stop | Out-Null
$env:REXSDK = Join-Path $projectDir 'rexglue\win-amd64'
$codegen = Join-Path $env:REXSDK 'bin\rexglue.exe'
if (-not (Test-Path -LiteralPath $codegen)) { throw 'Install the pinned SDK under rexglue/win-amd64 first.' }
$version = Get-Content -LiteralPath (Join-Path $projectDir 'game_version.json') -Raw | ConvertFrom-Json
foreach ($entry in $version.sdk_binary_hashes.PSObject.Properties) {
    $dll = Join-Path $env:REXSDK "bin\$($entry.Name)"
    $stream = [IO.File]::OpenRead($dll)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $hasher.Dispose() }
    if ($hash -ne $entry.Value) { throw "Pinned SDK mismatch: $($entry.Name)" }
}
$xex = Join-Path $projectDir 'assets\default.xex'
if (-not (Test-Path -LiteralPath $xex)) { throw 'Place the supported extracted game under assets first.' }
$stream = [IO.File]::OpenRead($xex)
$hasher = [Security.Cryptography.SHA256]::Create()
try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
finally { $stream.Dispose(); $hasher.Dispose() }
if ($hash -ne $version.game_xex_sha256) { throw 'Unsupported default.xex hash. See game_version.json.' }
$presets = Get-Content -LiteralPath (Join-Path $projectDir 'CMakePresets.json') -Raw | ConvertFrom-Json
if ($Config -notin $presets.configurePresets.name) { throw "Unknown build preset: $Config" }
Write-Host "Validated LLVM 18.1.8, pinned SDK, game image and preset $Config."
if ($ValidateOnly) { return }
Push-Location $projectDir
try {
    & $codegen codegen badcompanyrecomp_manifest.toml
    if ($LASTEXITCODE -ne 0) { throw "Code generation failed: $LASTEXITCODE" }
    & cmake --preset $Config
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed: $LASTEXITCODE" }
    & cmake --build "out/build/$Config" --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
} finally { Pop-Location }
