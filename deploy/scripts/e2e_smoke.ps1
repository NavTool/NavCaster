param(
    [string]$RootPath,
    [ValidateSet("Docker", "External")]
    [string]$RedisMode = "Docker",
    [string]$RedisImage = "redis:8.6.3",
    [string]$RedisContainerName = "navcaster-e2e-redis-$PID",
    [string]$RedisHost = "127.0.0.1",
    [int]$RedisPort = 16379,
    [string]$RedisPassword = "password",
    [string]$Configuration = "Release",
    [int]$HttpPort = 8080,
    [string]$HttpBindAddr = "127.0.0.1",
    [string]$AdminUser = "admin",
    [string]$AdminPassword = "admin",
    [int]$StartupTimeoutSec = 45,
    [switch]$SkipRedisCompat,
    [switch]$KeepRedisContainer,
    [switch]$IncludeActiveAccounts
)

$ErrorActionPreference = "Stop"

try {
    Add-Type -AssemblyName System.Net.Http
}
catch {
}

function Fail($Message) {
    throw $Message
}

function Say($Message) {
    Write-Host "[e2e-smoke] $Message"
}

function Write-TextFile {
    param(
        [string]$Path,
        [string]$Text
    )

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
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

function Invoke-NativeCommandWithInput {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$InputText
    )

    $oldErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = $InputText | & $FilePath @Arguments 2>&1
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

function Set-YamlValueInSection {
    param(
        [string]$Text,
        [string]$Section,
        [string]$Key,
        [string]$Value
    )

    $lines = $Text -split "`r?`n"
    $inSection = $false
    $foundSection = $false
    $foundKey = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^\S[^:]*:\s*(?:#.*)?$') {
            $name = ($line -split ":", 2)[0].Trim()
            if ($name -eq $Section) {
                $inSection = $true
                $foundSection = $true
                continue
            }
            if ($inSection) {
                $inSection = $false
            }
        }

        if ($inSection -and $line -match ("^(\s*)" + [regex]::Escape($Key) + "\s*:\s*(.*)$")) {
            $indent = $Matches[1]
            $rest = $Matches[2]
            $comment = ""
            if ($rest -match '(\s+#.*)$') {
                $comment = $Matches[1]
            }
            $lines[$i] = "${indent}${Key}: $Value$comment"
            $foundKey = $true
            break
        }
    }

    if (-not $foundSection) {
        throw "section not found: $Section"
    }
    if (-not $foundKey) {
        throw "key not found: $Section.$Key"
    }

    return ($lines -join "`r`n")
}

function Invoke-RedisInDocker {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CommandArgs)

    $baseArgs = @("exec", $RedisContainerName, "redis-cli", "-h", "127.0.0.1", "-p", "6379", "--raw")
    if ($RedisPassword) {
        $baseArgs += @("--no-auth-warning", "-a", $RedisPassword)
    }
    $result = Invoke-NativeCommand "docker" ($baseArgs + $CommandArgs)
    if ($result.ExitCode -ne 0) {
        throw ($result.Output -join "`n")
    }
    return ($result.Output -join "`n").Trim()
}

function Resolve-ExternalRedisCli {
    $redisCli = $env:REDIS_CLI
    if (-not $redisCli) {
        $cmd = Get-Command redis-cli -ErrorAction SilentlyContinue
        if ($cmd) {
            $redisCli = $cmd.Source
        }
    }
    if ($redisCli) {
        $cmd = Get-Command $redisCli -ErrorAction SilentlyContinue
        if ($cmd) {
            $redisCli = $cmd.Source
        }
    }
    if (-not $redisCli -or -not (Test-Path -LiteralPath $redisCli)) {
        Fail "missing redis-cli; active account e2e in External mode requires redis-cli or REDIS_CLI"
    }
    return $redisCli
}

