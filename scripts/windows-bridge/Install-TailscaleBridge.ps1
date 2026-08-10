[CmdletBinding()]
param(
    [switch]$UseAuthKey
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$common = Join-Path $PSScriptRoot 'Bridge.Common.psm1'
$defaults = Join-Path $PSScriptRoot 'bridge-config.defaults.json'
Import-Module $common -Force
Assert-BridgeAdministrator
$config = Get-BridgeConfig -DefaultsPath $defaults

$tailscale = Get-TailscaleExe
if (-not $tailscale) {
    $architecture = if ([Environment]::Is64BitOperatingSystem) { 'amd64' } else { 'x86' }
    if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { $architecture = 'x86' }
    $msiUrl = "https://pkgs.tailscale.com/stable/tailscale-setup-latest-$architecture.msi"
    $msiPath = Join-Path $env:TEMP "tailscale-setup-latest-$architecture.msi"
    Write-Host "Downloading the official stable Tailscale MSI: $msiUrl"
    Invoke-WebRequest -Uri $msiUrl -OutFile $msiPath -UseBasicParsing
    try {
        $signature = Get-AuthenticodeSignature -FilePath $msiPath
        if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or
            $signature.SignerCertificate.Subject -notmatch 'Tailscale') {
            throw "Tailscale MSI signature validation failed: $($signature.Status) / $($signature.SignerCertificate.Subject)"
        }
        $arguments = @('/i', $msiPath, '/qn', '/norestart', 'TS_NOLAUNCH=1', 'TS_UNATTENDEDMODE=always', 'TS_ENABLEDNS=never')
        Invoke-BridgeNative -FilePath "$env:SystemRoot\System32\msiexec.exe" -ArgumentList $arguments -SuccessExitCodes @(0, 3010)
    } finally {
        Remove-Item -LiteralPath $msiPath -Force -ErrorAction SilentlyContinue
    }
    $tailscale = Get-TailscaleExe
    if (-not $tailscale) { throw 'The MSI completed, but tailscale.exe was not found.' }
}

$binding = Get-BridgeRouteBinding -AscendIp $config.ascendIp
Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters' `
    -Name IPEnableRouter -Type DWord -Value 1
Set-NetIPInterface -InterfaceIndex $binding.InterfaceIndex -AddressFamily IPv4 -Forwarding Enabled

$service = Get-Service -Name Tailscale -ErrorAction Stop
Set-Service -Name $service.Name -StartupType Automatic
if ($service.Status -ne 'Running') { Start-Service -Name $service.Name }

$route = "$($config.ascendIp)/$($config.routePrefixLength)"
$status = $null
try { $status = (& $tailscale status --json 2>$null | ConvertFrom-Json) } catch { }
if ($status -and $status.BackendState -eq 'Running') {
    Set-TailscaleBridgeRoute -TailscaleExe $tailscale -NewRoute $route
} else {
    $routes = Get-TailscaleMergedRouteList -TailscaleExe $tailscale -NewRoute $route
    $upArguments = @('up', '--unattended', '--accept-dns=false', "--advertise-routes=$routes")
    if ($UseAuthKey) {
        $secureKey = Read-Host 'Enter a Tailscale auth key (input is hidden)' -AsSecureString
        $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
        $plainKey = $null
        try {
            $plainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
            if (-not $plainKey.StartsWith('tskey-auth-')) { throw 'Invalid Tailscale auth key format.' }
            $upArguments += "--auth-key=$plainKey"
            & $tailscale @upArguments
            if ($LASTEXITCODE -ne 0) {
                throw "tailscale up failed with exit code $LASTEXITCODE."
            }
        } finally {
            $plainKey = $null
            $upArguments = $null
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
        }
    } else {
        Write-Host 'If this node is not logged in, Tailscale will print a browser login URL.'
        Invoke-BridgeNative -FilePath $tailscale -ArgumentList $upArguments
    }
}

$tailscaleInterface = Get-NetAdapter -ErrorAction SilentlyContinue |
    Where-Object InterfaceDescription -Match 'Tailscale' | Select-Object -First 1
if ($tailscaleInterface) {
    Set-NetIPInterface -InterfaceIndex $tailscaleInterface.ifIndex -AddressFamily IPv4 -Forwarding Enabled
}

Write-Host ''
Write-Host "TAILSCALE_BRIDGE_READY route=$route lan=$($binding.InterfaceAlias)"
Write-Warning 'Approve the new route in the Tailscale admin console unless autoApprovers handles it.'
Write-Warning 'IPEnableRouter is enabled. Reboot B once if forwarding does not work after first install.'
