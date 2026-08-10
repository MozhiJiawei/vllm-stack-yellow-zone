[CmdletBinding()]
param(
    [switch]$UseAuthKey
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

& (Join-Path $PSScriptRoot 'Install-TailscaleBridge.ps1') `
    -UseAuthKey:$UseAuthKey
& (Join-Path $PSScriptRoot 'Install-HttpProxy.ps1')

Write-Host ''
Write-Host 'WINDOWS_BRIDGE_READY'