function Invoke-RedisCommand {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CommandArgs)

    if ($RedisMode -eq "Docker") {
        return Invoke-RedisInDocker @CommandArgs
    }

    $redisCli = Resolve-ExternalRedisCli
    $baseArgs = @("-h", $RedisHost, "-p", [string]$RedisPort, "-n", "0", "--raw")
    if ($RedisPassword) {
        $baseArgs += @("--no-auth-warning", "-a", $RedisPassword)
    }
    $result = Invoke-NativeCommand $redisCli ($baseArgs + $CommandArgs)
    if ($result.ExitCode -ne 0) {
        throw ($result.Output -join "`n")
    }
    return ($result.Output -join "`n").Trim()
}

function Invoke-RedisHSetValue {
    param(
        [string]$Key,
        [string]$Field,
        [string]$Value
    )

    if ($RedisMode -eq "Docker") {
        $baseArgs = @("exec", "-i", $RedisContainerName, "redis-cli", "-h", "127.0.0.1", "-p", "6379", "--raw")
        if ($RedisPassword) {
            $baseArgs += @("--no-auth-warning", "-a", $RedisPassword)
        }
        $result = Invoke-NativeCommandWithInput "docker" ($baseArgs + @("-x", "HSET", $Key, $Field)) $Value
        if ($result.ExitCode -ne 0) {
            throw ($result.Output -join "`n")
        }
        return ($result.Output -join "`n").Trim()
    }

    $redisCli = Resolve-ExternalRedisCli
    $baseArgs = @("-h", $RedisHost, "-p", [string]$RedisPort, "-n", "0", "--raw")
    if ($RedisPassword) {
        $baseArgs += @("--no-auth-warning", "-a", $RedisPassword)
    }
    $result = Invoke-NativeCommandWithInput $redisCli ($baseArgs + @("-x", "HSET", $Key, $Field)) $Value
    if ($result.ExitCode -ne 0) {
        throw ($result.Output -join "`n")
    }
    return ($result.Output -join "`n").Trim()
}

function ConvertTo-CompactJson {
    param([object]$InputObject)
    return ($InputObject | ConvertTo-Json -Compress -Depth 8)
}

function New-ActiveSessionRecord {
    param(
        [string]$Account,
        [string]$ConnectKey,
        [string]$GroupUid,
        [string]$Marker,
        [switch]$IncludePasswordMaterial
    )

    $record = [ordered]@{
        uid = $ConnectKey
        connect_key = $ConnectKey
        account = $Account
        anonymous = $false
        auth_type = "client"
        online_time = 1710000000
        update_time = 1710000010
        addr = "127.0.0.1"
        port = "2101"
        group_uid = $GroupUid
        marker = $Marker
    }
    if ($IncludePasswordMaterial) {
        $record["password"] = "must-not-leak"
        $record["password_hash"] = "must-not-leak"
        $record["password_algo"] = "plain"
        $record["password_salt"] = "must-not-leak"
        $record["password_iterations"] = 1
        $record["old_password"] = "must-not-leak"
    }
    return ConvertTo-CompactJson $record
}

function New-ActiveAccountSeed {
    $prefix = "nc016-$PID-$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
    $legacyOnlyAccount = "$prefix-legacy-account"
    $sessionOnlyAccount = "$prefix-session-account"
    $conflictLegacyAccount = "$prefix-legacy-conflict-account"
    $conflictSessionAccount = "$prefix-session-conflict-account"
    $multiAccount = "$prefix-multi-account"

    return [pscustomobject]@{
        Prefix = $prefix
        LegacyOnlyField = "$prefix-legacy-only"
        SessionOnlyField = "$prefix-session-only"
        ConflictField = "$prefix-conflict"
        MultiField1 = "$prefix-multi-1"
        MultiField2 = "$prefix-multi-2"
        ActiveIndexField = "$prefix-active-index-only"
        LegacyOnlyAccount = $legacyOnlyAccount
        SessionOnlyAccount = $sessionOnlyAccount
        ConflictLegacyAccount = $conflictLegacyAccount
        ConflictSessionAccount = $conflictSessionAccount
        MultiAccount = $multiAccount
        LegacyFields = @("$prefix-legacy-only", "$prefix-conflict")
        SessionKeys = @(
            "ACT:SESSION:$sessionOnlyAccount",
            "ACT:SESSION:$conflictSessionAccount",
            "ACT:SESSION:$multiAccount"
        )
    }
}

