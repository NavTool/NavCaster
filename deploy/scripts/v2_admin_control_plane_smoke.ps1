param(
    [string]$PostgresImage = "postgres:15",
    [string]$RedisImage = "redis:8.6.3",
    [string]$PostgresContainerName = "navcaster-nc101-postgres-$PID",
    [string]$RedisContainerName = "navcaster-nc101-redis-$PID",
    [int]$PostgresPort = 15432,
    [int]$RedisPort = 16379,
    [int]$AdminPort = 18080,
    [switch]$KeepContainers
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$AdminDir = Join-Path $RootDir "admin"
$BinDir = Join-Path $RootDir "build\nc101-admin-smoke"
$AdminExe = Join-Path $BinDir "navcaster-admin.exe"
$SmokeExe = Join-Path $BinDir "navcaster-admin-smoke.exe"
$PostgresPassword = "navcaster"
$PostgresDSN = "postgres://navcaster:${PostgresPassword}@127.0.0.1:${PostgresPort}/navcaster?sslmode=disable"
$RedisAddr = "127.0.0.1:${RedisPort}"
$AdminBaseUrl = "http://127.0.0.1:${AdminPort}"
$AdminProc = $null

function Say([string]$Message) {
    Write-Host "[NC-101 smoke] $Message"
}

function Fail([string]$Message) {
    throw "[NC-101 smoke] $Message"
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments) {
    $output = & $FilePath @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        Fail "$FilePath $($Arguments -join ' ') failed with exit $LASTEXITCODE`n$($output -join "`n")"
    }
    return $output
}

function Wait-HttpOk([string]$Url, [int]$TimeoutSeconds = 30) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri $Url -TimeoutSec 2
            if ($response.StatusCode -eq 200) {
                return
            }
        } catch {
            Start-Sleep -Milliseconds 500
        }
    } while ((Get-Date) -lt $deadline)
    Fail "HTTP endpoint did not become ready: $Url"
}

function Wait-DockerLog([string]$ContainerName, [string]$Pattern, [int]$TimeoutSeconds = 30) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $logs = & cmd /c "docker logs $ContainerName 2>&1"
        if (($logs -join "`n") -match $Pattern) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    Fail "container $ContainerName did not report readiness"
}

try {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        Fail "missing docker"
    }

    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

    Say "starting PostgreSQL fixture $PostgresContainerName"
    Invoke-Native docker @(
        "run", "-d", "--rm",
        "--name", $PostgresContainerName,
        "-p", "127.0.0.1:${PostgresPort}:5432",
        "-e", "POSTGRES_USER=navcaster",
        "-e", "POSTGRES_PASSWORD=$PostgresPassword",
        "-e", "POSTGRES_DB=navcaster",
        $PostgresImage
    ) | Out-Null
    Wait-DockerLog $PostgresContainerName "database system is ready to accept connections"

    Say "starting Redis fixture $RedisContainerName"
    Invoke-Native docker @(
        "run", "-d", "--rm",
        "--name", $RedisContainerName,
        "-p", "127.0.0.1:${RedisPort}:6379",
        $RedisImage,
        "redis-server", "--save", "", "--appendonly", "no"
    ) | Out-Null

    Say "building navcaster-admin and smoke binary"
    Push-Location $AdminDir
    try {
        Invoke-Native go @("build", "-o", $AdminExe, ".\cmd\navcaster-admin") | Out-Null
        Invoke-Native go @("build", "-o", $SmokeExe, ".\cmd\navcaster-admin-smoke") | Out-Null
    } finally {
        Pop-Location
    }

    Say "starting navcaster-admin on port $AdminPort"
    $env:NAVCASTER_ADMIN_ADDR = "127.0.0.1:${AdminPort}"
    $env:NAVCASTER_ADMIN_POSTGRES_DSN = $PostgresDSN
    $env:NAVCASTER_ADMIN_REDIS_ADDR = $RedisAddr
    $AdminProc = Start-Process -FilePath $AdminExe -PassThru -WindowStyle Hidden
    Wait-HttpOk "$AdminBaseUrl/api/v1/health" 30

    Say "running create runtime / action / actual / events / projection consistency smoke"
    $env:NAVCASTER_ADMIN_SMOKE_URL = $AdminBaseUrl
    $env:NAVCASTER_ADMIN_SMOKE_REDIS_ADDR = $RedisAddr
    Invoke-Native $SmokeExe @() | ForEach-Object { Write-Host $_ }

    Say "PASS"
} finally {
    if ($AdminProc -and -not $AdminProc.HasExited) {
        Stop-Process -Id $AdminProc.Id -Force -ErrorAction SilentlyContinue
    }
    if (-not $KeepContainers) {
        try { docker rm -f $PostgresContainerName 2>$null | Out-Null } catch {}
        try { docker rm -f $RedisContainerName 2>$null | Out-Null } catch {}
    }
}
