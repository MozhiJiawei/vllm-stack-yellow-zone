Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:BridgeRoot = Join-Path $env:ProgramData 'VllmStackBridge'
$script:StatePath = Join-Path $script:BridgeRoot 'bridge-config.json'
$script:InboundFirewallRule = 'vLLM Stack - Ascend HTTP Proxy Inbound'
$script:OutboundFirewallRule = 'vLLM Stack - Ascend HTTP Proxy Private Destinations'

function Assert-BridgeAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script from an elevated PowerShell session.'
    }
}

function Test-BridgeIPv4 {
    param([Parameter(Mandatory)][string]$IpAddress)
    $parsed = $null
    return [Net.IPAddress]::TryParse($IpAddress, [ref]$parsed) -and
        $parsed.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork -and
        -not [Net.IPAddress]::IsLoopback($parsed) -and
        $IpAddress -ne '0.0.0.0' -and
        ([byte[]]$parsed.GetAddressBytes())[0] -lt 224
}

function Initialize-BridgeState {
    param([Parameter(Mandatory)][string]$DefaultsPath)
    if (-not (Test-Path -LiteralPath $DefaultsPath -PathType Leaf)) {
        throw "Defaults file not found: $DefaultsPath"
    }
    [void](New-Item -ItemType Directory -Path $script:BridgeRoot -Force)
    if (-not (Test-Path -LiteralPath $script:StatePath -PathType Leaf)) {
        Copy-Item -LiteralPath $DefaultsPath -Destination $script:StatePath
    }
    return $script:StatePath
}

function Get-BridgeConfig {
    param([Parameter(Mandatory)][string]$DefaultsPath)
    $path = Initialize-BridgeState -DefaultsPath $DefaultsPath
    $runtime = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($runtime.schemaVersion -ne 1 -or -not (Test-BridgeIPv4 $runtime.ascendIp)) {
        throw "Runtime configuration is invalid: $path"
    }
    # Only the target IP is mutable. Always take software versions, hashes,
    # service names, and security settings from the tracked repository file.
    $config = Get-Content -LiteralPath $DefaultsPath -Raw | ConvertFrom-Json
    $config.ascendIp = [string]$runtime.ascendIp
    return $config
}

function Save-BridgeConfig {
    param([Parameter(Mandatory)]$Config)
    [void](New-Item -ItemType Directory -Path $script:BridgeRoot -Force)
    $temporary = "$script:StatePath.$PID.tmp"
    try {
        $Config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding UTF8
        Move-Item -LiteralPath $temporary -Destination $script:StatePath -Force
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-BridgeNative {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter()][string[]]$ArgumentList = @(),
        [Parameter()][int[]]$SuccessExitCodes = @(0)
    )
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -notin $SuccessExitCodes) {
        throw "Command failed (exit $LASTEXITCODE): $FilePath $($ArgumentList -join ' ')"
    }
}

function Invoke-BridgeProcess {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter()][string[]]$ArgumentList = @(),
        [Parameter()][int[]]$SuccessExitCodes = @(0)
    )
    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -PassThru -Wait
    if ($process.ExitCode -notin $SuccessExitCodes) {
        throw "Process failed (exit $($process.ExitCode)): $FilePath $($ArgumentList -join ' ')"
    }
}

function Get-TailscaleExe {
    $command = Get-Command tailscale.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $installed = Join-Path $env:ProgramFiles 'Tailscale\tailscale.exe'
    if (Test-Path -LiteralPath $installed -PathType Leaf) { return $installed }
    return $null
}

function Get-TailscaleMergedRouteList {
    param(
        [Parameter(Mandatory)][string]$TailscaleExe,
        [Parameter(Mandatory)][string]$NewRoute,
        [Parameter()][string]$OldRoute
    )
    $rawPrefs = & $TailscaleExe debug prefs 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw 'Cannot read Tailscale preferences safely; existing advertised routes were not changed.'
    }
    $prefs = $rawPrefs | ConvertFrom-Json
    $routes = [System.Collections.Generic.List[string]]::new()
    foreach ($existing in @($prefs.AdvertiseRoutes)) {
        $routeText = [string]$existing
        if ($routeText -and $routeText -ne $OldRoute -and -not $routes.Contains($routeText)) {
            $routes.Add($routeText)
        }
    }
    if (-not $routes.Contains($NewRoute)) { $routes.Add($NewRoute) }
    return $routes -join ','
}

