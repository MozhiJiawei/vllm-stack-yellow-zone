[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$IpAddress,
    [switch]$ConfigOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Bridge.Common.psm1'
$defaults = Join-Path $PSScriptRoot 'bridge-config.defaults.json'
Import-Module $common -Force
Assert-BridgeAdministrator
if (-not (Test-BridgeIPv4 $IpAddress)) { throw "Invalid IPv4 address: $IpAddress" }

$oldConfig = Get-BridgeConfig -DefaultsPath $defaults
$config = $oldConfig | ConvertTo-Json -Depth 8 | ConvertFrom-Json
$oldIp = [string]$oldConfig.ascendIp
if ($oldIp -eq $IpAddress) {
    Write-Host "The target is already $IpAddress; reconciling installed components."
}
$binding = if ($ConfigOnly) { $null } else { Get-BridgeRouteBinding -AscendIp $IpAddress }
$config.ascendIp = $IpAddress

try {
    Save-BridgeConfig -Config $config
    if (-not $ConfigOnly) {
        $layout = Get-PrivoxyLayout
        if (Test-Path -LiteralPath $layout.Exe -PathType Leaf) {
            $service = Get-Service -Name $config.proxy.serviceName -ErrorAction SilentlyContinue
            [void](Set-PrivoxyBridgeConfiguration -Config $config -Binding $binding -Restart:([bool]$service))
            Write-Host "Updated the proxy client ACL to $IpAddress"
        } else {
            Write-Warning 'Privoxy is not installed; skipped its update.'
        }

        $tailscale = Get-TailscaleExe
        if ($tailscale) {
            $route = "$IpAddress/$($config.routePrefixLength)"
            $oldRoute = "$oldIp/$($oldConfig.routePrefixLength)"
            Set-TailscaleBridgeRoute -TailscaleExe $tailscale -NewRoute $route -OldRoute $oldRoute
            Write-Host "Updated the Tailscale advertised route to $route"
        } else {
            Write-Warning 'Tailscale is not installed; skipped its route update.'
        }
    }
} catch {
    $failure = $_
    Save-BridgeConfig -Config $oldConfig
    if (-not $ConfigOnly) {
        try {
            $tailscale = Get-TailscaleExe
            if ($tailscale) {
                $oldRoute = "$oldIp/$($oldConfig.routePrefixLength)"
                $failedRoute = "$IpAddress/$($config.routePrefixLength)"
                Set-TailscaleBridgeRoute -TailscaleExe $tailscale -NewRoute $oldRoute -OldRoute $failedRoute
            }
            $layout = Get-PrivoxyLayout
            if (Test-Path -LiteralPath $layout.Exe -PathType Leaf) {
                $oldBinding = Get-BridgeRouteBinding -AscendIp $oldIp
                $service = Get-Service -Name $oldConfig.proxy.serviceName -ErrorAction SilentlyContinue
                [void](Set-PrivoxyBridgeConfiguration -Config $oldConfig -Binding $oldBinding -Restart:([bool]$service))
            }
        } catch {
            Write-Warning "Automatic component rollback also failed: $($_.Exception.Message)"
        }
    }
    Write-Warning "Apply failed; attempted to restore runtime state and components to $oldIp."
    throw $failure
}

Write-Host "ASCEND_TARGET_READY old=$oldIp new=$IpAddress configOnly=$ConfigOnly"
if (-not $ConfigOnly) {
    Write-Warning 'The newly advertised route might require approval in the Tailscale admin console.'
}