function Add-ActiveAccountSeed {
    param([pscustomobject]$Seed)

    Say "seeding active account Redis records prefix=$($Seed.Prefix)"
    Invoke-RedisHSetValue "STR:ACTIVE" $Seed.LegacyOnlyField `
        (New-ActiveSessionRecord $Seed.LegacyOnlyAccount $Seed.LegacyOnlyField "legacy" "legacy-only" -IncludePasswordMaterial) | Out-Null
    Invoke-RedisHSetValue "STR:ACTIVE" $Seed.ConflictField `
        (New-ActiveSessionRecord $Seed.ConflictLegacyAccount $Seed.ConflictField "legacy" "legacy-loses" -IncludePasswordMaterial) | Out-Null
    Invoke-RedisHSetValue "ACT:SESSION:$($Seed.SessionOnlyAccount)" $Seed.SessionOnlyField `
        (New-ActiveSessionRecord $Seed.SessionOnlyAccount $Seed.SessionOnlyField "session" "session-only" -IncludePasswordMaterial) | Out-Null
    Invoke-RedisHSetValue "ACT:SESSION:$($Seed.ConflictSessionAccount)" $Seed.ConflictField `
        (New-ActiveSessionRecord $Seed.ConflictSessionAccount $Seed.ConflictField "session" "session-wins" -IncludePasswordMaterial) | Out-Null
    Invoke-RedisHSetValue "ACT:SESSION:$($Seed.MultiAccount)" $Seed.MultiField1 `
        (New-ActiveSessionRecord $Seed.MultiAccount $Seed.MultiField1 "session" "multi-1") | Out-Null
    Invoke-RedisHSetValue "ACT:SESSION:$($Seed.MultiAccount)" $Seed.MultiField2 `
        (New-ActiveSessionRecord $Seed.MultiAccount $Seed.MultiField2 "session" "multi-2") | Out-Null

    $activeIndex = [ordered]@{
        account = "$($Seed.Prefix)-login-index"
        password = "must-not-appear"
        group_uid = "default"
    }
    Invoke-RedisHSetValue "ACT:ACTIVE" $Seed.ActiveIndexField (ConvertTo-CompactJson $activeIndex) | Out-Null
}

function Assert-ActiveAccountSeedExists {
    param([pscustomobject]$Seed)

    $legacy = Invoke-RedisCommand HGETALL "STR:ACTIVE"
    if ($legacy -notmatch [regex]::Escape($Seed.LegacyOnlyField)) {
        Fail "active account Redis seed missing from STR:ACTIVE"
    }
    $legacyValue = Invoke-RedisCommand HGET "STR:ACTIVE" $Seed.LegacyOnlyField
    try {
        $legacyJson = $legacyValue | ConvertFrom-Json
        if ($legacyJson.account -ne $Seed.LegacyOnlyAccount -or $legacyJson.marker -ne "legacy-only") {
            Fail "active account STR:ACTIVE seed JSON mismatch"
        }
    }
    catch {
        Fail "active account STR:ACTIVE seed is not valid JSON: $legacyValue"
    }

    $session = Invoke-RedisCommand HGETALL "ACT:SESSION:$($Seed.SessionOnlyAccount)"
    if ($session -notmatch [regex]::Escape($Seed.SessionOnlyField)) {
        Fail "active account Redis seed missing from ACT:SESSION session-only hash"
    }
    $sessionValue = Invoke-RedisCommand HGET "ACT:SESSION:$($Seed.SessionOnlyAccount)" $Seed.SessionOnlyField
    try {
        $sessionJson = $sessionValue | ConvertFrom-Json
        if ($sessionJson.account -ne $Seed.SessionOnlyAccount -or $sessionJson.marker -ne "session-only") {
            Fail "active account ACT:SESSION seed JSON mismatch"
        }
    }
    catch {
        Fail "active account ACT:SESSION seed is not valid JSON: $sessionValue"
    }
}

function Format-DebugText {
    param(
        [string]$Text,
        [int]$MaxLength = 320
    )

    if ($null -eq $Text) {
        return ""
    }
    $oneLine = ($Text -replace "[`r`n]+", " ")
    if ($oneLine.Length -gt $MaxLength) {
        return $oneLine.Substring(0, $MaxLength) + "..."
    }
    return $oneLine
}

