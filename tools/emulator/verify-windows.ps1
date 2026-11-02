[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallManifest,
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$Rom,
    [string]$Image,
    [int]$Frames = 120,
    [string]$Press
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$python = Join-Path $repo 'out/emulator/.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Create the isolated emulator Python environment from requirements.lock first.' }
$verifyArguments = @('-X', 'utf8', (Join-Path $repo 'tests/emulator/verify_native.py'), '--install-manifest', $InstallManifest, '--output', $Output, '--frames', $Frames)
if ($Rom) { $verifyArguments += @('--rom', $Rom) }
if ($Image) { $verifyArguments += @('--image', $Image) }
if ($Press) { $verifyArguments += @('--press', $Press) }
& $python @verifyArguments
if ($LASTEXITCODE -ne 0) { throw 'Native MCP verification failed; inspect the retained evidence.' }
