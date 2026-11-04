[CmdletBinding()]
param(
	[string]$BuildDirectory = "build/list-release",
	[switch]$BuildOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
# PowerShell 7 must also route native failures through the recorded exit code.
$PSNativeCommandUseErrorActionPreference = $false

if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
	$BuildDirectory = Join-Path $PSScriptRoot $BuildDirectory
}
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$log_directory = Join-Path $BuildDirectory "list-validation"
New-Item -ItemType Directory -Path $log_directory -Force | Out-Null
$summary_path = Join-Path $log_directory "summary.json"
$process = [System.Diagnostics.Process]::GetCurrentProcess()
$original_priority = $process.PriorityClass
$original_affinity = $process.ProcessorAffinity
$environment_names = @("_CL_", "_LINK_", "CMAKE_BUILD_PARALLEL_LEVEL", "CTEST_PARALLEL_LEVEL")
$original_environment = @{}
foreach ($name in $environment_names) {
	$original_environment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}
$summary = [ordered]@{
	configuration = "Release"
	build_directory = $BuildDirectory
	started_at = [DateTimeOffset]::Now.ToString("o")
	build = "not_run"
	harness = "not_run"
	status = "running"
	steps = @()
	error = $null
}
$exit_code = 1

function Invoke-ValidationStep {
	param([string]$Name, [string]$Tool, [string[]]$Arguments)
	$log_path = Join-Path $log_directory "$Name.log"
	Write-Host "[$Name] $Tool $($Arguments -join ' ')"
	$timer = [System.Diagnostics.Stopwatch]::StartNew()
	& $Tool @Arguments *> $log_path
	$code = $LASTEXITCODE
	$timer.Stop()
	$summary.steps += [ordered]@{
		name = $Name
		exit_code = $code
		seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
		log = $log_path
	}
	if ($code -ne 0) {
		Get-Content -LiteralPath $log_path -Tail 40 | Write-Host
		throw "$Name failed with exit code $code. See $log_path"
	}
}

try {
	$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summary_path -Encoding UTF8
	# Children inherit priority and affinity before the compiler is started.
	$process.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
	$allowed_mask = $original_affinity.ToInt64()
	$single_cpu = $allowed_mask -band (-$allowed_mask)
	$process.ProcessorAffinity = [IntPtr]$single_cpu
	$env:_CL_ = "$($original_environment['_CL_']) /cgthreads1"
	$env:_LINK_ = "$($original_environment['_LINK_']) /CGTHREADS:1"
	$env:CMAKE_BUILD_PARALLEL_LEVEL = "1"
	$env:CTEST_PARALLEL_LEVEL = "1"
	$summary.priority = $process.PriorityClass.ToString()
	$summary.affinity = $process.ProcessorAffinity.ToInt64()
	$cmake = (Get-Command cmake -CommandType Application -ErrorAction Stop).Source
	$ctest = (Get-Command ctest -CommandType Application -ErrorAction Stop).Source
	Invoke-ValidationStep "configure" $cmake @(
		"-S", $PSScriptRoot, "-B", $BuildDirectory,
		"-DCMAKE_BUILD_TYPE=Release", "-DNOVA_BUILD_TESTS=ON", "-DNOVA_BUILD_BENCHMARKS=OFF"
	)
	$build_arguments = @(
		"--build", $BuildDirectory, "--config", "Release", "--parallel", "1",
		"--target", "nova_containers_list_tests", "nova_containers_list_optimize_tests"
	)
	$cache = Get-Content -LiteralPath (Join-Path $BuildDirectory "CMakeCache.txt")
	if ($cache -match '^CMAKE_GENERATOR:INTERNAL=Visual Studio') {
		$build_arguments += @("--", "/maxcpucount:1", "/p:CL_MPCount=1", "/verbosity:minimal", "/nologo")
	}
	$summary.build = "failed"
	Invoke-ValidationStep "build" $cmake $build_arguments
	$summary.build = "passed"
	if (-not $BuildOnly) {
		$summary.harness = "failed"
		Invoke-ValidationStep "harness" $ctest @(
			"--test-dir", $BuildDirectory, "-C", "Release", "--parallel", "1",
			"--tests-regex", "^nova_containers_list(_optimize)?_tests$",
			"--no-tests=error", "--timeout", "300", "--output-on-failure"
		)
		$summary.harness = "passed"
		Get-Content -LiteralPath (Join-Path $log_directory "harness.log") | Write-Host
	}
	$summary.status = "passed"
	$exit_code = 0
} catch {
	$summary.status = "failed"
	$summary.error = $_.Exception.Message
	Write-Host "FAILED: $($summary.error)"
} finally {
	foreach ($name in $environment_names) {
		[Environment]::SetEnvironmentVariable($name, $original_environment[$name], "Process")
	}
	$process.ProcessorAffinity = $original_affinity
	$process.PriorityClass = $original_priority
	$summary.finished_at = [DateTimeOffset]::Now.ToString("o")
	$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summary_path -Encoding UTF8
	Write-Host "Release build: $($summary.build); harness: $($summary.harness)"
	Write-Host "Result: $summary_path"
}
exit $exit_code