function Remove-ActiveAccountSeed {
    param([pscustomobject]$Seed)

    if (-not $Seed) {
        return
    }
    $hdelLegacy = @("HDEL", "STR:ACTIVE") + $Seed.LegacyFields
    Invoke-RedisCommand @hdelLegacy | Out-Null
    Invoke-RedisCommand HDEL "ACT:ACTIVE" $Seed.ActiveIndexField | Out-Null
    $delSessions = @("DEL") + $Seed.SessionKeys
    Invoke-RedisCommand @delSessions | Out-Null
}

function Get-JsonProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $null
    }
    $prop = $Object.PSObject.Properties[$Name]
    if ($prop) {
        return $prop.Value
    }
    return $null
}

function Require-JsonProperty {
    param(
        [object]$Object,
        [string]$Name,
        [string]$Context
    )

    $value = Get-JsonProperty $Object $Name
    if ($null -eq $value) {
        $keys = @()
        if ($null -ne $Object) {
            $keys = @($Object.PSObject.Properties | ForEach-Object { $_.Name })
        }
        Fail "$Context missing field $Name; actual fields=[$($keys -join ',')]"
    }
    return $value
}

function Assert-NoPasswordMaterial {
    param(
        [object]$Record,
        [string]$Context
    )

    foreach ($field in @("password", "password_hash", "password_algo", "password_salt", "password_iterations", "old_password")) {
        if ($null -ne (Get-JsonProperty $Record $field)) {
            Fail "$Context leaked password material field $field"
        }
    }
}

function Assert-ActiveAccountPayload {
    param(
        [object]$Payload,
        [pscustomobject]$Seed,
        [string]$Label
    )

    $legacy = Require-JsonProperty $Payload $Seed.LegacyOnlyField "$Label active account payload"
    if ($legacy.account -ne $Seed.LegacyOnlyAccount -or $legacy.marker -ne "legacy-only") {
        Fail "$Label legacy-only record mismatch"
    }
    Assert-NoPasswordMaterial $legacy "$Label legacy-only"

    $session = Require-JsonProperty $Payload $Seed.SessionOnlyField "$Label active account payload"
    if ($session.account -ne $Seed.SessionOnlyAccount -or $session.marker -ne "session-only") {
        Fail "$Label session-only record mismatch"
    }
    Assert-NoPasswordMaterial $session "$Label session-only"

    $conflict = Require-JsonProperty $Payload $Seed.ConflictField "$Label active account payload"
    if ($conflict.account -ne $Seed.ConflictSessionAccount -or $conflict.marker -ne "session-wins") {
        Fail "$Label conflict record did not prefer ACT:SESSION"
    }
    Assert-NoPasswordMaterial $conflict "$Label conflict"

    $multi1 = Require-JsonProperty $Payload $Seed.MultiField1 "$Label active account payload"
    $multi2 = Require-JsonProperty $Payload $Seed.MultiField2 "$Label active account payload"
    if ($multi1.account -ne $Seed.MultiAccount -or $multi2.account -ne $Seed.MultiAccount) {
        Fail "$Label multi-connection records do not share expected account"
    }

    if ($null -ne (Get-JsonProperty $Payload $Seed.ActiveIndexField)) {
        Fail "$Label included ACT:ACTIVE login index as an active session"
    }
}

