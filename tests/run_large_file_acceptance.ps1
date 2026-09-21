[CmdletBinding()]
param(
    [string]$BuildDir = 'out/build/release',
    [Parameter(Mandatory = $true)][string]$QtPlugins,
    [string]$OutputDirectory = 'out/acceptance',
    [string]$Python = 'python',
    [switch]$KeepFixtures
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Resolve-TaskPath([string]$Value) {
    if ([IO.Path]::IsPathRooted($Value)) { return [IO.Path]::GetFullPath($Value) }
    return [IO.Path]::GetFullPath((Join-Path $repository $Value))
}
$buildPath = Resolve-TaskPath $BuildDir
$outputPath = Resolve-TaskPath $OutputDirectory
$executable = Join-Path $buildPath 'hellojson_benchmark.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw "Build the benchmark first: $executable" }
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$savedPlatform = $env:QT_QPA_PLATFORM
$savedPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path (Resolve-TaskPath $QtPlugins) 'platforms'
$cases = @(
    @{ name='array-100m'; mode='stress'; shape='array'; mib=100 },
    @{ name='array-1g'; mode='stress'; shape='array'; mib=1024 },
    @{ name='nested-1g'; mode='stress'; shape='nested'; mib=1024 },
    @{ name='string-1g'; mode='stress'; shape='string'; mib=1024 },
    @{ name='invalid-100m'; mode='fault'; shape='invalid'; mib=100 },
    @{ name='truncated-1g'; mode='fault'; shape='truncated'; mib=1024 },
    @{ name='deep-1g'; mode='fault'; shape='deep'; mib=1024 },
    @{ name='operations-100m'; mode='tasks'; shape='array'; mib=100 },
    @{ name='operations-1g'; mode='tasks'; shape='array'; mib=1024 }
)
$results = [Collections.Generic.List[object]]::new()
try {
    foreach ($case in $cases) {
        $fixture = Join-Path $outputPath ($case.name + '.fixture.json')
        $report = Join-Path $outputPath ($case.name + '.report.json')
        if (Test-Path -LiteralPath $fixture) { throw "Refusing to overwrite existing fixture: $fixture" }
        Write-Output ("Running {0}: {1}, {2} MiB" -f $case.name, $case.mode, $case.mib)
        & $Python (Join-Path $PSScriptRoot 'generate_large_json.py') $fixture --mib $case.mib --shape $case.shape
        if ($LASTEXITCODE -ne 0) { throw "Fixture generation failed: $($case.name)" }
        try {
            & $executable $case.mode $fixture $report
            if ($LASTEXITCODE -ne 0) { throw "Acceptance failed: $($case.name); inspect $report" }
            $measurement = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
            if ($measurement.file_bytes -ne $case.mib * 1048576L) { throw 'Unexpected fixture size' }
            if ($case.mode -eq 'tasks') {
                if ($measurement.peak_working_set_mib -ge 512) { throw 'Operation memory budget exceeded' }
            } elseif (-not $measurement.passed) { throw "Acceptance assertion failed: $($case.name)" }
            if ($case.mode -in @('stress', 'tasks')) {
                if ($measurement.max_navigation_ms -ge 500 -or $measurement.max_event_loop_gap_ms -ge 500) {
                    throw "UI responsiveness target exceeded: $($case.name)"
                }
            }
            $results.Add([ordered]@{ name=$case.name; mode=$case.mode; shape=$case.shape; measurement=$measurement })
        } finally {
            # Only the exact fixture just created above; never recursively delete.
            if (-not $KeepFixtures) { Remove-Item -LiteralPath $fixture }
        }
    }
    [ordered]@{
        measured_at_utc = (Get-Date).ToUniversalTime().ToString('o')
        build_directory = $buildPath
        environment = 'Windows; offscreen 1200x780; actual generated files; OS cache may be warm'
        passed = $true
        results = @($results.ToArray())
    } | ConvertTo-Json -Depth 16 | Set-Content -Encoding utf8 (Join-Path $outputPath 'acceptance.json')
    Write-Output ("All {0} acceptance cases passed." -f $results.Count)
} finally {
    $env:QT_QPA_PLATFORM = $savedPlatform
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPlugins
}
