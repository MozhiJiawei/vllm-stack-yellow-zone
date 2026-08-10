param(
    [string]$PrivoxyExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$errors = [System.Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object Extension -In @('.ps1', '.psm1') | ForEach-Object {
    $tokens = $null
    $parseErrors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($_.FullName, [ref]$tokens, [ref]$parseErrors)
    foreach ($parseError in $parseErrors) { $errors.Add("$($_.Name): $($parseError.Message)") }
}
if ($errors.Count) { throw ($errors -join [Environment]::NewLine) }

Import-Module (Join-Path $PSScriptRoot 'Bridge.Common.psm1') -Force
$defaults = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'bridge-config.defaults.json') -Raw | ConvertFrom-Json
if ($defaults.ascendIp -ne '9.15.144.34') { throw 'Unexpected default Ascend IP.' }
if (-not (Test-BridgeIPv4 $defaults.ascendIp)) { throw 'Default Ascend IP validation failed.' }

$proxyConfig = New-PrivoxyConfigText -BindAddress '192.0.2.10' -ClientAddress $defaults.ascendIp `
    -Port $defaults.proxy.port -ActionsPath 'C:\bridge.action' -LogPath 'C:\privoxy.log'
if ($proxyConfig -match 'listen-address\s+0\.0\.0\.0') { throw 'Proxy must not bind all interfaces.' }
if ($proxyConfig -notmatch "permit-access $([regex]::Escape($defaults.ascendIp))") { throw 'Proxy client ACL missing.' }
if ($proxyConfig -match '(?m)^deny-access\s') { throw 'Destination denies belong in Windows Firewall, not Privoxy ACLs.' }
$actions = New-PrivoxyActionText
if ($actions -notmatch '\+limit-connect\{443\}') { throw 'CONNECT port restriction missing.' }
if ($actions -notmatch '192\.168\.\*\.\*') { throw 'Private destination block missing.' }
$blockedDestinations = Get-BlockedProxyDestinations -BindAddress '192.0.2.10' `
    -ClientAddress $defaults.ascendIp -AdditionalLocalAddresses @('9.15.155.147')
if ('100.64.0.0/10' -notin $blockedDestinations -or 'fc00::/7' -notin $blockedDestinations -or
    $defaults.ascendIp -notin $blockedDestinations -or '9.15.155.147' -notin $blockedDestinations) {
    throw 'Windows Firewall private destination set is incomplete.'
}

if ($PrivoxyExe) {
    if (-not (Test-Path -LiteralPath $PrivoxyExe -PathType Leaf)) { throw "Privoxy executable not found: $PrivoxyExe" }
    $testRoot = Join-Path $env:TEMP ("vllm-stack-privoxy-config-test-" + [guid]::NewGuid().ToString('N'))
    $process = $null
    [void](New-Item -ItemType Directory -Path $testRoot)
    try {
        do {
            $testPort = Get-Random -Minimum 20000 -Maximum 45000
            $portInUse = Get-NetTCPConnection -State Listen -LocalPort $testPort -ErrorAction SilentlyContinue
        } while ($portInUse)
        $actionsPath = Join-Path $testRoot 'bridge.action'
        $configPath = Join-Path $testRoot 'bridge.conf'
        $logPath = Join-Path $testRoot 'privoxy.log'
        $templateSource = Join-Path (Split-Path -Parent $PrivoxyExe) 'templates'
        if (Test-Path -LiteralPath $templateSource -PathType Container) {
            Copy-Item -LiteralPath $templateSource -Destination $testRoot -Recurse
        }
        $actions | Set-Content -LiteralPath $actionsPath -Encoding ASCII
        $realConfig = New-PrivoxyConfigText -BindAddress '127.0.0.1' -ClientAddress '127.0.0.1' `
            -Port $testPort -ActionsPath $actionsPath -LogPath $logPath
        $realConfig | Set-Content -LiteralPath $configPath -Encoding ASCII
        $process = Start-Process -FilePath $PrivoxyExe -ArgumentList @($configPath) -PassThru
        $ready = $false
        foreach ($attempt in 1..20) {
            Start-Sleep -Milliseconds 250
            if (Get-NetTCPConnection -State Listen -LocalAddress 127.0.0.1 -LocalPort $testPort -ErrorAction SilentlyContinue) {
                $ready = $true
                break
            }
            if ($process.HasExited) { break }
        }
        if (-not $ready) { throw 'Privoxy did not accept the generated configuration.' }
        & curl.exe --fail --silent --show-error --proxy "http://127.0.0.1:$testPort" `
            --output NUL https://github.com/
        if ($LASTEXITCODE -ne 0) { throw 'Privoxy HTTPS smoke test failed.' }
        $blockedStatus = & curl.exe --silent --proxy "http://127.0.0.1:$testPort" `
            --output NUL --write-out '%{http_code}' http://127.0.0.1/
        if ($blockedStatus -ne '403') { throw "Privoxy private-destination test returned HTTP $blockedStatus instead of 403." }
    } catch {
        if (Test-Path -LiteralPath $logPath) {
            Write-Warning ((Get-Content -LiteralPath $logPath -Tail 80) -join [Environment]::NewLine)
        }
        throw
    } finally {
        if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host 'WINDOWS_BRIDGE_STATIC_TESTS_OK'
