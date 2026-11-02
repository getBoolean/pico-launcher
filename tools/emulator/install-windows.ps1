[CmdletBinding()]
param(
    [string]$Lock = (Join-Path $PSScriptRoot 'versions.json'),
    [string]$Destination
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
$installerArguments = @((Join-Path $PSScriptRoot 'packages.py'), '--platform', 'windows', '--lock', $Lock)
if ($Destination) { $installerArguments += @('--destination', $Destination) }
& $python @pythonArguments @installerArguments
if ($LASTEXITCODE -ne 0) { throw 'Emulator package installation failed.' }