function Read-SseEvent {
    param(
        [string]$Uri,
        [string]$ExpectedEvent,
        [int]$TimeoutSec
    )

    $client = $null
    $response = $null
    $reader = $null
    try {
        $client = New-Object System.Net.Http.HttpClient
        $client.Timeout = [TimeSpan]::FromSeconds($TimeoutSec + 5)
        $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, $Uri)
        $response = $client.SendAsync($request, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) {
            Fail "SSE request failed with HTTP $([int]$response.StatusCode)"
        }

        $stream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $reader = New-Object System.IO.StreamReader($stream)
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        $currentEvent = ""
        $dataLines = New-Object System.Collections.Generic.List[string]
        $lineTask = $reader.ReadLineAsync()

        while ((Get-Date) -lt $deadline) {
            if (-not $lineTask.Wait(1000)) {
                continue
            }
            $line = $lineTask.Result
            if ($null -eq $line) {
                break
            }

            if ($line.Length -eq 0) {
                if ($currentEvent -eq $ExpectedEvent -and $dataLines.Count -gt 0) {
                    $data = $dataLines -join "`n"
                    return ($data | ConvertFrom-Json)
                }
                $currentEvent = ""
                $dataLines.Clear()
            }
            elseif ($line.StartsWith("event:")) {
                $currentEvent = $line.Substring(6).Trim()
            }
            elseif ($line.StartsWith("data:")) {
                $dataLines.Add($line.Substring(5).TrimStart())
            }

            $lineTask = $reader.ReadLineAsync()
        }
    }
    finally {
        if ($reader) { $reader.Dispose() }
        if ($response) { $response.Dispose() }
        if ($client) { $client.Dispose() }
    }

    Fail "SSE event '$ExpectedEvent' was not received before timeout"
}

function Invoke-ActiveAccountSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Token
    )

    $script:activeAccountSeed = New-ActiveAccountSeed
    Add-ActiveAccountSeed $script:activeAccountSeed
    Assert-ActiveAccountSeedExists $script:activeAccountSeed

    Say "validating /api/accounts/active against seeded Redis records"
    $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
    if (($rest.PSObject.Properties | Measure-Object).Count -eq 0) {
        $status = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/status" -TimeoutSec 10
        $redisKeys = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/redis/keys" -TimeoutSec 10
        $categories = if ($redisKeys.categories) { ($redisKeys.categories | ForEach-Object { "$($_.prefix):$($_.count)" }) -join "," } else { "" }
        $legacyRaw = Format-DebugText (Invoke-RedisCommand HGETALL "STR:ACTIVE")
        $sessionRaw = Format-DebugText (Invoke-RedisCommand HGETALL "ACT:SESSION:$($script:activeAccountSeed.SessionOnlyAccount)")
        Fail "REST /api/accounts/active returned empty after Redis seed; redis_auth_connected=$($status.redis_auth_connected), redis_caster_connected=$($status.redis_caster_connected), redis_key_categories=[$categories], str_active=[$legacyRaw], act_session=[$sessionRaw]"
    }
    Assert-ActiveAccountPayload $rest $script:activeAccountSeed "REST /api/accounts/active"

    Say "validating SSE account_actives initial snapshot"
    $sseUri = "$Base/api/events/stream?token=$Token&channels=account_actives"
    $sse = Read-SseEvent $sseUri "account_actives" $StartupTimeoutSec
    Assert-ActiveAccountPayload $sse $script:activeAccountSeed "SSE account_actives"

    Remove-ActiveAccountSeed $script:activeAccountSeed
    $script:activeAccountSeed = $null
    Say "PASS active account REST/SSE smoke"
}

if (-not $RootPath) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $RootPath = Join-Path $scriptDir "..\.."
}

$RootPath = (Resolve-Path -LiteralPath $RootPath).Path
$releaseDir = Join-Path $RootPath "bin\$Configuration"
$serviceExe = Join-Path $releaseDir "CasterService.exe"
$confDir = Join-Path $releaseDir "conf"
$serviceConfig = Join-Path $confDir "Service_Setting.yml"
$coreConfig = Join-Path $confDir "Caster_Core.yml"
$authConfig = Join-Path $confDir "Auth_Verify.yml"
$compatScript = Join-Path $RootPath "deploy\scripts\check_redis_compat.ps1"

foreach ($path in @($serviceExe, $serviceConfig, $coreConfig, $authConfig, $compatScript)) {
    if (-not (Test-Path -LiteralPath $path)) {
        Fail "missing required file: $path. Build CasterService before running e2e smoke."
    }
}

$startedContainer = $false
$serviceProcess = $null
$originals = @{}
$activeAccountSeed = $null
$stdout = Join-Path $env:TEMP ("navcaster-e2e-" + [guid]::NewGuid().ToString() + ".out.log")
$stderr = Join-Path $env:TEMP ("navcaster-e2e-" + [guid]::NewGuid().ToString() + ".err.log")
$scriptFailed = $false

