param(
    [string]$Image = "vllm-pair-scheduler-e2e:local",
    [switch]$SkipStress
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$RunRoot = Join-Path $RepoRoot ".tmp\pair-scheduler-e2e"
$Shared = Join-Path $RunRoot "shm"
$Results = Join-Path $RunRoot "results"
New-Item -ItemType Directory -Force $Shared, $Results | Out-Null

docker build -q -f (Join-Path $RepoRoot "scheduler\Dockerfile.e2e") `
    -t $Image (Join-Path $RepoRoot "scheduler") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "pair scheduler E2E image build failed" }

function Invoke-PairCase {
    param(
        [string]$Name,
        [string[]]$PrimaryExtra = @(),
        [string[]]$StandbyExtra = @(),
        [bool]$ExpectFailure = $false
    )
    $Pair = "e2e-$Name-$([Guid]::NewGuid().ToString('N'))"
    $Trace = Join-Path $Results "$Name.jsonl"
    if (Test-Path $Trace) { Remove-Item -LiteralPath $Trace }
    $Suffix = [Guid]::NewGuid().ToString("N").Substring(0, 10)
    $PrimaryName = "pair-primary-$Suffix"
    $StandbyName = "pair-standby-$Suffix"
    $Common = @(
        "--pair", $Pair, "--shm-dir", "/pair-shm", "--trace", "/results/$Name.jsonl",
        "--forward-timeout-ms", "100"
    )
    try {
        docker run -d --name $PrimaryName `
            --mount "type=bind,source=$Shared,target=/pair-shm" `
            --mount "type=bind,source=$Results,target=/results" `
            $Image --role primary --instance A @Common @PrimaryExtra | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "$Name primary container failed to start" }
        Start-Sleep -Milliseconds 80
        docker run -d --name $StandbyName `
            --mount "type=bind,source=$Shared,target=/pair-shm" `
            --mount "type=bind,source=$Results,target=/results" `
            $Image --role standby --instance B @Common @StandbyExtra | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "$Name standby container failed to start" }
        $PrimaryCode = [int](docker wait $PrimaryName)
        $StandbyCode = [int](docker wait $StandbyName)
        if ($ExpectFailure) {
            if ($PrimaryCode -eq 0 -and $StandbyCode -eq 0) {
                throw "$Name unexpectedly succeeded"
            }
            python (Join-Path $RepoRoot "scheduler\tests\verify_trace.py") $Trace --expect failure
            if ($LASTEXITCODE -ne 0) { throw "$Name failure trace verification failed" }
        } else {
            if ($PrimaryCode -ne 0 -or $StandbyCode -ne 0) {
                docker logs $PrimaryName
                docker logs $StandbyName
                throw "$Name failed: primary=$PrimaryCode standby=$StandbyCode"
            }
            python (Join-Path $RepoRoot "scheduler\tests\verify_trace.py") $Trace
            if ($LASTEXITCODE -ne 0) { throw "$Name trace verification failed" }
        }
    } finally {
        docker rm -f $PrimaryName $StandbyName 2>$null | Out-Null
    }
}

Invoke-PairCase -Name "normal" `
    -PrimaryExtra @("--iterations", "40", "--start-delay-ms", "2500", "--linger-ms", "1000") `
    -StandbyExtra @("--iterations", "40", "--start-delay-ms", "2000")
Invoke-PairCase -Name "forward-timeout" -ExpectFailure $true `
    -PrimaryExtra @("--iterations", "1", "--hang-first-ms", "250") `
    -StandbyExtra @("--iterations", "1")
Invoke-PairCase -Name "primary-death" -ExpectFailure $true `
    -PrimaryExtra @("--crash-after-open") `
    -StandbyExtra @("--iterations", "1")

$HungPair = "e2e-hung-$([Guid]::NewGuid().ToString('N'))"
$HungTrace = Join-Path $Results "hung-forward.jsonl"
$HungName = "pair-hung-$([Guid]::NewGuid().ToString('N').Substring(0, 10))"
try {
    docker run -d --name $HungName `
        --mount "type=bind,source=$Shared,target=/pair-shm" `
        --mount "type=bind,source=$Results,target=/results" `
        $Image --role primary --instance A --pair $HungPair `
        --shm-dir /pair-shm --trace /results/hung-forward.jsonl `
        --forward-timeout-ms 100 --hang-forever-first | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "hung-forward container failed to start" }
    Start-Sleep -Milliseconds 350
    docker run --rm --entrypoint vllm-pair-scheduler-inspect `
        --mount "type=bind,source=$Shared,target=/pair-shm" `
        $Image --pair-id $HungPair --shm-dir /pair-shm --json
    if ($LASTEXITCODE -ne 2) {
        throw "hung-forward inspector did not report FAILED"
    }
} finally {
    docker rm -f $HungName 2>$null | Out-Null
}

if (-not $SkipStress) {
    docker run --rm --entrypoint sh `
        --mount "type=bind,source=$(Join-Path $RepoRoot 'scheduler'),target=/work" `
        -w /work python:3.11-slim -lc `
        "apt-get update -qq && apt-get install -y -qq gcc >/dev/null && pip install -q pytest . && PAIR_SCHED_STRESS_ITERS=100000 pytest -q"
    if ($LASTEXITCODE -ne 0) { throw "100000-iteration stress suite failed" }
}

Write-Host "Pair scheduler E2E suite passed. Traces: $Results"
