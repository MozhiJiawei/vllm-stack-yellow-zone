[CmdletBinding()]
param(
    [string]$RemoteHost = 'root@9.15.144.34',
    [string]$RemoteWorkspace = '/root/l00933108',
    [string]$SecretsPath = (Join-Path $env:USERPROFILE '.secrets\gh-oss-attachments.env')
)

$ErrorActionPreference = 'Stop'
$updaterPath = Join-Path $PSScriptRoot 'Update-CodeFromBundle.sh'

foreach ($requiredPath in @($SecretsPath, $updaterPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file does not exist: $requiredPath"
    }
}

Write-Host 'Preparing protected remote directories...'
& ssh -o BatchMode=yes -o ConnectTimeout=10 $RemoteHost `
    "install -d -m 700 '$RemoteWorkspace/.secrets' '$RemoteWorkspace/bin'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to prepare remote directories (ssh exit $LASTEXITCODE)."
}

Write-Host 'Installing the credential file without printing its contents...'
$secretContent = [IO.File]::ReadAllText($SecretsPath).Replace("`r`n", "`n")
$secretContent | & ssh -o BatchMode=yes -o ConnectTimeout=10 $RemoteHost `
    "umask 077; tr -d '\r' | install -m 600 /dev/stdin '$RemoteWorkspace/.secrets/gh-oss-attachments.env'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install the remote credential file (ssh exit $LASTEXITCODE)."
}

Write-Host 'Installing the remote update controller...'
$updaterContent = [IO.File]::ReadAllText($updaterPath).Replace("`r`n", "`n")
$updaterContent | & ssh -o BatchMode=yes -o ConnectTimeout=10 $RemoteHost `
    "tr -d '\r' | install -m 700 /dev/stdin '$RemoteWorkspace/bin/update-code-from-bundle.sh'"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install the remote update controller (ssh exit $LASTEXITCODE)."
}

& ssh -o BatchMode=yes -o ConnectTimeout=10 $RemoteHost `
    "test -s '$RemoteWorkspace/.secrets/gh-oss-attachments.env' && test -x '$RemoteWorkspace/bin/update-code-from-bundle.sh' && stat -c '%a %U:%G %n' '$RemoteWorkspace/.secrets/gh-oss-attachments.env' '$RemoteWorkspace/bin/update-code-from-bundle.sh'"
if ($LASTEXITCODE -ne 0) {
    throw "Remote sync installation verification failed (ssh exit $LASTEXITCODE)."
}

Write-Host 'ASCEND_REMOTE_SYNC_INSTALLED'