try {
    Say "root=$RootPath configuration=$Configuration redis_mode=$RedisMode"

    if ($RedisMode -eq "Docker") {
        $dockerCmd = Get-Command docker -ErrorAction SilentlyContinue
        if (-not $dockerCmd) {
            Fail "missing docker. Install Docker or rerun with -RedisMode External and a compatible Redis 8.4+ instance."
        }

        $dockerInfo = Invoke-NativeCommand "docker" @("info")
        if ($dockerInfo.ExitCode -ne 0) {
            Fail ("docker engine is not available: " + (($dockerInfo.Output | Select-Object -First 6) -join " "))
        }

        Say "starting Redis fixture container $RedisContainerName image=$RedisImage port=$RedisPort"
        $existingResult = Invoke-NativeCommand "docker" @("ps", "-a", "--filter", "name=^/$RedisContainerName$", "--format", "{{.Names}}")
        $existing = $existingResult.Output | Where-Object { $_ -eq $RedisContainerName } | Select-Object -First 1
        if ($existing -eq $RedisContainerName) {
            Fail "container already exists: $RedisContainerName"
        }

        $runResult = Invoke-NativeCommand "docker" @(
            "run", "-d",
            "--name", $RedisContainerName,
            "-p", "127.0.0.1:${RedisPort}:6379",
            $RedisImage,
            "redis-server", "--requirepass", $RedisPassword, "--save", "", "--appendonly", "no"
        )
        if ($runResult.ExitCode -ne 0) {
            if (-not $KeepRedisContainer) {
                Invoke-NativeCommand "docker" @("rm", "-f", $RedisContainerName) | Out-Null
            }
            Fail ("failed to start Redis container: " + ($runResult.Output -join " "))
        }
        $startedContainer = $true

        $redisReady = $false
        $deadline = (Get-Date).AddSeconds($StartupTimeoutSec)
        do {
            Start-Sleep -Milliseconds 500
            try {
                if ((Invoke-RedisInDocker PING) -eq "PONG") {
                    $redisReady = $true
                    break
                }
            }
            catch {
            }
        } while ((Get-Date) -lt $deadline)

        if (-not $redisReady) {
            Fail "Redis fixture did not become ready before timeout"
        }

        if (-not $SkipRedisCompat) {
            Say "running Redis compatibility check through docker exec"
            $compatResult = Invoke-NativeCommand "powershell" @(
                "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $compatScript,
                "-DockerContainer", $RedisContainerName,
                "-HostName", "127.0.0.1",
                "-Port", "6379",
                "-Password", $RedisPassword
            )
            $compatResult.Output | ForEach-Object { Write-Host $_ }
            if ($compatResult.ExitCode -ne 0) {
                Fail "Redis compatibility check failed"
            }
        }
    }
    else {
        if (-not $SkipRedisCompat) {
            Say "running Redis compatibility check against external Redis $RedisHost`:$RedisPort"
            $compatResult = Invoke-NativeCommand "powershell" @(
                "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $compatScript,
                "-HostName", $RedisHost,
                "-Port", ([string]$RedisPort),
                "-Password", $RedisPassword
            )
            $compatResult.Output | ForEach-Object { Write-Host $_ }
            if ($compatResult.ExitCode -ne 0) {
                Fail "Redis compatibility check failed"
            }
        }
        else {
            Say "Redis compatibility check explicitly skipped"
        }
    }

    foreach ($path in @($serviceConfig, $coreConfig, $authConfig)) {
        $originals[$path] = Get-Content -LiteralPath $path -Raw
    }

    $serviceText = $originals[$serviceConfig]
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Port" ([string]$HttpPort)
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Bind_Addr" "`"$HttpBindAddr`""
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Force_Enable" "true"
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Admin_User" "`"$AdminUser`""
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Admin_Password" "`"$AdminPassword`""
    Write-TextFile $serviceConfig $serviceText

    $coreText = $originals[$coreConfig]
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "IP" $RedisHost
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "Port" ([string]$RedisPort)
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    Write-TextFile $coreConfig $coreText

    $authText = $originals[$authConfig]
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "IP" $RedisHost
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Port" ([string]$RedisPort)
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    Write-TextFile $authConfig $authText

    $base = "http://${HttpBindAddr}:${HttpPort}"
    Say "starting CasterService and waiting for $base/api/status/health"
    $serviceProcess = Start-Process -FilePath $serviceExe `
        -WorkingDirectory $releaseDir `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -PassThru

    $healthOk = $false
    $deadline = (Get-Date).AddSeconds($StartupTimeoutSec)
    do {
        Start-Sleep -Milliseconds 800
        if ($serviceProcess.HasExited) {
            break
        }
        try {
            $health = Invoke-RestMethod -Uri "$base/api/status/health" -TimeoutSec 3
            if ($health.status -eq "ok") {
                $healthOk = $true
                break
            }
        }
        catch {
        }
    } while ((Get-Date) -lt $deadline)

    if (-not $healthOk) {
        $exitText = if ($serviceProcess.HasExited) { "exited=$($serviceProcess.ExitCode)" } else { "still-running" }
        Fail "health endpoint did not become ready: $exitText"
    }

    Say "logging in as $AdminUser"
    $loginBody = @{ username = $AdminUser; password = $AdminPassword } | ConvertTo-Json -Compress
    $login = Invoke-RestMethod -Method Post -ContentType "application/json" -Body $loginBody -Uri "$base/api/auth/login" -TimeoutSec 10
    if (-not $login.token) {
        Fail "login did not return token"
    }

    $headers = @{ Authorization = "Bearer $($login.token)" }
    $status = Invoke-RestMethod -Headers $headers -Uri "$base/api/status" -TimeoutSec 10
    if ($null -eq $status.node_id -or $null -eq $status.cpu_percent) {
        Fail "status response missing node_id or cpu_percent"
    }
    if ($status.redis_auth_connected -ne $true -or $status.redis_caster_connected -ne $true) {
        Fail "status response reports Redis disconnected: caster=$($status.redis_caster_connected) auth=$($status.redis_auth_connected)"
    }

    $cluster = Invoke-RestMethod -Headers $headers -Uri "$base/api/monitor/cluster" -TimeoutSec 10
    if ($null -eq $cluster.nodes) {
        Fail "cluster monitor response missing nodes"
    }

    if ($IncludeActiveAccounts) {
        Invoke-ActiveAccountSmoke $base $headers $login.token
    }

    Say "PASS health/login/status/cluster smoke"
}
catch {
    $scriptFailed = $true
    [Console]::Error.WriteLine("[e2e-smoke FAIL] $($_.Exception.Message)")
}
finally {
    try {
        if ($activeAccountSeed) {
            Remove-ActiveAccountSeed $activeAccountSeed
            $activeAccountSeed = $null
        }
    }
    catch {
        Write-Warning "failed to remove active account Redis seed: $($_.Exception.Message)"
    }

    try {
        if ($serviceProcess -and -not $serviceProcess.HasExited) {
            Stop-Process -Id $serviceProcess.Id -Force
            Start-Sleep -Milliseconds 500
        }
    }
    catch {
        Write-Warning "failed to stop CasterService: $($_.Exception.Message)"
    }

    try {
        foreach ($entry in $originals.GetEnumerator()) {
            Write-TextFile $entry.Key $entry.Value
        }
    }
    catch {
        Write-Warning "failed to restore generated config files: $($_.Exception.Message)"
    }

    try {
        if ($startedContainer -and -not $KeepRedisContainer) {
            Invoke-NativeCommand "docker" @("rm", "-f", $RedisContainerName) | Out-Null
        }
    }
    catch {
        Write-Warning "failed to remove Redis fixture container: $($_.Exception.Message)"
    }

    try {
        $left = Get-Process | Where-Object { $_.ProcessName -like "*CasterService*" } | Select-Object Id, ProcessName, Path
        if ($left) {
            Write-Host "[e2e-smoke] residual CasterService processes:"
            $left | Format-Table -AutoSize | Out-String | Write-Host
        }
    }
    catch {
    }
}

if ($scriptFailed) {
    exit 1
}
