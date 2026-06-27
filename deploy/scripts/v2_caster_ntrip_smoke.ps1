param(
    [string]$CasterExe = "",
    [string]$HostAddress = "127.0.0.1",
    [int]$NtripPort = 42185,
    [int]$HealthPort = 19185,
    [int]$WorkerCount = 2,
    [string]$Mount = "QA_MOUNT_A",
    [string]$Payload = "NC085_PAYLOAD_source_to_client_0123456789`r`n",
    [int]$TimeoutSeconds = 10
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $CasterExe) {
    $CasterExe = Join-Path $RootDir "bin\Release\navcaster-caster.exe"
}

function Fail {
    param([string]$Message)
    throw "[NC-085 smoke] $Message"
}

function Write-Ascii {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [string]$Text
    )
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Text)
    $Stream.Write($bytes, 0, $bytes.Length)
    $Stream.Flush()
}

function Read-UntilHeaderEnd {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $bytes = New-Object System.Collections.Generic.List[byte]
    $buffer = New-Object byte[] 1

    while ([DateTime]::UtcNow -lt $deadline) {
        if ($Stream.DataAvailable) {
            $count = $Stream.Read($buffer, 0, 1)
            if ($count -le 0) {
                break
            }
            $bytes.Add($buffer[0])
            $text = [System.Text.Encoding]::ASCII.GetString($bytes.ToArray())
            if ($text.EndsWith("`r`n`r`n") -or $text.EndsWith("`n`n")) {
                return $text
            }
            if ($bytes.Count -gt 4096) {
                Fail "response header exceeded 4096 bytes"
            }
        } else {
            Start-Sleep -Milliseconds 20
        }
    }

    Fail "timed out waiting for response header"
}

function Read-ExactBytes {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [int]$Length,
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $buffer = New-Object byte[] $Length
    $offset = 0
    while ($offset -lt $Length -and [DateTime]::UtcNow -lt $deadline) {
        if ($Stream.DataAvailable) {
            $count = $Stream.Read($buffer, $offset, $Length - $offset)
            if ($count -le 0) {
                break
            }
            $offset += $count
        } else {
            Start-Sleep -Milliseconds 20
        }
    }
    if ($offset -ne $Length) {
        Fail "timed out waiting for payload bytes expected=$Length actual=$offset"
    }
    return $buffer
}

function Wait-Health {
    param(
        [string]$Url,
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri $Url -TimeoutSec 2
            if ($response.StatusCode -eq 200) {
                return
            }
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
    Fail "health endpoint did not become ready: $Url"
}

function Get-Metrics {
    param([string]$Url)
    return (Invoke-WebRequest -UseBasicParsing -Uri $Url -TimeoutSec 3).Content | ConvertFrom-Json
}

if (-not (Test-Path -LiteralPath $CasterExe)) {
    Fail "navcaster-caster executable not found: $CasterExe"
}

$SmokeDir = Join-Path $RootDir "build\nc085-smoke"
New-Item -ItemType Directory -Force -Path $SmokeDir | Out-Null
$stdoutPath = Join-Path $SmokeDir "navcaster-caster.stdout.log"
$stderrPath = Join-Path $SmokeDir "navcaster-caster.stderr.log"
Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

$casterArgs = @(
    "--runtime-id", "nc085-smoke",
    "--listen-host", $HostAddress,
    "--listen-port", "$NtripPort",
    "--health-host", $HostAddress,
    "--health-port", "$HealthPort",
    "--worker-count", "$WorkerCount"
)

$source = $null
$client = $null
$process = $null
try {
    $process = Start-Process -FilePath $CasterExe -ArgumentList $casterArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

    Wait-Health -Url "http://$HostAddress`:$HealthPort/health" -TimeoutMs ($TimeoutSeconds * 1000)

    $source = [System.Net.Sockets.TcpClient]::new()
    $source.ReceiveTimeout = $TimeoutSeconds * 1000
    $source.SendTimeout = $TimeoutSeconds * 1000
    $source.Connect($HostAddress, $NtripPort)
    $sourceStream = $source.GetStream()
    Write-Ascii $sourceStream "POST /$Mount HTTP/1.1`r`nHost: $HostAddress`r`nAuthorization: Basic bmMwODU6c291cmNl`r`n`r`n"
    $sourceResponse = Read-UntilHeaderEnd $sourceStream ($TimeoutSeconds * 1000)

    $client = [System.Net.Sockets.TcpClient]::new()
    $client.ReceiveTimeout = $TimeoutSeconds * 1000
    $client.SendTimeout = $TimeoutSeconds * 1000
    $client.Connect($HostAddress, $NtripPort)
    $clientStream = $client.GetStream()
    Write-Ascii $clientStream "GET /$Mount HTTP/1.1`r`nHost: $HostAddress`r`nUser-Agent: NC085-Smoke`r`nAuthorization: Basic bmMwODU6Y2xpZW50`r`n`r`n"
    $clientResponse = Read-UntilHeaderEnd $clientStream ($TimeoutSeconds * 1000)

    $payloadBytes = [System.Text.Encoding]::ASCII.GetBytes($Payload)
    $sourceStream.Write($payloadBytes, 0, $payloadBytes.Length)
    $sourceStream.Flush()

    $receivedBytes = Read-ExactBytes $clientStream $payloadBytes.Length ($TimeoutSeconds * 1000)
    $received = [System.Text.Encoding]::ASCII.GetString($receivedBytes)
    if ($received -ne $Payload) {
        Fail "payload mismatch sent=[$Payload] received=[$received]"
    }

    Start-Sleep -Milliseconds 200
    $metrics = Get-Metrics -Url "http://$HostAddress`:$HealthPort/metrics"
    $ownerWorker = @($metrics.workers | Where-Object { $_.source_count -eq 1 -and $_.client_count -eq 1 })
    if ($ownerWorker.Count -ne 1) {
        Fail "expected exactly one owner worker with source_count=1 and client_count=1"
    }
    if ([int64]$ownerWorker[0].fanout_write_count -lt 1) {
        Fail "fanout_write_count did not increment"
    }
    if ([int64]$ownerWorker[0].redis_publish_count -lt 1) {
        Fail "redis_publish_count did not increment"
    }
    if ([int64]$ownerWorker[0].output_buffer_limit_count -ne 0) {
        Fail "unexpected output buffer limit count"
    }

    Write-Host "[NC-085 smoke] PASS"
    Write-Host "[NC-085 smoke] ntrip_port=$NtripPort health_port=$HealthPort mount=$Mount worker_id=$($ownerWorker[0].worker_id)"
    Write-Host "[NC-085 smoke] source_response=$($sourceResponse.Trim())"
    Write-Host "[NC-085 smoke] client_response=$($clientResponse.Trim())"
    Write-Host "[NC-085 smoke] payload_sent=$Payload"
    Write-Host "[NC-085 smoke] payload_received=$received"
    Write-Host "[NC-085 smoke] fanout_write_count=$($ownerWorker[0].fanout_write_count) redis_publish_count=$($ownerWorker[0].redis_publish_count)"
    Write-Host "[NC-085 smoke] logs=$SmokeDir"
} finally {
    if ($client) {
        $client.Close()
    }
    if ($source) {
        $source.Close()
    }
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit(3000) | Out-Null
    }
}
