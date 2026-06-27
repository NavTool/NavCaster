param(
    [string]$CasterExe = "",
    [string]$HostAddress = "127.0.0.1",
    [int]$RuntimeANtripPort = 42195,
    [int]$RuntimeAHealthPort = 19195,
    [int]$RuntimeBNtripPort = 42196,
    [int]$RuntimeBHealthPort = 19196,
    [string]$RedisHost = "127.0.0.1",
    [int]$RedisPort = 6379,
    [int]$WorkerCount = 2,
    [string]$Mount = "QA_V2_REDIS_MOUNT_A",
    [string]$Payload = "NC093_PAYLOAD_cross_runtime_redis_pubsub_0123456789`r`n",
    [int]$TimeoutSeconds = 12
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $CasterExe) {
    $CasterExe = Join-Path $RootDir "bin\Release\navcaster-caster.exe"
}

function Fail {
    param([string]$Message)
    throw "[NC-093 smoke] $Message"
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
        [int]$TimeoutMs,
        [string]$Label
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
                Fail "$Label response header exceeded 4096 bytes"
            }
        } else {
            Start-Sleep -Milliseconds 20
        }
    }

    Fail "$Label timed out waiting for response header"
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
        Fail "timed out waiting for cross-runtime payload bytes expected=$Length actual=$offset"
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

function Wait-Until {
    param(
        [scriptblock]$Condition,
        [int]$TimeoutMs,
        [string]$Message
    )
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (& $Condition) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    Fail $Message
}

function Test-TcpPort {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs
    )
    $client = $null
    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $async = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs)) {
            return $false
        }
        $client.EndConnect($async)
        return $true
    } catch {
        return $false
    } finally {
        if ($client) {
            $client.Close()
        }
    }
}

if (-not (Test-Path -LiteralPath $CasterExe)) {
    Fail "navcaster-caster executable not found: $CasterExe"
}

if (-not (Test-TcpPort -HostName $RedisHost -Port $RedisPort -TimeoutMs 1500)) {
    Fail "Redis is not reachable at $RedisHost`:$RedisPort"
}

if ($RuntimeANtripPort -eq $RuntimeBNtripPort -or $RuntimeAHealthPort -eq $RuntimeBHealthPort) {
    Fail "runtime A and B ports must be distinct"
}

$SmokeDir = Join-Path $RootDir "build\nc093-redis-pubsub-smoke"
New-Item -ItemType Directory -Force -Path $SmokeDir | Out-Null
$stdoutAPath = Join-Path $SmokeDir "runtime-a.stdout.log"
$stderrAPath = Join-Path $SmokeDir "runtime-a.stderr.log"
$stdoutBPath = Join-Path $SmokeDir "runtime-b.stdout.log"
$stderrBPath = Join-Path $SmokeDir "runtime-b.stderr.log"
Remove-Item -LiteralPath $stdoutAPath, $stderrAPath, $stdoutBPath, $stderrBPath -Force -ErrorAction SilentlyContinue

$runtimeAArgs = @(
    "--runtime-id", "nc093-runtime-a",
    "--listen-host", $HostAddress,
    "--listen-port", "$RuntimeANtripPort",
    "--health-host", $HostAddress,
    "--health-port", "$RuntimeAHealthPort",
    "--worker-count", "$WorkerCount",
    "--redis-host", $RedisHost,
    "--redis-port", "$RedisPort"
)

$runtimeBArgs = @(
    "--runtime-id", "nc093-runtime-b",
    "--listen-host", $HostAddress,
    "--listen-port", "$RuntimeBNtripPort",
    "--health-host", $HostAddress,
    "--health-port", "$RuntimeBHealthPort",
    "--worker-count", "$WorkerCount",
    "--redis-host", $RedisHost,
    "--redis-port", "$RedisPort"
)

