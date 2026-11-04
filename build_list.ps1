[CmdletBinding()]
param(
	[string]$BuildDirectory = "build/list-release"
)

& (Join-Path $PSScriptRoot "run_list_harness.ps1") -BuildDirectory $BuildDirectory -BuildOnly
exit $LASTEXITCODE
