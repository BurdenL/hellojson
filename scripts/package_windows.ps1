[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$QtDirectory,
    [string]$BuildDirectory = 'out/build/release',
    [string]$OutputDirectory = 'out/dist',
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Absolute([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
    return [IO.Path]::GetFullPath((Join-Path $repo $path))
}
$qt = Absolute $QtDirectory
$build = Absolute $BuildDirectory
$output = Absolute $OutputDirectory
& $CMake -S $repo -B $build "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $CMake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$binary = $build
if (Test-Path -LiteralPath (Join-Path $build 'Release/hellojson.exe')) { $binary = Join-Path $build 'Release' }
$stage = Join-Path $output ('release-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$package = Join-Path $stage 'HelloJson-windows-x64'
$qa = Join-Path $stage 'qa'
[IO.Directory]::CreateDirectory($package) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $qa 'platforms')) | Out-Null
Copy-Item -LiteralPath (Join-Path $binary 'hellojson.exe') -Destination $package
& (Join-Path $qt 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime --no-opengl-sw --dir $package (Join-Path $package 'hellojson.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
if (-not $env:VCToolsRedistDir) { throw 'Run from MSVC Developer PowerShell (VCToolsRedistDir is required).' }
$crt = Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory | Where-Object Name -Like 'Microsoft.VC*.CRT' | Select-Object -First 1
if (-not $crt) { throw 'MSVC runtime directory not found' }
Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $package
Set-Content -LiteralPath (Join-Path $package 'qt.conf') -Value "[Paths]`nPrefix=." -Encoding ascii
[IO.Directory]::CreateDirectory((Join-Path $package 'licenses/Qt')) | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE'),(Join-Path $repo 'NOTICE') -Destination (Join-Path $package 'licenses')
$qtLicenses = Join-Path $qt '../../Licenses'
if (-not (Test-Path -LiteralPath $qtLicenses)) { throw 'Qt license directory not found' }
Copy-Item -Path (Join-Path $qtLicenses '*') -Destination (Join-Path $package 'licenses/Qt') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'docs/RELEASE_READINESS.md') -Destination (Join-Path $package 'README.md')
Copy-Item -LiteralPath (Join-Path $binary 'hellojson_tests.exe'),(Join-Path $qt 'bin/Qt6Test.dll') -Destination $qa
Copy-Item -LiteralPath (Join-Path $qt 'plugins/platforms/qoffscreen.dll') -Destination (Join-Path $qa 'platforms')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'verify_windows_package.ps1') -Destination $stage
& (Join-Path $PSScriptRoot 'verify_windows_package.ps1') -PackageDirectory $package -QaDirectory $qa -ReportDirectory (Join-Path $stage 'host-verification')
$zip = Join-Path $output 'HelloJson-windows-x64.zip'
Compress-Archive -Path $package -DestinationPath $zip -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
Set-Content -LiteralPath "$zip.sha256" -Value "$hash  HelloJson-windows-x64.zip" -Encoding ascii
$escapedStage = [Security.SecurityElement]::Escape($stage)
@"
<Configuration>
  <Networking>Disable</Networking>
  <MappedFolders><MappedFolder><HostFolder>$escapedStage</HostFolder><SandboxFolder>C:\Release</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder></MappedFolders>
  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Release\verify_windows_package.ps1 -PackageDirectory C:\Release\HelloJson-windows-x64 -QaDirectory C:\Release\qa -ReportDirectory C:\Release\sandbox-verification -CleanMachine</Command></LogonCommand>
</Configuration>
"@ | Set-Content -LiteralPath (Join-Path $stage 'verify-clean-windows.wsb') -Encoding utf8
[ordered]@{package=$zip; sha256=$hash; host_verification='passed'; clean_machine_verification='pending'; sandbox=(Join-Path $stage 'verify-clean-windows.wsb')} |
    ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $output 'release-manifest.json')
Write-Output "Package: $zip"
Write-Output "Clean-machine verification: open $(Join-Path $stage 'verify-clean-windows.wsb'); inspect sandbox-verification/result.json."
