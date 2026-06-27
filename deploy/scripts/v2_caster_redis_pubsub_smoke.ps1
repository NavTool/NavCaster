param(
    [string]$CasterExe = "",
    [string]$HostAddress = "127.0.0.1",
    [string]$BindAddress = "",
    [string]$ConnectAddress = "",
    [int]$RuntimeANtripPort = 42195,
    [int]$RuntimeAHealthPort = 19195,
    [int]$RuntimeBNtripPort = 42196,
    [int]$RuntimeBHealthPort = 19196,
    [string]$RedisHost = "127.0.0.1",
    [int]$RedisPort = 6379,
    [int]$WorkerCount = 2,
    [string]$Mount = "QA_V2_REDIS_MOUNT_A",
    [string]$Payload = "NC103_PAYLOAD_cross_runtime_redis_pubsub_0123456789`r`n",
    [int]$TimeoutSeconds = 12
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $CasterExe) {
    $CasterExe = Join-Path $RootDir "bin\Release\navcaster-caster.exe"
}
if (-not $BindAddress) {
    $BindAddress = $HostAddress
}
if (-not $ConnectAddress) {
    $ConnectAddress = $HostAddress
}

function Fail {
    param([string]$Message)
    throw "[NC-103 pubsub smoke] $Message"
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

$SmokeDir = Join-Path $RootDir "build\nc103-redis-pubsub-smoke"
New-Item -ItemType Directory -Force -Path $SmokeDir | Out-Null
$stdoutAPath = Join-Path $SmokeDir "runtime-a.stdout.log"
$stderrAPath = Join-Path $SmokeDir "runtime-a.stderr.log"
$stdoutBPath = Join-Path $SmokeDir "runtime-b.stdout.log"
$stderrBPath = Join-Path $SmokeDir "runtime-b.stderr.log"
Remove-Item -LiteralPath $stdoutAPath, $stderrAPath, $stdoutBPath, $stderrBPath -Force -ErrorAction SilentlyContinue

$runtimeAArgs = @(
    "--runtime-id", "nc103-runtime-a",
    "--listen-host", $BindAddress,
    "--listen-port", "$RuntimeANtripPort",
    "--health-host", $BindAddress,
    "--health-port", "$RuntimeAHealthPort",
    "--worker-count", "$WorkerCount",
    "--redis-host", $RedisHost,
    "--redis-port", "$RedisPort"
)

$runtimeBArgs = @(
    "--runtime-id", "nc103-runtime-b",
    "--listen-host", $BindAddress,
    "--listen-port", "$RuntimeBNtripPort",
    "--health-host", $BindAddress,
    "--health-port", "$RuntimeBHealthPort",
    "--worker-count", "$WorkerCount",
    "--redis-host", $RedisHost,
    "--redis-port", "$RedisPort"
)

$source = $null
$localClient = $null
$remoteClient = $null
$runtimeA = $null
$runtimeB = $null
try {
    $runtimeA = Start-Process -FilePath $CasterExe -ArgumentList $runtimeAArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutAPath -RedirectStandardError $stderrAPath
    $runtimeB = Start-Process -FilePath $CasterExe -ArgumentList $runtimeBArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutBPath -RedirectStandardError $stderrBPath

    Wait-Health -Url "http://$ConnectAddress`:$RuntimeAHealthPort/health" -TimeoutMs ($TimeoutSeconds * 1000)
    Wait-Health -Url "http://$ConnectAddress`:$RuntimeBHealthPort/health" -TimeoutMs ($TimeoutSeconds * 1000)

    $localClient = [System.Net.Sockets.TcpClient]::new()
    $localClient.ReceiveTimeout = $TimeoutSeconds * 1000
    $localClient.SendTimeout = $TimeoutSeconds * 1000
    $localClient.Connect($ConnectAddress, $RuntimeANtripPort)
    $localClientStream = $localClient.GetStream()
    Write-Ascii $localClientStream "GET /$Mount HTTP/1.1`r`nHost: $ConnectAddress`r`nUser-Agent: NC103-Smoke-Local-Client`r`nAuthorization: Basic bmMxMDM6Y2xpZW50`r`n`r`n"
    $localClientResponse = Read-UntilHeaderEnd $localClientStream ($TimeoutSeconds * 1000) "runtime A local client"

    $remoteClient = [System.Net.Sockets.TcpClient]::new()
    $remoteClient.ReceiveTimeout = $TimeoutSeconds * 1000
    $remoteClient.SendTimeout = $TimeoutSeconds * 1000
    $remoteClient.Connect($ConnectAddress, $RuntimeBNtripPort)
    $remoteClientStream = $remoteClient.GetStream()
    Write-Ascii $remoteClientStream "GET /$Mount HTTP/1.1`r`nHost: $ConnectAddress`r`nUser-Agent: NC103-Smoke-Remote-Client`r`nAuthorization: Basic bmMxMDM6Y2xpZW50`r`n`r`n"
    $remoteClientResponse = Read-UntilHeaderEnd $remoteClientStream ($TimeoutSeconds * 1000) "runtime B remote client"

    Wait-Until -TimeoutMs ($TimeoutSeconds * 1000) -Message "runtime B did not subscribe to mount $Mount" -Condition {
        $metrics = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeBHealthPort/metrics"
        @($metrics.workers | Where-Object { [int64]$_.redis_subscribed_mount_count -ge 1 }).Count -ge 1
    }

    $source = [System.Net.Sockets.TcpClient]::new()
    $source.ReceiveTimeout = $TimeoutSeconds * 1000
    $source.SendTimeout = $TimeoutSeconds * 1000
    $source.Connect($ConnectAddress, $RuntimeANtripPort)
    $sourceStream = $source.GetStream()
    Write-Ascii $sourceStream "POST /$Mount HTTP/1.1`r`nHost: $ConnectAddress`r`nAuthorization: Basic bmMxMDM6c291cmNl`r`n`r`n"
    $sourceResponse = Read-UntilHeaderEnd $sourceStream ($TimeoutSeconds * 1000) "runtime A source"

    $payloadBytes = [System.Text.Encoding]::ASCII.GetBytes($Payload)
    $sourceStream.Write($payloadBytes, 0, $payloadBytes.Length)
    $sourceStream.Flush()

    $localReceivedBytes = Read-ExactBytes $localClientStream $payloadBytes.Length ($TimeoutSeconds * 1000)
    $localReceived = [System.Text.Encoding]::ASCII.GetString($localReceivedBytes)
    if ($localReceived -ne $Payload) {
        Fail "local payload mismatch sent=[$Payload] received=[$localReceived]"
    }

    $remoteReceivedBytes = Read-ExactBytes $remoteClientStream $payloadBytes.Length ($TimeoutSeconds * 1000)
    $remoteReceived = [System.Text.Encoding]::ASCII.GetString($remoteReceivedBytes)
    if ($remoteReceived -ne $Payload) {
        Fail "cross-runtime payload mismatch sent=[$Payload] received=[$remoteReceived]"
    }

    Start-Sleep -Milliseconds 300
    $metricsA = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeAHealthPort/metrics"
    $metricsB = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeBHealthPort/metrics"
    if ([int]$metricsA.worker_count -ne $WorkerCount -or [int]$metricsB.worker_count -ne $WorkerCount) {
        Fail "unexpected worker_count runtimeA=$($metricsA.worker_count) runtimeB=$($metricsB.worker_count) expected=$WorkerCount"
    }
    $mountOwnerA = @($metricsA.mount_owners | Where-Object { $_.mount -eq $Mount })
    $mountOwnerB = @($metricsB.mount_owners | Where-Object { $_.mount -eq $Mount })
    if ($mountOwnerA.Count -ne 1 -or $mountOwnerB.Count -ne 1) {
        Fail "expected one mount owner per runtime for $Mount"
    }
    $publisherWorker = @($metricsA.workers | Where-Object { [int64]$_.source_count -eq 1 -and [int64]$_.client_count -eq 1 -and [int64]$_.fanout_write_count -ge 1 -and [int64]$_.redis_publish_count -ge 1 })
    $subscriberWorker = @($metricsB.workers | Where-Object { [int64]$_.client_count -eq 1 -and [int64]$_.redis_subscribe_message_count -ge 1 -and [int64]$_.redis_remote_fanout_write_count -ge 1 })
    if ($publisherWorker.Count -ne 1) {
        Fail "expected one runtime A publisher worker with source_count=1, client_count=1, fanout_write_count>=1 and redis_publish_count>=1"
    }
    if ($subscriberWorker.Count -ne 1) {
        Fail "expected one runtime B subscriber worker with client_count=1, redis_subscribe_message_count>=1 and redis_remote_fanout_write_count>=1"
    }
    if ([int]$mountOwnerA[0].worker_id -ne [int]$publisherWorker[0].worker_id) {
        Fail "runtime A mount owner worker_id=$($mountOwnerA[0].worker_id) does not match publisher worker_id=$($publisherWorker[0].worker_id)"
    }
    if ([int]$mountOwnerB[0].worker_id -ne [int]$subscriberWorker[0].worker_id) {
        Fail "runtime B mount owner worker_id=$($mountOwnerB[0].worker_id) does not match subscriber worker_id=$($subscriberWorker[0].worker_id)"
    }
    if ([int64]$publisherWorker[0].redis_publish_error_count -ne 0) {
        Fail "runtime A publisher reported redis_publish_error_count=$($publisherWorker[0].redis_publish_error_count)"
    }
    if ([int64]$subscriberWorker[0].redis_error_count -ne 0) {
        Fail "runtime B subscriber reported redis_error_count=$($subscriberWorker[0].redis_error_count)"
    }

    Write-Host "[NC-103 pubsub smoke] PASS"
    Write-Host "[NC-103 pubsub smoke] redis=$RedisHost`:$RedisPort mount=$Mount worker_count=$WorkerCount"
    Write-Host "[NC-103 pubsub smoke] bind=$BindAddress connect=$ConnectAddress"
    Write-Host "[NC-103 pubsub smoke] runtime_a_ntrip=$RuntimeANtripPort runtime_a_health=$RuntimeAHealthPort publisher_worker=$($publisherWorker[0].worker_id)"
    Write-Host "[NC-103 pubsub smoke] runtime_b_ntrip=$RuntimeBNtripPort runtime_b_health=$RuntimeBHealthPort subscriber_worker=$($subscriberWorker[0].worker_id)"
    Write-Host "[NC-103 pubsub smoke] source_response=$($sourceResponse.Trim())"
    Write-Host "[NC-103 pubsub smoke] local_client_response=$($localClientResponse.Trim())"
    Write-Host "[NC-103 pubsub smoke] remote_client_response=$($remoteClientResponse.Trim())"
    Write-Host "[NC-103 pubsub smoke] payload_sent=$Payload"
    Write-Host "[NC-103 pubsub smoke] local_payload_received=$localReceived"
    Write-Host "[NC-103 pubsub smoke] remote_payload_received=$remoteReceived"
    Write-Host "[NC-103 pubsub smoke] runtime_a_connection_count=$($metricsA.connection_count) fanout_write_count=$($publisherWorker[0].fanout_write_count) redis_publish_count=$($publisherWorker[0].redis_publish_count)"
    Write-Host "[NC-103 pubsub smoke] runtime_b_connection_count=$($metricsB.connection_count) redis_subscribe_message_count=$($subscriberWorker[0].redis_subscribe_message_count) redis_remote_fanout_write_count=$($subscriberWorker[0].redis_remote_fanout_write_count)"
    Write-Host "[NC-103 pubsub smoke] logs=$SmokeDir"
} finally {
    if ($remoteClient) {
        $remoteClient.Close()
    }
    if ($localClient) {
        $localClient.Close()
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
