[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Bridge.Common.psm1'
$defaults = Join-Path $PSScriptRoot 'bridge-config.defaults.json'
Import-Module $common -Force
Assert-BridgeAdministrator
$config = Get-BridgeConfig -DefaultsPath $defaults
$binding = Get-BridgeRouteBinding -AscendIp $config.ascendIp
$layout = Get-PrivoxyLayout

if (-not (Test-Path -LiteralPath $layout.Exe -PathType Leaf)) {
    $download = Join-Path $env:TEMP "privoxy-$($config.proxy.version).zip"
    $extract = Join-Path $env:TEMP "vllm-stack-privoxy-$PID"
    Write-Host "Downloading Privoxy $($config.proxy.version) and verifying its pinned SHA256"
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) { throw 'curl.exe is required for the SourceForge download redirect.' }
    Invoke-BridgeNative -FilePath $curl.Source -ArgumentList @(
        '--location', '--fail', '--silent', '--show-error', '--retry', '3',
        '--output', $download, $config.proxy.downloadUrl
    )
    try {
        $actualHash = (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash
        if ($actualHash -ne $config.proxy.sha256) {
            throw "Privoxy SHA256 mismatch. Expected $($config.proxy.sha256), got $actualHash"
        }
        [void](New-Item -ItemType Directory -Path $extract -Force)
        Expand-Archive -LiteralPath $download -DestinationPath $extract -Force
        $privoxyExe = Get-ChildItem -LiteralPath $extract -Filter privoxy.exe -File -Recurse |
            Select-Object -First 1
        if (-not $privoxyExe) { throw 'The Privoxy archive does not contain privoxy.exe.' }
        $sourceRoot = $privoxyExe.Directory.FullName
        [void](New-Item -ItemType Directory -Path $layout.Root -Force)
        Copy-Item -Path (Join-Path $sourceRoot '*') -Destination $layout.Root -Recurse -Force
    } finally {
        Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $extract) {
            Remove-Item -LiteralPath $extract -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

if (-not (Test-Path -LiteralPath $layout.Exe -PathType Leaf)) {
    throw "Privoxy installation is incomplete: $($layout.Exe)"
}

$service = Get-Service -Name $config.proxy.serviceName -ErrorAction SilentlyContinue
$managedPid = 0
if ($service) {
    $managedService = Get-CimInstance Win32_Service -Filter "Name='$($config.proxy.serviceName)'"
    if ($managedService) { $managedPid = [int]$managedService.ProcessId }
}
$foreignListener = Get-NetTCPConnection -State Listen -LocalAddress $binding.LocalAddress `
    -LocalPort $config.proxy.port -ErrorAction SilentlyContinue |
    Where-Object OwningProcess -NE $managedPid
if ($foreignListener) {
    throw "Port $($binding.LocalAddress):$($config.proxy.port) is already owned by another process."
}
try {
    if ($service -and $service.Status -eq 'Running') {
        Stop-Service -Name $service.Name -Force
    }
    $layout = Set-PrivoxyBridgeConfiguration -Config $config -Binding $binding
    if (-not $service) {
        Invoke-BridgeProcess -FilePath $layout.Exe -ArgumentList @("--install:$($config.proxy.serviceName)", $layout.Config)
    }
    # Privoxy 4.1.0 registers itself with the legacy INTERACTIVE_PROCESS bit.
    # Modern Windows disables interactive services, leaving the process marked
    # Running without loading the configuration or opening its listener.
    Invoke-BridgeNative -FilePath "$env:SystemRoot\System32\sc.exe" -ArgumentList @(
        'config', $config.proxy.serviceName, 'type=', 'own'
    )
    Set-Service -Name $config.proxy.serviceName -StartupType Automatic
    Start-Service -Name $config.proxy.serviceName
} catch {
    if ($service -and (Get-Service -Name $service.Name).Status -ne 'Running') {
        Start-Service -Name $service.Name -ErrorAction SilentlyContinue
    }
    throw
}

# Restart automatically after transient failures. These calls are idempotent.
Invoke-BridgeNative -FilePath "$env:SystemRoot\System32\sc.exe" -ArgumentList @(
    'failure', $config.proxy.serviceName, 'reset=', '86400', 'actions=', 'restart/5000/restart/15000/restart/60000'
)
Invoke-BridgeNative -FilePath "$env:SystemRoot\System32\sc.exe" -ArgumentList @(
    'failureflag', $config.proxy.serviceName, '1'
)

$listener = $null
foreach ($attempt in 1..20) {
    $listener = Get-NetTCPConnection -State Listen -LocalAddress $binding.LocalAddress `
        -LocalPort $config.proxy.port -ErrorAction SilentlyContinue
    if ($listener) { break }
    Start-Sleep -Milliseconds 500
}
if (-not $listener) { throw 'Privoxy started but is not listening on the expected address. Check privoxy.log.' }

Write-Host ''
Write-Host "HTTP_PROXY_READY listen=$($binding.LocalAddress):$($config.proxy.port) client=$($config.ascendIp)"
Write-Host "Ascend-side environment values: $($layout.ClientInfo)"
