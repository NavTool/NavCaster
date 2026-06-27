param(
    [string]$CasterExe = "",
    [string]$HostAddress = "127.0.0.1",
    [string]$BindAddress = "",
    [string]$ConnectAddress = "",
    [int]$RuntimeCount = 2,
    [int]$RuntimeANtripPort = 42205,
    [int]$RuntimeAHealthPort = 19205,
    [int]$RuntimeBNtripPort = 42206,
    [int]$RuntimeBHealthPort = 19206,
    [string]$RedisHost = "127.0.0.1",
    [int]$RedisPort = 6379,
    [int[]]$WorkerCount = @(2),
    [int]$SourceCount = 2,
    [int]$ClientsPerSource = 3,
    [int]$PayloadSizeBytes = 128,
    [double]$PayloadRateHz = 1.0,
    [int]$DurationSec = 20,
    [int]$SampleIntervalSec = 5,
    [string]$MountPrefix = "QA_NC103_CAP",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$SamplingMethod = "PowerShell Get-Process sampled every SampleIntervalSec; CPU percent is delta CPU seconds divided by elapsed wall time and logical processor count; RSS is WorkingSet64 MB."

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
if (-not $OutDir) {
    $OutDir = Join-Path $RootDir "build\nc103-capacity-baseline"
}

function Fail {
    param([string]$Message)
    throw "[NC-103 capacity] $Message"
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
        [int]$TimeoutMs,
        [string]$Label
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
            Start-Sleep -Milliseconds 10
        }
    }
    if ($offset -ne $Length) {
        Fail "$Label timed out waiting for payload bytes expected=$Length actual=$offset"
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

function New-Payload {
    param(
        [string]$Mount,
        [int]$Sequence,
        [int]$Size
    )
    $prefix = "NC103|$Mount|$Sequence|"
    if ($Size -le $prefix.Length) {
        return $prefix.Substring(0, $Size)
    }
    return $prefix + ([string]::new([char]'X', $Size - $prefix.Length))
}

function New-NtripConnection {
    param(
        [string]$Kind,
        [string]$Mount,
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs,
        [string]$Label
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    $client.ReceiveTimeout = $TimeoutMs
    $client.SendTimeout = $TimeoutMs
    $client.Connect($HostName, $Port)
    $stream = $client.GetStream()
    if ($Kind -eq "source") {
        Write-Ascii $stream "POST /$Mount HTTP/1.1`r`nHost: $HostName`r`nAuthorization: Basic bmMxMDM6c291cmNl`r`n`r`n"
    } else {
        Write-Ascii $stream "GET /$Mount HTTP/1.1`r`nHost: $HostName`r`nUser-Agent: NC103-Capacity`r`nAuthorization: Basic bmMxMDM6Y2xpZW50`r`n`r`n"
    }
    $response = Read-UntilHeaderEnd $stream $TimeoutMs $Label
    return [PSCustomObject]@{
        Client = $client
        Stream = $stream
        Response = $response
        Mount = $Mount
        Label = $Label
    }
}

$script:LastProcessSamples = @{}
function Get-ProcessResourceSample {
    param(
        [string]$RuntimeID,
        [System.Diagnostics.Process]$Process,
        [DateTime]$SampleTime,
        [int]$LogicalProcessors
    )

    try {
        $current = Get-Process -Id $Process.Id -ErrorAction Stop
        $key = "$($Process.Id)"
        $cpuPercent = $null
        if ($script:LastProcessSamples.ContainsKey($key)) {
            $last = $script:LastProcessSamples[$key]
            $elapsed = ($SampleTime - $last.SampleTime).TotalSeconds
            if ($elapsed -gt 0 -and $null -ne $current.CPU) {
                $cpuPercent = [Math]::Round((($current.CPU - $last.CPUSeconds) / $elapsed / [Math]::Max(1, $LogicalProcessors)) * 100.0, 2)
            }
        }
        $script:LastProcessSamples[$key] = [PSCustomObject]@{
            SampleTime = $SampleTime
            CPUSeconds = [double]($current.CPU)
        }
        return [PSCustomObject]@{
            runtime_id = $RuntimeID
            pid = $Process.Id
            cpu_seconds = [Math]::Round([double]($current.CPU), 3)
            cpu_percent_since_last_sample = $cpuPercent
            rss_mb = [Math]::Round($current.WorkingSet64 / 1MB, 2)
            private_memory_mb = [Math]::Round($current.PrivateMemorySize64 / 1MB, 2)
        }
    } catch {
        return [PSCustomObject]@{
            runtime_id = $RuntimeID
            pid = $Process.Id
            error = $_.Exception.Message
        }
    }
}

function Invoke-CapacityScenario {
    param([int]$WorkerCountValue)

    $scenarioId = "workers-$WorkerCountValue-rt-$RuntimeCount-src-$SourceCount-clients-$ClientsPerSource"
    $ScenarioDir = Join-Path $OutDir $scenarioId
    New-Item -ItemType Directory -Force -Path $ScenarioDir | Out-Null

    $stdoutAPath = Join-Path $ScenarioDir "runtime-a.stdout.log"
    $stderrAPath = Join-Path $ScenarioDir "runtime-a.stderr.log"
    $stdoutBPath = Join-Path $ScenarioDir "runtime-b.stdout.log"
    $stderrBPath = Join-Path $ScenarioDir "runtime-b.stderr.log"
    Remove-Item -LiteralPath $stdoutAPath, $stderrAPath, $stdoutBPath, $stderrBPath -Force -ErrorAction SilentlyContinue

    $runtimeA = $null
    $runtimeB = $null
    $sources = @()
    $clients = @()
    $samples = New-Object System.Collections.Generic.List[object]
    $payloadsSent = 0
    $payloadsReceived = 0
    $mismatchCount = 0
    $logicalProcessors = [Environment]::ProcessorCount

    try {
        $runtimeAArgs = @(
            "--runtime-id", "nc103-capacity-a",
            "--listen-host", $BindAddress,
            "--listen-port", "$RuntimeANtripPort",
            "--health-host", $BindAddress,
            "--health-port", "$RuntimeAHealthPort",
            "--worker-count", "$WorkerCountValue",
            "--redis-host", $RedisHost,
            "--redis-port", "$RedisPort"
        )
        $runtimeA = Start-Process -FilePath $CasterExe -ArgumentList $runtimeAArgs -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $stdoutAPath -RedirectStandardError $stderrAPath

        if ($RuntimeCount -eq 2) {
            $runtimeBArgs = @(
                "--runtime-id", "nc103-capacity-b",
                "--listen-host", $BindAddress,
                "--listen-port", "$RuntimeBNtripPort",
                "--health-host", $BindAddress,
                "--health-port", "$RuntimeBHealthPort",
                "--worker-count", "$WorkerCountValue",
                "--redis-host", $RedisHost,
                "--redis-port", "$RedisPort"
            )
            $runtimeB = Start-Process -FilePath $CasterExe -ArgumentList $runtimeBArgs -PassThru -WindowStyle Hidden `
                -RedirectStandardOutput $stdoutBPath -RedirectStandardError $stderrBPath
        }

        Wait-Health -Url "http://$ConnectAddress`:$RuntimeAHealthPort/health" -TimeoutMs 12000
        if ($RuntimeCount -eq 2) {
            Wait-Health -Url "http://$ConnectAddress`:$RuntimeBHealthPort/health" -TimeoutMs 12000
        }

        $localClientsPerSource = $ClientsPerSource
        $remoteClientsPerSource = 0
        if ($RuntimeCount -eq 2) {
            if ($ClientsPerSource -lt 2) {
                Fail "RuntimeCount=2 requires ClientsPerSource >= 2 so local and remote fan-out are both exercised"
            }
            $localClientsPerSource = 1
            $remoteClientsPerSource = $ClientsPerSource - 1
        }

        for ($sourceIndex = 1; $sourceIndex -le $SourceCount; ++$sourceIndex) {
            $mount = "${MountPrefix}_$sourceIndex"
            for ($i = 1; $i -le $localClientsPerSource; ++$i) {
                $clients += New-NtripConnection -Kind "client" -Mount $mount -HostName $ConnectAddress -Port $RuntimeANtripPort `
                    -TimeoutMs 10000 -Label "runtime A local client mount=$mount index=$i"
            }
            for ($i = 1; $i -le $remoteClientsPerSource; ++$i) {
                $clients += New-NtripConnection -Kind "client" -Mount $mount -HostName $ConnectAddress -Port $RuntimeBNtripPort `
                    -TimeoutMs 10000 -Label "runtime B remote client mount=$mount index=$i"
            }
        }

        if ($RuntimeCount -eq 2) {
            Wait-Until -TimeoutMs 12000 -Message "runtime B did not subscribe all $SourceCount mounts" -Condition {
                $metrics = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeBHealthPort/metrics"
                [int64]$metrics.redis_subscribed_mount_count -ge $SourceCount
            }
        }

        for ($sourceIndex = 1; $sourceIndex -le $SourceCount; ++$sourceIndex) {
            $mount = "${MountPrefix}_$sourceIndex"
            $sources += New-NtripConnection -Kind "source" -Mount $mount -HostName $ConnectAddress -Port $RuntimeANtripPort `
                -TimeoutMs 10000 -Label "runtime A source mount=$mount"
        }

        $startedAt = [DateTime]::UtcNow
        $nextPayloadAt = $startedAt
        $nextSampleAt = $startedAt
        $sequence = 0
        $payloadIntervalMs = [int][Math]::Max(1, [Math]::Round(1000.0 / [Math]::Max(0.01, $PayloadRateHz)))
        $deadline = $startedAt.AddSeconds($DurationSec)

        while ([DateTime]::UtcNow -lt $deadline) {
            $now = [DateTime]::UtcNow
            if ($now -ge $nextPayloadAt) {
                ++$sequence
                foreach ($source in $sources) {
                    $payload = New-Payload -Mount $source.Mount -Sequence $sequence -Size $PayloadSizeBytes
                    $bytes = [System.Text.Encoding]::ASCII.GetBytes($payload)
                    $source.Stream.Write($bytes, 0, $bytes.Length)
                    $source.Stream.Flush()
                    ++$payloadsSent

                    foreach ($client in @($clients | Where-Object { $_.Mount -eq $source.Mount })) {
                        $receivedBytes = Read-ExactBytes $client.Stream $bytes.Length 10000 $client.Label
                        $received = [System.Text.Encoding]::ASCII.GetString($receivedBytes)
                        ++$payloadsReceived
                        if ($received -ne $payload) {
                            ++$mismatchCount
                            Fail "payload mismatch label=$($client.Label) expected=[$payload] received=[$received]"
                        }
                    }
                }
                $nextPayloadAt = $nextPayloadAt.AddMilliseconds($payloadIntervalMs)
            }

            if ($now -ge $nextSampleAt) {
                $metricsA = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeAHealthPort/metrics"
                $metricsB = $null
                if ($RuntimeCount -eq 2) {
                    $metricsB = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeBHealthPort/metrics"
                }
                $resources = @(
                    Get-ProcessResourceSample -RuntimeID "nc103-capacity-a" -Process $runtimeA -SampleTime $now -LogicalProcessors $logicalProcessors
                )
                if ($runtimeB) {
                    $resources += Get-ProcessResourceSample -RuntimeID "nc103-capacity-b" -Process $runtimeB -SampleTime $now -LogicalProcessors $logicalProcessors
                }
                $samples.Add([PSCustomObject]@{
                    sampled_at_utc = $now.ToString("o")
                    elapsed_sec = [Math]::Round(($now - $startedAt).TotalSeconds, 3)
                    runtime_a_metrics = $metricsA
                    runtime_b_metrics = $metricsB
                    resources = $resources
                }) | Out-Null
                $nextSampleAt = $nextSampleAt.AddSeconds($SampleIntervalSec)
            }

            Start-Sleep -Milliseconds 10
        }

        $finishedAt = [DateTime]::UtcNow
        $finalMetricsA = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeAHealthPort/metrics"
        $finalMetricsB = $null
        if ($RuntimeCount -eq 2) {
            $finalMetricsB = Get-Metrics -Url "http://$ConnectAddress`:$RuntimeBHealthPort/metrics"
        }

        $expectedReceived = $payloadsSent * $ClientsPerSource
        if ($payloadsReceived -ne $expectedReceived) {
            Fail "payload receive count mismatch expected=$expectedReceived actual=$payloadsReceived"
        }
        if ($mismatchCount -ne 0) {
            Fail "payload mismatch count=$mismatchCount"
        }
        if ([int]$finalMetricsA.worker_count -ne $WorkerCountValue) {
            Fail "runtime A worker_count=$($finalMetricsA.worker_count) expected=$WorkerCountValue"
        }
        if ([int64]$finalMetricsA.source_count -ne $SourceCount) {
            Fail "runtime A source_count=$($finalMetricsA.source_count) expected=$SourceCount"
        }
        if ([int64]$finalMetricsA.client_count -ne ($SourceCount * $localClientsPerSource)) {
            Fail "runtime A client_count=$($finalMetricsA.client_count) expected=$($SourceCount * $localClientsPerSource)"
        }
        if ([int64]$finalMetricsA.fanout_write_count -lt ($payloadsSent * $localClientsPerSource)) {
            Fail "runtime A fanout_write_count=$($finalMetricsA.fanout_write_count) expected_at_least=$($payloadsSent * $localClientsPerSource)"
        }
        if ([int64]$finalMetricsA.redis_publish_count -lt $payloadsSent) {
            Fail "runtime A redis_publish_count=$($finalMetricsA.redis_publish_count) expected_at_least=$payloadsSent"
        }
        if ($RuntimeCount -eq 2) {
            if ([int]$finalMetricsB.worker_count -ne $WorkerCountValue) {
                Fail "runtime B worker_count=$($finalMetricsB.worker_count) expected=$WorkerCountValue"
            }
            if ([int64]$finalMetricsB.client_count -ne ($SourceCount * $remoteClientsPerSource)) {
                Fail "runtime B client_count=$($finalMetricsB.client_count) expected=$($SourceCount * $remoteClientsPerSource)"
            }
            if ([int64]$finalMetricsB.redis_remote_fanout_write_count -lt ($payloadsSent * $remoteClientsPerSource)) {
                Fail "runtime B redis_remote_fanout_write_count=$($finalMetricsB.redis_remote_fanout_write_count) expected_at_least=$($payloadsSent * $remoteClientsPerSource)"
            }
            if ([int64]$finalMetricsB.redis_error_count -ne 0) {
                Fail "runtime B redis_error_count=$($finalMetricsB.redis_error_count)"
            }
        }

        $report = [PSCustomObject]@{
            task_id = "NC-103"
            scenario_id = $scenarioId
            commit = (git -C $RootDir rev-parse --short HEAD)
            started_at_utc = $startedAt.ToString("o")
            finished_at_utc = $finishedAt.ToString("o")
            duration_sec = [Math]::Round(($finishedAt - $startedAt).TotalSeconds, 3)
            runtime_count = $RuntimeCount
            worker_count = $WorkerCountValue
            source_count = $SourceCount
            clients_per_source = $ClientsPerSource
            local_clients_per_source = $localClientsPerSource
            remote_clients_per_source = $remoteClientsPerSource
            payload_size_bytes = $PayloadSizeBytes
            payload_rate_hz_per_source = $PayloadRateHz
            payloads_sent = $payloadsSent
            payloads_received = $payloadsReceived
            payload_mismatch_count = $mismatchCount
            sampling_method = $SamplingMethod
            redis = [PSCustomObject]@{
                host = $RedisHost
                port = $RedisPort
                reachable = (Test-TcpPort -HostName $RedisHost -Port $RedisPort -TimeoutMs 1000)
            }
            ports = [PSCustomObject]@{
                runtime_a_ntrip = $RuntimeANtripPort
                runtime_a_health = $RuntimeAHealthPort
                runtime_b_ntrip = if ($RuntimeCount -eq 2) { $RuntimeBNtripPort } else { $null }
                runtime_b_health = if ($RuntimeCount -eq 2) { $RuntimeBHealthPort } else { $null }
            }
            final_runtime_a_metrics = $finalMetricsA
            final_runtime_b_metrics = $finalMetricsB
            samples = $samples
            logs = $ScenarioDir
            scope_note = "Lightweight reproducibility baseline only; this is not a 16k capacity test or long soak."
        }
        $reportPath = Join-Path $ScenarioDir "capacity-baseline.json"
        $report | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $reportPath -Encoding UTF8

        Write-Host "[NC-103 capacity] PASS scenario=$scenarioId"
        Write-Host "[NC-103 capacity] report=$reportPath"
        Write-Host "[NC-103 capacity] runtime_count=$RuntimeCount worker_count=$WorkerCountValue source_count=$SourceCount clients_per_source=$ClientsPerSource"
        Write-Host "[NC-103 capacity] payloads_sent=$payloadsSent payloads_received=$payloadsReceived payload_size_bytes=$PayloadSizeBytes duration_sec=$([Math]::Round(($finishedAt - $startedAt).TotalSeconds, 3))"
        Write-Host "[NC-103 capacity] runtime_a fanout_write_count=$($finalMetricsA.fanout_write_count) redis_publish_count=$($finalMetricsA.redis_publish_count) redis_publish_error_count=$($finalMetricsA.redis_publish_error_count)"
        if ($RuntimeCount -eq 2) {
            Write-Host "[NC-103 capacity] runtime_b redis_subscribe_message_count=$($finalMetricsB.redis_subscribe_message_count) redis_remote_fanout_write_count=$($finalMetricsB.redis_remote_fanout_write_count) redis_error_count=$($finalMetricsB.redis_error_count)"
        }
        return $reportPath
    } finally {
        foreach ($connection in @($clients + $sources)) {
            if ($connection -and $connection.Client) {
                $connection.Client.Close()
            }
        }
        foreach ($process in @($runtimeA, $runtimeB)) {
            if ($process -and -not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit(3000) | Out-Null
            }
        }
    }
}

if (-not (Test-Path -LiteralPath $CasterExe)) {
    Fail "navcaster-caster executable not found: $CasterExe"
}
if ($RuntimeCount -lt 1 -or $RuntimeCount -gt 2) {
    Fail "RuntimeCount must be 1 or 2"
}
if ($SourceCount -lt 1) {
    Fail "SourceCount must be greater than zero"
}
if ($ClientsPerSource -lt 1) {
    Fail "ClientsPerSource must be greater than zero"
}
if ($PayloadSizeBytes -lt 16) {
    Fail "PayloadSizeBytes must be at least 16"
}
if ($DurationSec -lt 1) {
    Fail "DurationSec must be greater than zero"
}
if ($SampleIntervalSec -lt 1) {
    Fail "SampleIntervalSec must be greater than zero"
}
if ($RuntimeCount -eq 2) {
    if ($RuntimeANtripPort -eq $RuntimeBNtripPort -or $RuntimeAHealthPort -eq $RuntimeBHealthPort) {
        Fail "runtime A and B ports must be distinct"
    }
}
if (-not (Test-TcpPort -HostName $RedisHost -Port $RedisPort -TimeoutMs 1500)) {
    Fail "Redis is not reachable at $RedisHost`:$RedisPort; this baseline requires a real Redis fixture because navcaster-caster is started with Redis Pub/Sub enabled"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$reports = @()
foreach ($workerCountValue in $WorkerCount) {
    if ($workerCountValue -lt 1) {
        Fail "WorkerCount values must be greater than zero"
    }
    $reports += Invoke-CapacityScenario -WorkerCountValue $workerCountValue
}

Write-Host "[NC-103 capacity] reports=$($reports -join ',')"
Write-Host "[NC-103 capacity] sampling_method=$SamplingMethod"
