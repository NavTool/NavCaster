param(
    [string]$RedisCli = $env:REDIS_CLI,
    [string]$DockerContainer = $env:REDIS_DOCKER_CONTAINER,
    [string]$HostName = $(if ($env:REDIS_HOST) { $env:REDIS_HOST } else { "127.0.0.1" }),
    [int]$Port = $(if ($env:REDIS_PORT) { [int]$env:REDIS_PORT } else { 6379 }),
    [string]$User = $env:REDIS_USER,
    [string]$Password = $env:REDIS_PASSWORD,
    [int]$Database = $(if ($env:REDIS_DB) { [int]$env:REDIS_DB } else { 0 })
)

$ErrorActionPreference = "Stop"
$MinimumRedisVersion = [version]"8.4.0"

function Fail($Message) {
    [Console]::Error.WriteLine("[redis-compat FAIL] $Message")
    exit 1
}

function Invoke-NativeCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    $oldErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { $_.ToString() })
    }
}

if ($DockerContainer) {
    $dockerCmd = Get-Command docker -ErrorAction SilentlyContinue
    if (-not $dockerCmd) {
        Fail "missing docker; DockerContainer was provided"
    }
}

if (-not $DockerContainer -and -not $RedisCli) {
    $cmd = Get-Command redis-cli -ErrorAction SilentlyContinue
    if ($cmd) {
        $RedisCli = $cmd.Source
    }
}

if (-not $DockerContainer -and $RedisCli) {
    $cmd = Get-Command $RedisCli -ErrorAction SilentlyContinue
    if ($cmd) {
        $RedisCli = $cmd.Source
    }
}

if (-not $DockerContainer -and (-not $RedisCli -or -not (Test-Path $RedisCli))) {
    Fail "missing redis-cli; pass -RedisCli or set REDIS_CLI"
}

$BaseArgs = @("-h", $HostName, "-p", [string]$Port, "-n", [string]$Database, "--raw")
if ($User) {
    $BaseArgs += @("--user", $User)
}
if ($Password) {
    $BaseArgs += @("--no-auth-warning", "-a", $Password)
}

function Invoke-Redis {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CommandArgs)

    if ($DockerContainer) {
        $result = Invoke-NativeCommand "docker" (@("exec", $DockerContainer, "redis-cli") + $BaseArgs + $CommandArgs)
    }
    else {
        $result = Invoke-NativeCommand $RedisCli ($BaseArgs + $CommandArgs)
    }
    if ($result.ExitCode -ne 0) {
        throw ($result.Output -join "`n")
    }
    return ($result.Output -join "`n").Trim()
}

$suffix = "$PID-$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
$hashKey = "NC:COMPAT:${suffix}:HASH"
$leaseKey = "NC:COMPAT:${suffix}:LEASE"

try {
    Write-Host "[redis-compat] target=$HostName`:$Port db=$Database"

    $pong = Invoke-Redis PING
    if ($pong -ne "PONG") {
        Fail "unexpected PING response: $pong"
    }

    $info = Invoke-Redis INFO server
    $versionText = ($info -split "`n" | Where-Object { $_ -match "^redis_version:" } | Select-Object -First 1)
    if (-not $versionText) {
        Fail "INFO server did not expose redis_version"
    }
    $versionString = (($versionText -split ":", 2)[1]).Trim()
    $numericVersion = ($versionString -replace "-.*$", "")
    $redisVersion = [version]$numericVersion
    Write-Host "[redis-compat] redis_version=$versionString"

    if ($redisVersion -lt $MinimumRedisVersion) {
        Fail "Redis $versionString is below NavCaster minimum $MinimumRedisVersion"
    }

    $out = Invoke-Redis HSETEX $hashKey EX 30 FIELDS 1 field value
    if ($out -ne "1" -and $out -ne "OK") {
        Fail "unexpected HSETEX response: $out"
    }

    $out = Invoke-Redis HEXPIRE $hashKey 30 FIELDS 1 field
    if (($out -split "`n")[-1] -ne "1") {
        Fail "unexpected HEXPIRE response: $out"
    }

    $out = Invoke-Redis SET $leaseKey node-a NX EX 30
    if ($out -ne "OK") {
        Fail "unexpected SET NX EX response: $out"
    }

    $out = Invoke-Redis SET $leaseKey node-b IFEQ node-a EX 30
    if ($out -ne "OK") {
        Fail "unexpected SET IFEQ EX response: $out"
    }

    Write-Host "[redis-compat] PASS HSETEX, HEXPIRE, SET IFEQ EX"
}
catch {
    Fail $_.Exception.Message
}
finally {
    try {
        Invoke-Redis DEL $hashKey $leaseKey | Out-Null
    }
    catch {
    }
}
