[CmdletBinding(DefaultParameterSetName = 'Preview')]
param(
    [string]$ConfigPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Preview')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Apply')]
    [string]$InstallManifest,
    [Parameter(Mandatory = $true, ParameterSetName = 'Apply')]
    [switch]$Apply,
    [Parameter(Mandatory = $true, ParameterSetName = 'Rollback')]
    [switch]$Rollback
)

$ErrorActionPreference = 'Stop'
$python = $null
foreach ($candidate in @(
    @{ Command = 'py'; Arguments = @('-3.12', '-X', 'utf8') },
    @{ Command = 'py'; Arguments = @('-3', '-X', 'utf8') },
    @{ Command = 'python'; Arguments = @('-X', 'utf8') }
)) {
    if (-not (Get-Command $candidate.Command -ErrorAction SilentlyContinue)) { continue }
    try {
        $candidateArguments = $candidate.Arguments
        & $candidate.Command @candidateArguments -c 'import sys; assert sys.version_info >= (3, 12)' 2>$null
        if ($LASTEXITCODE -eq 0) {
            $python = $candidate.Command
            $pythonArguments = $candidateArguments
            break
        }
    } catch { }
}
if (-not $python) { throw 'A working Python 3.12 or newer is required; no Python installation was changed.' }
$registrationArguments = @((Join-Path $PSScriptRoot 'mcp_registration.py'))
if ($ConfigPath) { $registrationArguments += @('--config', $ConfigPath) }
if ($InstallManifest) { $registrationArguments += @('--install-manifest', $InstallManifest) }
if ($Apply) { $registrationArguments += '--apply' }
if ($Rollback) { $registrationArguments += '--rollback' }
& $python @pythonArguments @registrationArguments
if ($LASTEXITCODE -ne 0) { throw 'Scoped MCP registration failed.' }
