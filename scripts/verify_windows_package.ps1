[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$QaDirectory,
    [Parameter(Mandatory)][string]$ReportDirectory,
    [switch]$CleanMachine
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$qa = (Resolve-Path -LiteralPath $QaDirectory).Path
[IO.Directory]::CreateDirectory($ReportDirectory) | Out-Null
$reports = (Resolve-Path -LiteralPath $ReportDirectory).Path
$work = Join-Path $reports ('runtime-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work) | Out-Null
Copy-Item -Path (Join-Path $package '*') -Destination $work -Recurse
Copy-Item -Path (Join-Path $qa '*') -Destination $work -Recurse -Force
$originalPath = $env:PATH
$originalPlatform = $env:QT_QPA_PLATFORM
$originalPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$originalQtPlugins = $env:QT_PLUGIN_PATH
try {
    $env:PATH = "$work;$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $work 'platforms'
    $env:QT_PLUGIN_PATH = $work
    $log = Join-Path $reports 'qt-tests.txt'
    $test = Start-Process -FilePath (Join-Path $work 'hellojson_tests.exe') -ArgumentList @('-o', ('"' + $log + ',txt"')) -WorkingDirectory $work -WindowStyle Hidden -PassThru
    if (-not $test.WaitForExit(60000)) { $test.Kill(); throw 'Regression test timed out' }
    if ($test.ExitCode -ne 0) { throw "Regression failed; inspect $log" }
    $env:QT_QPA_PLATFORM = 'windows'
    $app = Start-Process -FilePath (Join-Path $work 'hellojson.exe') -ArgumentList '--smoke-test' -WorkingDirectory $work -WindowStyle Hidden -PassThru
    if (-not $app.WaitForExit(15000)) { $app.Kill(); throw 'Application startup/exit timed out' }
    if ($app.ExitCode -ne 0) { throw "Application failed: $($app.ExitCode)" }
    [ordered]@{ passed=$true; checked_at=(Get-Date).ToUniversalTime().ToString('o'); environment= $(if ($CleanMachine) {'clean-machine'} else {'host-with-isolated-PATH'}); test_log=$log; application_exit_code=$app.ExitCode } |
        ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $reports 'result.json')
} catch {
    [ordered]@{passed=$false; error=$_.Exception.Message} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $reports 'result.json')
    throw
} finally {
    $env:PATH = $originalPath
    $env:QT_QPA_PLATFORM = $originalPlatform
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $originalPlugins
    $env:QT_PLUGIN_PATH = $originalQtPlugins
}