function Set-TailscaleBridgeRoute {
    param(
        [Parameter(Mandatory)][string]$TailscaleExe,
        [Parameter(Mandatory)][string]$NewRoute,
        [Parameter()][string]$OldRoute
    )
    $routes = Get-TailscaleMergedRouteList -TailscaleExe $TailscaleExe -NewRoute $NewRoute -OldRoute $OldRoute
    Invoke-BridgeNative -FilePath $TailscaleExe -ArgumentList @('set', "--advertise-routes=$routes", '--accept-dns=false')
}

function Get-BridgeRouteBinding {
    param([Parameter(Mandatory)][string]$AscendIp)
    if (-not (Test-BridgeIPv4 $AscendIp)) { throw "Invalid IPv4 address: $AscendIp" }
    $route = Find-NetRoute -RemoteIPAddress $AscendIp -ErrorAction Stop |
        Where-Object { $_.IPAddress -and $_.IPAddress -notin @('0.0.0.0', '127.0.0.1') } |
        Select-Object -First 1
    if (-not $route) { throw "Windows found no usable source address/route to $AscendIp." }
    $adapter = Get-NetAdapter -InterfaceIndex $route.InterfaceIndex -ErrorAction Stop
    if ($adapter.InterfaceDescription -match 'Tailscale') {
        throw "The route to $AscendIp uses Tailscale; refusing to create a forwarding loop."
    }
    [pscustomobject]@{
        LocalAddress = [string]$route.IPAddress
        InterfaceIndex = [int]$route.InterfaceIndex
        InterfaceAlias = [string]$adapter.Name
    }
}

function Get-PrivoxyLayout {
    $root = Join-Path $script:BridgeRoot 'privoxy'
    [pscustomobject]@{
        Root = $root
        Exe = Join-Path $root 'privoxy.exe'
        Config = Join-Path $root 'bridge.conf'
        Actions = Join-Path $root 'bridge.action'
        Log = Join-Path $root 'privoxy.log'
        ClientInfo = Join-Path $script:BridgeRoot 'client-proxy.txt'
    }
}

function Get-BlockedProxyDestinations {
    param(
        [Parameter(Mandatory)][string]$BindAddress,
        [Parameter(Mandatory)][string]$ClientAddress,
        [Parameter()][string[]]$AdditionalLocalAddresses = @()
    )
    $destinations = @(
        '0.0.0.0/8', '10.0.0.0/8', '100.64.0.0/10', '127.0.0.0/8',
        '169.254.0.0/16', '172.16.0.0/12', '192.168.0.0/16', '224.0.0.0/4',
        '240.0.0.0/4', '::1', 'fc00::/7', 'fe80::/10',
        $BindAddress, $ClientAddress
    )
    $destinations += $AdditionalLocalAddresses
    $destinations | Sort-Object -Unique
}