$source = $null
$client = $null
$runtimeA = $null
$runtimeB = $null
try {
    $runtimeA = Start-Process -FilePath $CasterExe -ArgumentList $runtimeAArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutAPath -RedirectStandardError $stderrAPath
    $runtimeB = Start-Process -FilePath $CasterExe -ArgumentList $runtimeBArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutBPath -RedirectStandardError $stderrBPath

    Wait-Health -Url "http://$HostAddress`:$RuntimeAHealthPort/health" -TimeoutMs ($TimeoutSeconds * 1000)
    Wait-Health -Url "http://$HostAddress`:$RuntimeBHealthPort/health" -TimeoutMs ($TimeoutSeconds * 1000)

    $client = [System.Net.Sockets.TcpClient]::new()
    $client.ReceiveTimeout = $TimeoutSeconds * 1000
    $client.SendTimeout = $TimeoutSeconds * 1000
    $client.Connect($HostAddress, $RuntimeBNtripPort)
    $clientStream = $client.GetStream()
    Write-Ascii $clientStream "GET /$Mount HTTP/1.1`r`nHost: $HostAddress`r`nUser-Agent: NC093-Smoke-Client`r`nAuthorization: Basic bmMwOTM6Y2xpZW50`r`n`r`n"
    $clientResponse = Read-UntilHeaderEnd $clientStream ($TimeoutSeconds * 1000) "runtime B client"

    Wait-Until -TimeoutMs ($TimeoutSeconds * 1000) -Message "runtime B did not subscribe to mount $Mount" -Condition {
        $metrics = Get-Metrics -Url "http://$HostAddress`:$RuntimeBHealthPort/metrics"
        @($metrics.workers | Where-Object { [int64]$_.redis_subscribed_mount_count -ge 1 }).Count -ge 1
    }

    $source = [System.Net.Sockets.TcpClient]::new()
    $source.ReceiveTimeout = $TimeoutSeconds * 1000
    $source.SendTimeout = $TimeoutSeconds * 1000
    $source.Connect($HostAddress, $RuntimeANtripPort)
    $sourceStream = $source.GetStream()
    Write-Ascii $sourceStream "POST /$Mount HTTP/1.1`r`nHost: $HostAddress`r`nAuthorization: Basic bmMwOTM6c291cmNl`r`n`r`n"
    $sourceResponse = Read-UntilHeaderEnd $sourceStream ($TimeoutSeconds * 1000) "runtime A source"

    $payloadBytes = [System.Text.Encoding]::ASCII.GetBytes($Payload)
    $sourceStream.Write($payloadBytes, 0, $payloadBytes.Length)
    $sourceStream.Flush()

    $receivedBytes = Read-ExactBytes $clientStream $payloadBytes.Length ($TimeoutSeconds * 1000)
    $received = [System.Text.Encoding]::ASCII.GetString($receivedBytes)
    if ($received -ne $Payload) {
        Fail "cross-runtime payload mismatch sent=[$Payload] received=[$received]"
    }

    Start-Sleep -Milliseconds 300
    $metricsA = Get-Metrics -Url "http://$HostAddress`:$RuntimeAHealthPort/metrics"
    $metricsB = Get-Metrics -Url "http://$HostAddress`:$RuntimeBHealthPort/metrics"
    $publisherWorker = @($metricsA.workers | Where-Object { [int64]$_.source_count -eq 1 -and [int64]$_.redis_publish_count -ge 1 })
    $subscriberWorker = @($metricsB.workers | Where-Object { [int64]$_.client_count -eq 1 -and [int64]$_.redis_subscribe_message_count -ge 1 -and [int64]$_.redis_remote_fanout_write_count -ge 1 })
    if ($publisherWorker.Count -ne 1) {
        Fail "expected one runtime A publisher worker with source_count=1 and redis_publish_count>=1"
    }
    if ($subscriberWorker.Count -ne 1) {
        Fail "expected one runtime B subscriber worker with client_count=1, redis_subscribe_message_count>=1 and redis_remote_fanout_write_count>=1"
    }
    if ([int64]$publisherWorker[0].redis_publish_error_count -ne 0) {
        Fail "runtime A publisher reported redis_publish_error_count=$($publisherWorker[0].redis_publish_error_count)"
    }
    if ([int64]$subscriberWorker[0].redis_error_count -ne 0) {
        Fail "runtime B subscriber reported redis_error_count=$($subscriberWorker[0].redis_error_count)"
    }

    Write-Host "[NC-093 smoke] PASS"
    Write-Host "[NC-093 smoke] redis=$RedisHost`:$RedisPort mount=$Mount"
    Write-Host "[NC-093 smoke] runtime_a_ntrip=$RuntimeANtripPort runtime_a_health=$RuntimeAHealthPort publisher_worker=$($publisherWorker[0].worker_id)"
    Write-Host "[NC-093 smoke] runtime_b_ntrip=$RuntimeBNtripPort runtime_b_health=$RuntimeBHealthPort subscriber_worker=$($subscriberWorker[0].worker_id)"
    Write-Host "[NC-093 smoke] source_response=$($sourceResponse.Trim())"
    Write-Host "[NC-093 smoke] client_response=$($clientResponse.Trim())"
    Write-Host "[NC-093 smoke] payload_sent=$Payload"
    Write-Host "[NC-093 smoke] payload_received=$received"
    Write-Host "[NC-093 smoke] publisher_redis_publish_count=$($publisherWorker[0].redis_publish_count)"
    Write-Host "[NC-093 smoke] subscriber_redis_subscribe_message_count=$($subscriberWorker[0].redis_subscribe_message_count)"
    Write-Host "[NC-093 smoke] subscriber_redis_remote_fanout_write_count=$($subscriberWorker[0].redis_remote_fanout_write_count)"
    Write-Host "[NC-093 smoke] logs=$SmokeDir"
} finally {
    if ($client) {
        $client.Close()
    }
    if ($source) {
        $source.Close()
    }
    foreach ($process in @($runtimeA, $runtimeB)) {
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit(3000) | Out-Null
        }
    }
}

