[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$BaselineHeader,
	[string]$CandidateHeader = "",
	[ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Label = "final",
	[int]$Count = 100000,
	[int]$Iterations = 3,
	[ValidateSet("all", "count", "range", "queue", "control")][string]$Group = "all",
	[ValidateSet("fragmented", "contiguous")][string]$Layout = "fragmented",
	[switch]$ReverseOrder,
	[switch]$NativeHeap
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
$BaselineHeader = [IO.Path]::GetFullPath($BaselineHeader)
if ($CandidateHeader) { $CandidateHeader = [IO.Path]::GetFullPath($CandidateHeader) }
$build_directory = Join-Path $PSScriptRoot "build/list-memory"
$output_directory = Join-Path $PSScriptRoot "docs/benchmarks"
New-Item -ItemType Directory -Path $output_directory -Force | Out-Null
New-Item -ItemType Directory -Path $build_directory -Force | Out-Null
$date = Get-Date -Format "yyyy-MM-dd"
$prefix = Join-Path $output_directory "list_memory_$($date)_$Label"
$log_prefix = Join-Path $build_directory $Label
$process = [Diagnostics.Process]::GetCurrentProcess()
$original_priority = $process.PriorityClass
$original_affinity = $process.ProcessorAffinity
$names = @("_CL_", "_LINK_", "CMAKE_BUILD_PARALLEL_LEVEL")
$original_environment = @{}
foreach ($name in $names) {
	$original_environment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}
$exit_code = 1
$summary = [ordered]@{
	started_at = [DateTimeOffset]::Now.ToString("o")
	status = "running"
	count = $Count
	iterations = $Iterations
	group = $Group
	layout = $Layout
	reverse_order = [bool]$ReverseOrder
	native_heap = [bool]$NativeHeap
	baseline_header = [IO.Path]::GetFullPath($BaselineHeader)
	baseline_sha256 = (Get-FileHash -LiteralPath $BaselineHeader -Algorithm SHA256).Hash
	candidate_header = $CandidateHeader
	candidate_sha256 = $null
	benchmark_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot "benchmarks/list/benchmark_list_memory.cpp") -Algorithm SHA256).Hash
}
if ($CandidateHeader) {
	$summary.candidate_sha256 = (Get-FileHash -LiteralPath $CandidateHeader -Algorithm SHA256).Hash
} else {
	$summary.candidate_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot "include/NovaContainers/list.hpp") -Algorithm SHA256).Hash
}
try {
	$process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal
	$allowed_mask = $original_affinity.ToInt64()
	$process.ProcessorAffinity = [IntPtr]($allowed_mask -band (-$allowed_mask))
	$env:_CL_ = "$($original_environment['_CL_']) /cgthreads1"
	$env:_LINK_ = "$($original_environment['_LINK_']) /CGTHREADS:1"
	$env:CMAKE_BUILD_PARALLEL_LEVEL = "1"
	$summary.priority = $process.PriorityClass.ToString()
	$summary.affinity = $process.ProcessorAffinity.ToInt64()
	$native = if ($NativeHeap) { "ON" } else { "OFF" }
	& cmake -S $PSScriptRoot -B $build_directory -DNOVA_BUILD_TESTS=OFF -DNOVA_BUILD_BENCHMARKS=ON `
		"-DNOVA_LIST_MEMORY_BASELINE_HEADER=$($BaselineHeader.Replace('\', '/'))" `
		"-DNOVA_LIST_MEMORY_CANDIDATE_HEADER=$($CandidateHeader.Replace('\', '/'))" `
		"-DNOVA_LIST_MEMORY_NATIVE_HEAP=$native" *> "$log_prefix.configure.log"
	if ($LASTEXITCODE -ne 0) { throw "Configure failed. See $log_prefix.configure.log" }
	& cmake --build $build_directory --config Release --parallel 1 `
		--target nova_containers_list_memory_benchmark -- /maxcpucount:1 /p:CL_MPCount=1 /verbosity:minimal /nologo *> "$log_prefix.build.log"
	if ($LASTEXITCODE -ne 0) { throw "Build failed. See $log_prefix.build.log" }
	$executable = Join-Path $build_directory "benchmarks/Release/nova_containers_list_memory_benchmark.exe"
	$order = if ($ReverseOrder) { "1" } else { "0" }
	$benchmark = Start-Process -FilePath $executable -ArgumentList @($Count, $Iterations, $order, $Group, $Layout) `
		-WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "$prefix.csv" -RedirectStandardError "$prefix.stderr.txt"
	if ($benchmark.ExitCode -ne 0) { throw "Benchmark failed. See $prefix.stderr.txt" }
	$summary.status = "passed"
	$exit_code = 0
} catch {
	$summary.status = "failed"
	$summary.error = $_.Exception.Message
	Write-Host $summary.error
} finally {
	foreach ($name in $names) {
		[Environment]::SetEnvironmentVariable($name, $original_environment[$name], "Process")
	}
	$process.ProcessorAffinity = $original_affinity
	$process.PriorityClass = $original_priority
	$summary.finished_at = [DateTimeOffset]::Now.ToString("o")
	$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$prefix.json" -Encoding UTF8
	Write-Host "$Label`: $($summary.status); $prefix.csv"
}
exit $exit_code
