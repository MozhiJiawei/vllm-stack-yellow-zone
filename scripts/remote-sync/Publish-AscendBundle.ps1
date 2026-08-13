[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$Branch = 'main',
    [string]$RemoteHost = 'root@9.15.144.34',
    [string]$RemoteUpdater = '/root/l00933108/bin/update-code-from-bundle.sh',
    [string]$SecretsPath = (Join-Path $env:USERPROFILE '.secrets\gh-oss-attachments.env'),
    [string]$UploaderScript = (Join-Path $PSScriptRoot 'Upload-OssBundle.cjs'),
    [string]$UploaderNodeModules = (Join-Path $env:USERPROFILE '.codex\skills\gh-oss-attachments\node_modules'),
    [string]$OssDirectLocalAddress = $env:OSS_DIRECT_LOCAL_ADDRESS,
    [ValidateRange(1, 20)][int]$FetchAttempts = 6
)

$ErrorActionPreference = 'Stop'

foreach ($commandName in @('git', 'node', 'ssh')) {
    if (-not (Get-Command $commandName -ErrorAction SilentlyContinue)) {
        throw "Required command is unavailable: $commandName"
    }
}

foreach ($requiredPath in @($RepositoryRoot, $SecretsPath, $UploaderScript, $UploaderNodeModules)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required path does not exist: $requiredPath"
    }
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [string[]]$Arguments = @()
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE."
    }
}

function Find-DirectLocalAddress {
    if (-not [string]::IsNullOrWhiteSpace($OssDirectLocalAddress)) {
        return $OssDirectLocalAddress
    }

    $defaultRoutes = @(Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' `
        -ErrorAction SilentlyContinue)
    $hasTunDefault = $defaultRoutes | Where-Object {
        $_.InterfaceAlias -match '(?i)(^|[-_ ])tun([0-9]*|[-_ ])'
    }
    if (-not $hasTunDefault) {
        return $null
    }

    $directRoute = $defaultRoutes |
        Where-Object {
            $_.NextHop -ne '0.0.0.0' -and
            $_.InterfaceAlias -notmatch '(?i)(^|[-_ ])tun([0-9]*|[-_ ])'
        } |
        Sort-Object RouteMetric, InterfaceMetric |
        Select-Object -First 1
    if (-not $directRoute) {
        return $null
    }

    Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $directRoute.InterfaceIndex `
        -ErrorAction SilentlyContinue |
        Where-Object {
            $_.IPAddress -notlike '169.254.*' -and $_.IPAddress -ne '127.0.0.1'
        } |
        Select-Object -ExpandProperty IPAddress -First 1
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempDirectory = Join-Path $tempBase ('ascend-git-bundle-' + [guid]::NewGuid().ToString('N'))
$publisherRepository = Join-Path $tempDirectory 'publisher.git'
$bundlePath = Join-Path $tempDirectory 'vllm-stack-yellow-zone.bundle'

New-Item -ItemType Directory -Path $tempDirectory -Force | Out-Null

try {
    $fetchSucceeded = $false
    for ($attempt = 1; $attempt -le $FetchAttempts; $attempt++) {
        Write-Host "Fetching origin/$Branch (attempt $attempt/$FetchAttempts)..."
        & git -C $RepositoryRoot fetch --prune origin $Branch
        if ($LASTEXITCODE -eq 0) {
            $fetchSucceeded = $true
            break
        }
        if ($attempt -lt $FetchAttempts) {
            Start-Sleep -Seconds ([Math]::Min(5 * $attempt, 20))
        }
    }
    if (-not $fetchSucceeded) {
        throw "Unable to fetch origin/$Branch after $FetchAttempts attempts; refusing to publish cached code."
    }

    Write-Host 'Creating a self-contained Git bundle...'
    Invoke-NativeChecked -Command git -Arguments @('init', '--bare', $publisherRepository)
    Invoke-NativeChecked -Command git -Arguments @(
        '-C', $publisherRepository,
        'fetch', '--force', $RepositoryRoot,
        "refs/remotes/origin/${Branch}:refs/heads/${Branch}"
    )
    Invoke-NativeChecked -Command git -Arguments @(
        '-C', $publisherRepository, 'symbolic-ref', 'HEAD', "refs/heads/$Branch"
    )
    Invoke-NativeChecked -Command git -Arguments @(
        '-C', $publisherRepository, 'bundle', 'create', $bundlePath, "refs/heads/$Branch"
    )
    Invoke-NativeChecked -Command git -Arguments @(
        '-C', $publisherRepository, 'bundle', 'verify', $bundlePath
    )

    $bundle = Get-Item -LiteralPath $bundlePath
    $bundleHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $bundlePath).Hash.ToLowerInvariant()
    Write-Host ("Bundle size: {0:N2} MiB" -f ($bundle.Length / 1MB))
    Write-Host "Bundle SHA-256: $bundleHash"

    Write-Host 'Uploading the bundle to private Aliyun OSS...'
    $previousNodePath = $env:NODE_PATH
    $previousDirectAddress = $env:OSS_DIRECT_LOCAL_ADDRESS
    $env:NODE_PATH = $UploaderNodeModules
    $directAddress = Find-DirectLocalAddress
    if ($directAddress) {
        Write-Host "Binding OSS upload to direct local address $directAddress to bypass TUN."
        $env:OSS_DIRECT_LOCAL_ADDRESS = $directAddress
    }
    try {
        $uploadOutput = & node $UploaderScript `
            --config $SecretsPath `
            --file $bundlePath `
            --repo MozhiJiawei/vllm-stack-yellow-zone `
            --expires 604800
    }
    finally {
        $env:NODE_PATH = $previousNodePath
        $env:OSS_DIRECT_LOCAL_ADDRESS = $previousDirectAddress
    }
    if ($LASTEXITCODE -ne 0) {
        throw "OSS upload failed with exit code $LASTEXITCODE."
    }

    $uploadResult = $uploadOutput | ConvertFrom-Json
    $signedUrl = $uploadResult.signedUrl
    if ([string]::IsNullOrWhiteSpace($signedUrl) -or -not $signedUrl.StartsWith('https://')) {
        throw 'OSS uploader did not return a signed HTTPS URL.'
    }

    Write-Host 'Triggering the remote atomic update...'
    ($signedUrl + "`n") | & ssh -o BatchMode=yes -o ConnectTimeout=10 $RemoteHost $RemoteUpdater
    if ($LASTEXITCODE -ne 0) {
        throw "Remote update failed with exit code $LASTEXITCODE."
    }

    Write-Host 'ASCEND_BUNDLE_PUBLISH_COMPLETE'
}
finally {
    $resolvedTemp = [IO.Path]::GetFullPath($tempDirectory)
    if ($resolvedTemp.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemp).StartsWith('ascend-git-bundle-')) {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force -ErrorAction SilentlyContinue
    }
}