function New-PrivoxyConfigText {
    param(
        [Parameter(Mandatory)][string]$BindAddress,
        [Parameter(Mandatory)][string]$ClientAddress,
        [Parameter(Mandatory)][ValidateRange(1, 65535)][int]$Port,
        [Parameter(Mandatory)][string]$ActionsPath,
        [Parameter(Mandatory)][string]$LogPath
    )
    $configDirectory = Split-Path -Parent $ActionsPath
    $logDirectory = Split-Path -Parent $LogPath
    $actionFileName = Split-Path -Leaf $ActionsPath
    $logFileName = Split-Path -Leaf $LogPath
    @"
confdir $configDirectory
logdir $logDirectory
listen-address $BindAddress`:$Port
toggle 1
enable-remote-toggle 0
enable-edit-actions 0
permit-access $ClientAddress
actionsfile $actionFileName
logfile $logFileName
debug 1024
debug 4096
debug 8192
socket-timeout 300
forwarded-connect-retries 1
accept-intercepted-requests 0
"@
}

function New-PrivoxyActionText {
    @'
{ +limit-connect{443} }
/

{ +block{Private or local proxy destinations are forbidden} }
localhost/
127.*.*.*/
0.*.*.*/
10.*.*.*/
169.254.*.*/
172.16.*.*/
172.17.*.*/
172.18.*.*/
172.19.*.*/
172.20.*.*/
172.21.*.*/
172.22.*.*/
172.23.*.*/
172.24.*.*/
172.25.*.*/
172.26.*.*/
172.27.*.*/
172.28.*.*/
172.29.*.*/
172.30.*.*/
172.31.*.*/
192.168.*.*/
100.64.*.*/
100.65.*.*/
100.66.*.*/
100.67.*.*/
100.68.*.*/
100.69.*.*/
100.70.*.*/
100.71.*.*/
100.72.*.*/
100.73.*.*/
100.74.*.*/
100.75.*.*/
100.76.*.*/
100.77.*.*/
100.78.*.*/
100.79.*.*/
100.80.*.*/
100.81.*.*/
100.82.*.*/
100.83.*.*/
100.84.*.*/
100.85.*.*/
100.86.*.*/
100.87.*.*/
100.88.*.*/
100.89.*.*/
100.90.*.*/
100.91.*.*/
100.92.*.*/
100.93.*.*/
100.94.*.*/
100.95.*.*/
100.96.*.*/
100.97.*.*/
100.98.*.*/
100.99.*.*/
100.100.*.*/
100.101.*.*/
100.102.*.*/
100.103.*.*/
100.104.*.*/
100.105.*.*/
100.106.*.*/
100.107.*.*/
100.108.*.*/
100.109.*.*/
100.110.*.*/
100.111.*.*/
100.112.*.*/
100.113.*.*/
100.114.*.*/
100.115.*.*/
100.116.*.*/
100.117.*.*/
100.118.*.*/
100.119.*.*/
100.120.*.*/
100.121.*.*/
100.122.*.*/
100.123.*.*/
100.124.*.*/
100.125.*.*/
100.126.*.*/
100.127.*.*/
'@
}

function Set-PrivoxyBridgeConfiguration {
    param(
        [Parameter(Mandatory)]$Config,
        [Parameter(Mandatory)]$Binding,
        [switch]$Restart
    )
    $layout = Get-PrivoxyLayout
    if (-not (Test-Path -LiteralPath $layout.Exe -PathType Leaf)) {
        throw "Privoxy is not installed: $($layout.Exe)"
    }
    New-PrivoxyConfigText -BindAddress $Binding.LocalAddress -ClientAddress $Config.ascendIp `
        -Port $Config.proxy.port -ActionsPath $layout.Actions -LogPath $layout.Log |
        Set-Content -LiteralPath $layout.Config -Encoding ASCII
    New-PrivoxyActionText | Set-Content -LiteralPath $layout.Actions -Encoding ASCII

    Get-NetFirewallRule -DisplayName $script:InboundFirewallRule -ErrorAction SilentlyContinue |
        Remove-NetFirewallRule -ErrorAction Stop
    Get-NetFirewallRule -DisplayName $script:OutboundFirewallRule -ErrorAction SilentlyContinue |
        Remove-NetFirewallRule -ErrorAction Stop
    New-NetFirewallRule -DisplayName $script:InboundFirewallRule -Direction Inbound -Action Allow `
        -Protocol TCP -LocalAddress $Binding.LocalAddress -LocalPort $Config.proxy.port `
        -RemoteAddress $Config.ascendIp -Program $layout.Exe -Profile Any | Out-Null
    $localAddresses = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notin @('0.0.0.0', '127.0.0.1') } |
        Select-Object -ExpandProperty IPAddress
    New-NetFirewallRule -DisplayName $script:OutboundFirewallRule -Direction Outbound -Action Block `
        -RemoteAddress (Get-BlockedProxyDestinations -BindAddress $Binding.LocalAddress `
            -ClientAddress $Config.ascendIp -AdditionalLocalAddresses $localAddresses) `
        -Program $layout.Exe -Profile Any | Out-Null

    @"
HTTP_PROXY=http://$($Binding.LocalAddress):$($Config.proxy.port)
HTTPS_PROXY=http://$($Binding.LocalAddress):$($Config.proxy.port)
NO_PROXY=localhost,127.0.0.1
"@ | Set-Content -LiteralPath $layout.ClientInfo -Encoding ASCII

    if ($Restart) {
        Restart-Service -Name $Config.proxy.serviceName -Force -ErrorAction Stop
    }
    return $layout
}

Export-ModuleMember -Function Assert-BridgeAdministrator, Test-BridgeIPv4, Initialize-BridgeState, `
    Get-BridgeConfig, Save-BridgeConfig, Invoke-BridgeNative, Invoke-BridgeProcess, Get-TailscaleExe, `
    Get-TailscaleMergedRouteList, Set-TailscaleBridgeRoute, `
    Get-BridgeRouteBinding, Get-PrivoxyLayout, New-PrivoxyConfigText, New-PrivoxyActionText, `
    Get-BlockedProxyDestinations, Set-PrivoxyBridgeConfiguration
