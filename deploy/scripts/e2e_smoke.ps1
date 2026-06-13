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
    [int]$NtripPort = 4202,
    [int]$NtripBroadcastHttpPort = 8081,
    [int]$NtripBroadcastNtripPort = 4203,
    [int]$StartupTimeoutSec = 45,
    [switch]$SkipRedisCompat,
    [switch]$KeepRedisContainer,
    [switch]$IncludeActiveAccounts,
    [switch]$IncludeActiveAccountSseDelta,
    [switch]$IncludeNtripAuthSession,
    [switch]$IncludeNtripAuthSessionRenewal,
    [int]$NtripRenewalWaitSec = 25,
    [switch]$IncludeNtripOnlineProtection,
    [ValidateSet("RejectNew", "KickOld")]
    [string]$NtripOnlineProtectionScenario = "RejectNew",
    [switch]$IncludeNtripAnonymousAuth,
    [ValidateSet("AllowAnonymous", "RejectAnonymous")]
    [string]$NtripAnonymousScenario = "AllowAnonymous",
    [switch]$IncludeNtripAuthBroadcast
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

function Get-RedisHashFieldTtl {
    param(
        [string]$Key,
        [string]$Field
    )

    $raw = Invoke-RedisCommand HTTL $Key FIELDS 1 $Field
    $lines = @($raw -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($lines.Count -eq 0) {
        return -2
    }

    try {
        return [int]($lines[-1].Trim())
    }
    catch {
        Fail "unexpected HTTL response for ${Key} ${Field}: $(Format-DebugText $raw)"
    }
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

function ConvertTo-BasicAuthValue {
    param(
        [string]$User,
        [string]$Password
    )

    $bytes = [System.Text.Encoding]::ASCII.GetBytes("${User}:$Password")
    return [Convert]::ToBase64String($bytes)
}

function ConvertFrom-RedisHashRaw {
    param([string]$Raw)

    $map = @{}
    if ([string]::IsNullOrWhiteSpace($Raw)) {
        return $map
    }

    $lines = @($Raw -split "`r?`n")
    for ($i = 0; ($i + 1) -lt $lines.Count; $i += 2) {
        $map[$lines[$i]] = $lines[$i + 1]
    }
    return $map
}

function Get-RedisHashMap {
    param([string]$Key)

    $raw = Invoke-RedisCommand HGETALL $Key
    return ConvertFrom-RedisHashRaw $raw
}

function Get-RedisKeys {
    param([string]$Pattern)

    $raw = Invoke-RedisCommand KEYS $Pattern
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return @()
    }
    return @($raw -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object)
}

function Test-FieldSetEquals {
    param(
        [string[]]$ActualFields,
        [string[]]$ExpectedFields
    )

    $actual = @($ActualFields | Sort-Object)
    $expected = @($ExpectedFields | Sort-Object)
    return (($actual -join "`n") -eq ($expected -join "`n"))
}

function New-NtripAuthSessionSeed {
    param(
        [string]$Label = "nc017",
        [int]$ConnectionLimit = 9999
    )

    $prefix = "${Label}_${PID}_$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
    return [pscustomobject]@{
        Prefix = $prefix
        Mount = "${prefix}_MPT"
        Account = "${prefix}_rover"
        Password = "${prefix}_pw"
        SourceUser = "${prefix}_source"
        SourcePassword = "${prefix}_source_pw"
        GroupUid = "default"
        ConnectionLimit = $ConnectionLimit
        SourceConnectKey = ""
        ClientConnectKey = ""
        ExtraClientConnectKeys = @()
    }
}

function Add-NtripAuthAccountSeed {
    param([pscustomobject]$Seed)

    Say "seeding NTRIP auth ACT:ACTIVE record account=$($Seed.Account) mount=$($Seed.Mount)"
    $activeIndex = [ordered]@{
        schema_version = 1
        uid = $Seed.Account
        account = $Seed.Account
        password = $Seed.Password
        legacy_plain_password = $true
        group_uid = $Seed.GroupUid
        connection_limit = $Seed.ConnectionLimit
        type = 0
        state = 1
        active = 1
        expire_time = 0
    }
    Invoke-RedisHSetValue "ACT:ACTIVE" $Seed.Account (ConvertTo-CompactJson $activeIndex) | Out-Null
}

function Remove-NtripAuthSessionSeed {
    param([pscustomobject]$Seed)

    if (-not $Seed) {
        return
    }

    Invoke-RedisCommand HDEL "ACT:ACTIVE" $Seed.Account | Out-Null
    Invoke-RedisCommand HDEL "ACT:UNNAMED" $Seed.SourceUser | Out-Null
    Invoke-RedisCommand HDEL "MPT:LIST" $Seed.Mount | Out-Null
    Invoke-RedisCommand HDEL "USR:LIST" $Seed.Account | Out-Null
    Invoke-RedisCommand HDEL "MPT:SOURCE" $Seed.Mount | Out-Null
    if ($Seed.SourceConnectKey) {
        Invoke-RedisCommand HDEL "MPT:STAT" $Seed.SourceConnectKey | Out-Null
        Invoke-RedisCommand HDEL "STR:STAT" $Seed.SourceConnectKey | Out-Null
    }
    if ($Seed.ClientConnectKey) {
        Invoke-RedisCommand HDEL "USR:STAT" $Seed.ClientConnectKey | Out-Null
        Invoke-RedisCommand HDEL "STR:STAT" $Seed.ClientConnectKey | Out-Null
    }
    foreach ($connectKey in @($Seed.ExtraClientConnectKeys)) {
        if ($connectKey) {
            Invoke-RedisCommand HDEL "USR:STAT" $connectKey | Out-Null
            Invoke-RedisCommand HDEL "STR:STAT" $connectKey | Out-Null
        }
    }
    Invoke-RedisCommand DEL `
        "ACT:SESSION:$($Seed.Account)" `
        "ACT:REC:$($Seed.Account)" `
        "ACT:UND:$($Seed.SourceUser)" `
        "USR:REC:$($Seed.Account)" `
        "USR:SUB:$($Seed.Account)" `
        "MPT:REC:$($Seed.Mount)" `
        "MPT:SUB:$($Seed.Mount)" `
        "LOG:USR:$($Seed.Account)" `
        "LOG:MPT:$($Seed.Mount)" | Out-Null
}

function Remove-NtripAnonymousAuthSeed {
    param([pscustomobject]$Seed)

    if (-not $Seed) {
        return
    }

    if ($Seed.PSObject.Properties["AnonymousConnectKey"] -and $Seed.AnonymousConnectKey) {
        Invoke-RedisCommand HDEL "USR:STAT" $Seed.AnonymousConnectKey | Out-Null
        Invoke-RedisCommand HDEL "STR:STAT" $Seed.AnonymousConnectKey | Out-Null
    }

    if ($Seed.PSObject.Properties["AnonymousUndKey"] -and $Seed.AnonymousUndKey) {
        Invoke-RedisCommand DEL $Seed.AnonymousUndKey | Out-Null
    }

    if ($Seed.PSObject.Properties["AnonymousUser"] -and $null -ne $Seed.AnonymousUser) {
        Invoke-RedisCommand HDEL "ACT:UNNAMED" $Seed.AnonymousUser | Out-Null
        Invoke-RedisCommand DEL `
            "ACT:SESSION:$($Seed.AnonymousUser)" `
            "ACT:REC:$($Seed.AnonymousUser)" `
            "USR:REC:$($Seed.AnonymousUser)" `
            "USR:SUB:$($Seed.AnonymousUser)" `
            "LOG:USR:$($Seed.AnonymousUser)" | Out-Null
    }

    Remove-NtripAuthSessionSeed $Seed
}

function Read-NtripResponse {
    param(
        [System.Net.Sockets.TcpClient]$Client,
        [int]$TimeoutSec
    )

    $stream = $Client.GetStream()
    $buffer = New-Object byte[] 4096
    $builder = New-Object System.Text.StringBuilder
    $deadline = (Get-Date).AddSeconds($TimeoutSec)

    while ((Get-Date) -lt $deadline) {
        if ($Client.Available -gt 0 -or $stream.DataAvailable) {
            $available = $Client.Available
            if ($available -le 0) {
                $available = $buffer.Length
            }
            $read = $stream.Read($buffer, 0, [Math]::Min($buffer.Length, $available))
            if ($read -le 0) {
                break
            }
            [void]$builder.Append([System.Text.Encoding]::ASCII.GetString($buffer, 0, $read))
            $text = $builder.ToString()
            if ($text -match "`r?`n`r?`n") {
                return $text
            }
        }
        else {
            Start-Sleep -Milliseconds 100
        }
    }

    return $builder.ToString()
}

function Open-NtripTcpConnection {
    param(
        [string]$HostName,
        [int]$Port,
        [string]$RequestText,
        [string]$Label,
        [int]$TimeoutSec,
        [switch]$AllowEmptyResponse
    )

    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $connectResult = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $connectResult.AsyncWaitHandle.WaitOne([TimeSpan]::FromSeconds($TimeoutSec))) {
            Fail "$Label NTRIP TCP connect timed out to $HostName`:$Port"
        }
        $client.EndConnect($connectResult)
        $client.ReceiveTimeout = $TimeoutSec * 1000
        $client.SendTimeout = $TimeoutSec * 1000

        $stream = $client.GetStream()
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($RequestText)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()

        $response = Read-NtripResponse $client $TimeoutSec
        if (-not $AllowEmptyResponse -and [string]::IsNullOrWhiteSpace($response)) {
            Fail "$Label NTRIP login returned an empty response"
        }

        return [pscustomobject]@{
            Label = $Label
            Client = $client
            Response = $response
        }
    }
    catch {
        try { $client.Close() } catch {}
        throw
    }
}

function Close-NtripTcpConnection {
    param([object]$Connection)

    if (-not $Connection) {
        return
    }

    try {
        if ($Connection.Client) {
            $Connection.Client.Close()
            $Connection.Client.Dispose()
        }
    }
    catch {
    }
}

function Assert-NtripResponseOk {
    param(
        [string]$Response,
        [string]$Context
    )

    $firstLine = (($Response -split "`r?`n") | Select-Object -First 1)
    if ($firstLine -match '^HTTP/1\.[01]\s+200\b' -or $firstLine -eq "ICY 200 OK" -or $firstLine -eq "OK") {
        return
    }
    Fail "$Context NTRIP response was not successful: $(Format-DebugText $Response)"
}

function Wait-NtripSourceActive {
    param(
        [pscustomobject]$Seed,
        [int]$TimeoutSec
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $online = Get-RedisHashMap "MPT:LIST"
        $records = Get-RedisHashMap "MPT:REC:$($Seed.Mount)"
        if ($online.ContainsKey($Seed.Mount) -and $records.Count -gt 0) {
            Start-Sleep -Milliseconds 1500
            return ($records.Keys | Select-Object -First 1)
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $onlineRaw = Format-DebugText (Invoke-RedisCommand HGETALL "MPT:LIST")
    $recRaw = Format-DebugText (Invoke-RedisCommand HGETALL "MPT:REC:$($Seed.Mount)")
    Fail "NTRIP source mount did not become active; mpt_list=[$onlineRaw], mpt_rec=[$recRaw]"
}

function Wait-NtripActiveSession {
    param(
        [pscustomobject]$Seed,
        [int]$TimeoutSec
    )

    $key = "ACT:SESSION:$($Seed.Account)"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $sessions = Get-RedisHashMap $key
        foreach ($field in $sessions.Keys) {
            try {
                $record = $sessions[$field] | ConvertFrom-Json
                if ($record.account -eq $Seed.Account -and $record.auth_type -eq "client") {
                    return [pscustomobject]@{
                        Key = $key
                        Field = $field
                        Record = $record
                        Raw = $sessions[$field]
                    }
                }
            }
            catch {
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL $key)
    Fail "NTRIP client active session did not appear in $key; raw=[$raw]"
}

function Get-NtripSessionMap {
    param([pscustomobject]$Seed)

    $key = "ACT:SESSION:$($Seed.Account)"
    $sessions = Get-RedisHashMap $key
    $records = @{}
    foreach ($field in $sessions.Keys) {
        try {
            $record = $sessions[$field] | ConvertFrom-Json
            if ($record.account -eq $Seed.Account -and $record.auth_type -eq "client") {
                $records[$field] = $record
            }
        }
        catch {
        }
    }
    return [pscustomobject]@{
        Key = $key
        Records = $records
    }
}

function Get-NtripActiveSessionField {
    param(
        [pscustomobject]$Seed,
        [string]$Field
    )

    $key = "ACT:SESSION:$($Seed.Account)"
    $raw = Invoke-RedisCommand HGET $key $Field
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }
    try {
        $record = $raw | ConvertFrom-Json
        if ($record.account -eq $Seed.Account -and $record.auth_type -eq "client") {
            return [pscustomobject]@{
                Key = $key
                Field = $Field
                Record = $record
                Raw = $raw
            }
        }
    }
    catch {
    }
    return $null
}

function Wait-NtripOnlineExactFields {
    param(
        [pscustomobject]$Seed,
        [string[]]$ExpectedFields,
        [int]$TimeoutSec,
        [string]$Context
    )

    $expected = @($ExpectedFields | Sort-Object)
    $actRecKey = "ACT:REC:$($Seed.Account)"
    $usrRecKey = "USR:REC:$($Seed.Account)"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $sessionMap = Get-NtripSessionMap $Seed
        $sessionFields = @($sessionMap.Records.Keys | Sort-Object)
        $actRecFields = @((Get-RedisHashMap $actRecKey).Keys | Sort-Object)
        $usrRecFields = @((Get-RedisHashMap $usrRecKey).Keys | Sort-Object)
        if ((Test-FieldSetEquals $sessionFields $expected) -and
            (Test-FieldSetEquals $actRecFields $expected) -and
            (Test-FieldSetEquals $usrRecFields $expected)) {
            return $sessionMap
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $sessionRaw = Format-DebugText (Invoke-RedisCommand HGETALL "ACT:SESSION:$($Seed.Account)")
    $actRecRaw = Format-DebugText (Invoke-RedisCommand HGETALL $actRecKey)
    $usrRecRaw = Format-DebugText (Invoke-RedisCommand HGETALL $usrRecKey)
    Fail "$Context expected online fields [$($expected -join ',')] but got ACT:SESSION=[$sessionRaw] ACT:REC=[$actRecRaw] USR:REC=[$usrRecRaw]"
}

function Wait-NtripOnlineFieldPresent {
    param(
        [pscustomobject]$Seed,
        [string]$Field,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $sessionKey = "ACT:SESSION:$($Seed.Account)"
    $actRecKey = "ACT:REC:$($Seed.Account)"
    $usrRecKey = "USR:REC:$($Seed.Account)"
    do {
        $session = Get-NtripActiveSessionField $Seed $Field
        $actRec = Invoke-RedisCommand HGET $actRecKey $Field
        $usrRec = Invoke-RedisCommand HGET $usrRecKey $Field
        $sessionTtl = Get-RedisHashFieldTtl $sessionKey $Field
        $actRecTtl = Get-RedisHashFieldTtl $actRecKey $Field
        $usrRecTtl = Get-RedisHashFieldTtl $usrRecKey $Field
        if ($session -and
            -not [string]::IsNullOrEmpty($actRec) -and
            -not [string]::IsNullOrEmpty($usrRec) -and
            $sessionTtl -gt 0 -and
            $actRecTtl -gt 0 -and
            $usrRecTtl -gt 0) {
            return [pscustomobject]@{
                Session = $session
                ActRec = $actRec
                UsrRec = $usrRec
                SessionTtl = $sessionTtl
                ActRecTtl = $actRecTtl
                UsrRecTtl = $usrRecTtl
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $sessionRaw = Format-DebugText (Invoke-RedisCommand HGET $sessionKey $Field)
    $actRecRaw = Format-DebugText (Invoke-RedisCommand HGET $actRecKey $Field)
    $usrRecRaw = Format-DebugText (Invoke-RedisCommand HGET $usrRecKey $Field)
    $sessionTtl = Get-RedisHashFieldTtl $sessionKey $Field
    $actRecTtl = Get-RedisHashFieldTtl $actRecKey $Field
    $usrRecTtl = Get-RedisHashFieldTtl $usrRecKey $Field
    Fail "$Context expected field $Field in ACT:SESSION/ACT:REC/USR:REC with live field TTLs but got session=[$sessionRaw] ttl=$sessionTtl act_rec=[$actRecRaw] ttl=$actRecTtl usr_rec=[$usrRecRaw] ttl=$usrRecTtl"
}

function Wait-NtripOnlineFieldGone {
    param(
        [pscustomobject]$Seed,
        [string]$Field,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $session = Invoke-RedisCommand HGET "ACT:SESSION:$($Seed.Account)" $Field
        $actRec = Invoke-RedisCommand HGET "ACT:REC:$($Seed.Account)" $Field
        $usrRec = Invoke-RedisCommand HGET "USR:REC:$($Seed.Account)" $Field
        if ([string]::IsNullOrEmpty($session) -and [string]::IsNullOrEmpty($actRec) -and [string]::IsNullOrEmpty($usrRec)) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $sessionRaw = Format-DebugText (Invoke-RedisCommand HGET "ACT:SESSION:$($Seed.Account)" $Field)
    $actRecRaw = Format-DebugText (Invoke-RedisCommand HGET "ACT:REC:$($Seed.Account)" $Field)
    $usrRecRaw = Format-DebugText (Invoke-RedisCommand HGET "USR:REC:$($Seed.Account)" $Field)
    Fail "$Context expected field $Field removed from ACT:SESSION/ACT:REC/USR:REC but got session=[$sessionRaw] act_rec=[$actRecRaw] usr_rec=[$usrRecRaw]"
}

function Wait-NtripSessionUpdateTimeAdvanced {
    param(
        [pscustomobject]$Seed,
        [string]$Field,
        [long]$InitialUpdateTime,
        [int]$TimeoutSec
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $session = Get-NtripActiveSessionField $Seed $Field
        if ($session) {
            $current = [long]$session.Record.update_time
            if ($current -gt $InitialUpdateTime) {
                return $session
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGET "ACT:SESSION:$($Seed.Account)" $Field)
    Fail "NTRIP active session update_time did not advance beyond $InitialUpdateTime for field $Field; raw=[$raw]"
}

function Wait-NtripActiveSessionGone {
    param(
        [string]$Key,
        [string]$Field,
        [int]$TimeoutSec
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $value = Invoke-RedisCommand HGET $Key $Field
        if ([string]::IsNullOrEmpty($value)) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL $Key)
    Fail "NTRIP client active session field was not removed after disconnect; key=$Key field=$Field raw=[$raw]"
}

function Test-NtripTcpConnectionClosed {
    param([object]$Connection)

    if (-not $Connection -or -not $Connection.Client) {
        return $true
    }
    $client = $Connection.Client
    if (-not $client.Connected) {
        return $true
    }
    try {
        if ($client.Client.Poll(0, [System.Net.Sockets.SelectMode]::SelectRead) -and $client.Available -eq 0) {
            return $true
        }
        $stream = $client.GetStream()
        if ($client.Available -gt 0 -or $stream.DataAvailable) {
            $buffer = New-Object byte[] 1
            $read = $stream.Read($buffer, 0, 1)
            return ($read -le 0)
        }
    }
    catch {
        return $true
    }
    return $false
}

function Wait-NtripTcpConnectionClosed {
    param(
        [object]$Connection,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        if (Test-NtripTcpConnectionClosed $Connection) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    Fail "$Context TCP connection did not close before timeout"
}

function Assert-NtripActiveAccountPayload {
    param(
        [object]$Payload,
        [pscustomobject]$Seed,
        [object]$Session,
        [string]$Label
    )

    $record = Require-JsonProperty $Payload $Session.Field "$Label active account payload"
    if ($record.account -ne $Seed.Account -or $record.auth_type -ne "client") {
        Fail "$Label NTRIP active account record mismatch"
    }
    if ($record.group_uid -ne $Seed.GroupUid) {
        Fail "$Label NTRIP active account group mismatch: $($record.group_uid)"
    }
    Assert-NoPasswordMaterial $record "$Label NTRIP active account"
}

function Assert-NtripOnlyActiveAccountPayload {
    param(
        [object]$Payload,
        [pscustomobject]$Seed,
        [object]$ExpectedSession,
        [string]$UnexpectedField,
        [string]$Label
    )

    Assert-NtripActiveAccountPayload $Payload $Seed $ExpectedSession $Label
    if (-not [string]::IsNullOrEmpty($UnexpectedField)) {
        $unexpected = Get-JsonProperty $Payload $UnexpectedField
        if ($null -ne $unexpected) {
            Fail "$Label active account payload still exposed evicted field $UnexpectedField"
        }
    }
}

function Copy-E2eServiceConfig {
    param(
        [string]$SourceConfDir,
        [string]$DestinationConfDir,
        [int]$HttpPort,
        [int]$NtripPort,
        [string]$HttpBindAddr,
        [string]$AdminUser,
        [string]$AdminPassword,
        [string]$RedisHost,
        [int]$RedisPort,
        [string]$RedisPassword,
        [bool]$RoverOnlineProtection,
        [bool]$RoverAnonymousLogin
    )

    New-Item -ItemType Directory -Path $DestinationConfDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $SourceConfDir "Service_Setting.yml") -Destination $DestinationConfDir -Force
    Copy-Item -LiteralPath (Join-Path $SourceConfDir "Caster_Core.yml") -Destination $DestinationConfDir -Force
    Copy-Item -LiteralPath (Join-Path $SourceConfDir "Auth_Verify.yml") -Destination $DestinationConfDir -Force

    $servicePath = Join-Path $DestinationConfDir "Service_Setting.yml"
    $serviceText = Get-Content -LiteralPath $servicePath -Raw
    $serviceText = Set-YamlValueInSection $serviceText "Ntrip_Listener_Setting" "Listen_Port" ([string]$NtripPort)
    $serviceText = Set-YamlValueInSection $serviceText "Ntrip_Listener_Setting" "Enable_Server_Login" "true"
    $serviceText = Set-YamlValueInSection $serviceText "Ntrip_Listener_Setting" "Enable_Client_Login" "true"
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Port" ([string]$HttpPort)
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Bind_Addr" "`"$HttpBindAddr`""
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Force_Enable" "true"
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Admin_User" "`"$AdminUser`""
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Admin_Password" "`"$AdminPassword`""
    Write-TextFile $servicePath $serviceText

    $corePath = Join-Path $DestinationConfDir "Caster_Core.yml"
    $coreText = Get-Content -LiteralPath $corePath -Raw
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "IP" $RedisHost
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "Port" ([string]$RedisPort)
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    $coreText = Set-YamlValueInSection $coreText "Caster_Setting" "Update_Intv" "1"
    $coreText = Set-YamlValueInSection $coreText "Caster_Setting" "Key_Expire_Time" "10"
    $coreText = Set-YamlValueInSection $coreText "Rover_Setting" "Enable_Mult" "true"
    $coreText = Set-YamlValueInSection $coreText "Rover_Setting" "Keep_Early" "false"
    Write-TextFile $corePath $coreText

    $authPath = Join-Path $DestinationConfDir "Auth_Verify.yml"
    $authText = Get-Content -LiteralPath $authPath -Raw
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "IP" $RedisHost
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Port" ([string]$RedisPort)
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    $authText = Set-YamlValueInSection $authText "Base_Setting" "Anonymous_Login" "true"
    $authText = Set-YamlValueInSection $authText "Rover_Setting" "Anonymous_Login" ($RoverAnonymousLogin.ToString().ToLowerInvariant())
    $authText = Set-YamlValueInSection $authText "Rover_Setting" "Online_Protection" ($RoverOnlineProtection.ToString().ToLowerInvariant())
    $authText = Set-YamlValueInSection $authText "Source_Setting" "Anonymous_Login" "true"
    Write-TextFile $authPath $authText
}

function Start-E2eCasterServiceNode {
    param(
        [string]$Label,
        [string]$ServiceExe,
        [string]$ReleaseDir,
        [string]$ConfDir,
        [string]$Base,
        [int]$TimeoutSec
    )

    $stdoutPath = Join-Path $env:TEMP ("navcaster-e2e-$Label-" + [guid]::NewGuid().ToString() + ".out.log")
    $stderrPath = Join-Path $env:TEMP ("navcaster-e2e-$Label-" + [guid]::NewGuid().ToString() + ".err.log")
    $confPath = $ConfDir
    if (-not $confPath.EndsWith([System.IO.Path]::DirectorySeparatorChar) -and -not $confPath.EndsWith([System.IO.Path]::AltDirectorySeparatorChar)) {
        $confPath = $confPath + [System.IO.Path]::DirectorySeparatorChar
    }

    Say "starting CasterService $Label and waiting for $Base/api/status/health"
    $process = Start-Process -FilePath $ServiceExe `
        -WorkingDirectory $ReleaseDir `
        -WindowStyle Hidden `
        -ArgumentList @("-conf", $confPath) `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru

    $healthOk = $false
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 800
        if ($process.HasExited) {
            break
        }
        try {
            $health = Invoke-RestMethod -Uri "$Base/api/status/health" -TimeoutSec 3
            if ($health.status -eq "ok") {
                $healthOk = $true
                break
            }
        }
        catch {
        }
    } while ((Get-Date) -lt $deadline)

    if (-not $healthOk) {
        $exitText = if ($process.HasExited) { "exited=$($process.ExitCode)" } else { "still-running" }
        Fail "$Label health endpoint did not become ready: $exitText"
    }

    return [pscustomobject]@{
        Label = $Label
        Process = $process
        Stdout = $stdoutPath
        Stderr = $stderrPath
        Base = $Base
        ConfDir = $ConfDir
    }
}

function Stop-E2eCasterServiceNode {
    param([object]$Node)

    if (-not $Node -or -not $Node.Process) {
        return
    }

    try {
        if (-not $Node.Process.HasExited) {
            Stop-Process -Id $Node.Process.Id -Force
            Start-Sleep -Milliseconds 500
        }
    }
    catch {
        Write-Warning "failed to stop CasterService $($Node.Label): $($_.Exception.Message)"
    }
}

function Open-NtripSourceForSeed {
    param(
        [pscustomobject]$Seed,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Label = "source"
    )

    $sourceAuth = ConvertTo-BasicAuthValue $Seed.SourceUser $Seed.SourcePassword
    $sourceRequest = "POST /$($Seed.Mount) HTTP/1.1`r`n" +
        "Host: ${NtripHost}:$NtripPort`r`n" +
        "Ntrip-Version: Ntrip/2.0`r`n" +
        "Authorization: Basic $sourceAuth`r`n" +
        "User-Agent: NTRIP NavCasterE2E/$($Seed.Prefix)`r`n" +
        "Connection: close`r`n`r`n"

    Say "opening NTRIP source mount=$($Seed.Mount)"
    $connection = Open-NtripTcpConnection $NtripHost $NtripPort $sourceRequest $Label 10
    Assert-NtripResponseOk $connection.Response $Label
    $Seed.SourceConnectKey = Wait-NtripSourceActive $Seed $StartupTimeoutSec
    return $connection
}

function Open-NtripClientForSeed {
    param(
        [pscustomobject]$Seed,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Label = "client",
        [switch]$AllowRejected
    )

    $clientAuth = ConvertTo-BasicAuthValue $Seed.Account $Seed.Password
    $clientRequest = "GET /$($Seed.Mount) HTTP/1.1`r`n" +
        "Host: ${NtripHost}:$NtripPort`r`n" +
        "Ntrip-Version: Ntrip/2.0`r`n" +
        "Authorization: Basic $clientAuth`r`n" +
        "User-Agent: NTRIP NavCasterE2E/$($Seed.Prefix)`r`n" +
        "Connection: close`r`n`r`n"

    Say "opening NTRIP client account=$($Seed.Account) label=$Label"
    $connection = Open-NtripTcpConnection $NtripHost $NtripPort $clientRequest $Label 10 -AllowEmptyResponse:$AllowRejected
    if (-not $AllowRejected) {
        Assert-NtripResponseOk $connection.Response $Label
    }
    elseif (-not [string]::IsNullOrWhiteSpace($connection.Response)) {
        $firstLine = (($connection.Response -split "`r?`n") | Select-Object -First 1)
        if ($firstLine -match '^HTTP/1\.[01]\s+200\b' -or $firstLine -eq "ICY 200 OK" -or $firstLine -eq "OK") {
            Fail "$Label unexpectedly received a successful NTRIP response while rejection was expected"
        }
    }
    return $connection
}

function New-NtripAnonymousAuthSeed {
    param([string]$Label)

    $seed = New-NtripAuthSessionSeed -Label $Label
    Add-Member -InputObject $seed -MemberType NoteProperty -Name AnonymousUser -Value ""
    Add-Member -InputObject $seed -MemberType NoteProperty -Name AnonymousConnectKey -Value ""
    Add-Member -InputObject $seed -MemberType NoteProperty -Name AnonymousUndKey -Value ""
    return $seed
}

function Open-NtripAnonymousClientForSeed {
    param(
        [pscustomobject]$Seed,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Label,
        [switch]$AllowRejected
    )

    $clientRequest = "GET /$($Seed.Mount) HTTP/1.1`r`n" +
        "Host: ${NtripHost}:$NtripPort`r`n" +
        "Ntrip-Version: Ntrip/2.0`r`n" +
        "User-Agent: NTRIP NavCasterE2E/$($Seed.Prefix)`r`n" +
        "Connection: close`r`n`r`n"

    Say "opening anonymous NTRIP client label=$Label mount=$($Seed.Mount)"
    $connection = Open-NtripTcpConnection $NtripHost $NtripPort $clientRequest $Label 10 -AllowEmptyResponse:$AllowRejected
    if (-not $AllowRejected) {
        Assert-NtripResponseOk $connection.Response $Label
    }
    elseif (-not [string]::IsNullOrWhiteSpace($connection.Response)) {
        $firstLine = (($connection.Response -split "`r?`n") | Select-Object -First 1)
        if ($firstLine -match '^HTTP/1\.[01]\s+200\b' -or $firstLine -eq "ICY 200 OK" -or $firstLine -eq "OK") {
            Fail "$Label unexpectedly received a successful NTRIP response while anonymous rejection was expected"
        }
    }
    return $connection
}

function Get-NtripAnonymousClientRecords {
    param([pscustomobject]$Seed)

    $records = @()
    $sourceUndKey = "ACT:UND:$($Seed.SourceUser)"
    $undPrefix = "ACT:UND:"
    foreach ($key in (Get-RedisKeys "ACT:UND:*")) {
        $map = Get-RedisHashMap $key
        foreach ($field in $map.Keys) {
            if ($key -eq $sourceUndKey -and $field -eq $Seed.SourceConnectKey) {
                continue
            }
            $user = $key
            if ($key.StartsWith($undPrefix)) {
                $user = $key.Substring($undPrefix.Length)
            }
            $records += [pscustomobject]@{
                Key = $key
                User = $user
                Field = $field
                Value = $map[$field]
                Ttl = Get-RedisHashFieldTtl $key $field
            }
        }
    }
    return @($records)
}

function Format-NtripAnonymousRecords {
    param([pscustomobject]$Seed)

    $parts = @()
    foreach ($record in (Get-NtripAnonymousClientRecords $Seed)) {
        $parts += "$($record.Key)/$($record.Field)/ttl=$($record.Ttl)/value=$($record.Value)"
    }
    return ($parts -join "; ")
}

function Wait-NtripAnonymousClientRecord {
    param(
        [pscustomobject]$Seed,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $records = @(Get-NtripAnonymousClientRecords $Seed | Where-Object { $_.Ttl -gt 0 })
        if ($records.Count -eq 1) {
            return $records[0]
        }
        if ($records.Count -gt 1) {
            Fail "$Context found multiple anonymous client records: $(Format-DebugText (Format-NtripAnonymousRecords $Seed))"
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    Fail "$Context anonymous ACT:UND client record did not appear; records=[$(Format-DebugText (Format-NtripAnonymousRecords $Seed))]"
}

function Wait-NtripAnonymousClientRecordGone {
    param(
        [pscustomobject]$Seed,
        [string]$ConnectKey,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $records = @(Get-NtripAnonymousClientRecords $Seed | Where-Object { $_.Field -eq $ConnectKey })
        if ($records.Count -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    Fail "$Context anonymous ACT:UND field was not removed; records=[$(Format-DebugText (Format-NtripAnonymousRecords $Seed))]"
}

function Assert-NtripNoAnonymousActiveDisplay {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [object]$AnonymousRecord,
        [string]$Context
    )

    foreach ($key in (Get-RedisKeys "ACT:SESSION:*")) {
        $sessions = Get-RedisHashMap $key
        foreach ($field in $sessions.Keys) {
            if ($field -eq $AnonymousRecord.Field) {
                Fail "$Context unexpectedly wrote anonymous client to $key field=$field"
            }
            try {
                $session = $sessions[$field] | ConvertFrom-Json
                if ($session.connect_key -eq $AnonymousRecord.Field) {
                    Fail "$Context unexpectedly exposed anonymous connect_key in $key"
                }
                if (-not [string]::IsNullOrEmpty($AnonymousRecord.User) -and $session.account -eq $AnonymousRecord.User) {
                    Fail "$Context unexpectedly exposed anonymous user in $key"
                }
            }
            catch {
            }
        }
    }

    $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
    foreach ($prop in @($rest.PSObject.Properties)) {
        if ($prop.Name -eq $AnonymousRecord.Field) {
            Fail "$Context REST /api/accounts/active exposed anonymous field $($AnonymousRecord.Field)"
        }
        $value = $prop.Value
        if ($null -ne $value) {
            if ($value.connect_key -eq $AnonymousRecord.Field) {
                Fail "$Context REST /api/accounts/active exposed anonymous connect_key"
            }
            if (-not [string]::IsNullOrEmpty($AnonymousRecord.User) -and $value.account -eq $AnonymousRecord.User) {
                Fail "$Context REST /api/accounts/active exposed anonymous user"
            }
            Assert-NoPasswordMaterial $value "$Context REST /api/accounts/active"
        }
    }
}

function Assert-NtripNoAnonymousAuthResidue {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [pscustomobject]$Seed,
        [string]$Context
    )

    $records = @(Get-NtripAnonymousClientRecords $Seed)
    if ($records.Count -ne 0) {
        Fail "$Context left anonymous ACT:UND records: $(Format-DebugText (Format-NtripAnonymousRecords $Seed))"
    }

    foreach ($pattern in @("ACT:SESSION:*", "ACT:REC:*", "USR:REC:*")) {
        $keys = @(Get-RedisKeys $pattern)
        if ($keys.Count -ne 0) {
            Fail "$Context left unexpected auth/core record keys for rejected anonymous client; pattern=$pattern keys=[$($keys -join ',')]"
        }
    }

    $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
    $fields = @($rest.PSObject.Properties | ForEach-Object { $_.Name })
    if ($fields.Count -ne 0) {
        Fail "$Context REST /api/accounts/active was not empty after rejected anonymous client; fields=[$($fields -join ',')]"
    }
}

function Invoke-NtripAuthSessionSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NtripHost,
        [int]$NtripPort
    )

    $script:ntripAuthSeed = New-NtripAuthSessionSeed
    Add-NtripAuthAccountSeed $script:ntripAuthSeed

    $script:ntripSourceConnection = Open-NtripSourceForSeed $script:ntripAuthSeed $NtripHost $NtripPort
    $script:ntripClientConnection = Open-NtripClientForSeed $script:ntripAuthSeed $NtripHost $NtripPort

    $session = Wait-NtripActiveSession $script:ntripAuthSeed $StartupTimeoutSec
    $script:ntripAuthSeed.ClientConnectKey = $session.Field
    if ($session.Record.group_uid -ne $script:ntripAuthSeed.GroupUid) {
        Fail "NTRIP active session group mismatch: $($session.Record.group_uid)"
    }

    Say "validating /api/accounts/active against real NTRIP Auth session"
    $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
    Assert-NtripActiveAccountPayload $rest $script:ntripAuthSeed $session "REST /api/accounts/active"

    Close-NtripTcpConnection $script:ntripClientConnection
    $script:ntripClientConnection = $null
    Wait-NtripActiveSessionGone $session.Key $session.Field $StartupTimeoutSec

    Close-NtripTcpConnection $script:ntripSourceConnection
    $script:ntripSourceConnection = $null
    Remove-NtripAuthSessionSeed $script:ntripAuthSeed
    $script:ntripAuthSeed = $null
    Say "PASS NTRIP Auth active session smoke"
}

function Invoke-NtripAuthSessionRenewalSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NtripHost,
        [int]$NtripPort,
        [int]$RenewalWaitSec
    )

    $sourceConnection = $null
    $clientConnection = $null
    $seed = $null

    try {
        $seed = New-NtripAuthSessionSeed -Label "nc019_renewal"
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed

        $sourceConnection = Open-NtripSourceForSeed $seed $NtripHost $NtripPort "source-renewal"
        $script:ntripSourceConnection = $sourceConnection

        $clientConnection = Open-NtripClientForSeed $seed $NtripHost $NtripPort "client-renewal"
        $script:ntripClientConnection = $clientConnection

        $initialSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $initialSession.Field
        $initialOnline = Wait-NtripOnlineFieldPresent $seed $initialSession.Field $StartupTimeoutSec "renewal initial login"
        $initialUpdateTime = [long]$initialSession.Record.update_time
        Say "waiting ${RenewalWaitSec}s for NTRIP Auth active session renewal field=$($initialSession.Field) ttl=$($initialOnline.SessionTtl)/$($initialOnline.ActRecTtl)/$($initialOnline.UsrRecTtl)"
        Start-Sleep -Seconds $RenewalWaitSec

        $renewed = Wait-NtripSessionUpdateTimeAdvanced $seed $initialSession.Field $initialUpdateTime $StartupTimeoutSec
        Wait-NtripOnlineExactFields $seed @($initialSession.Field) $StartupTimeoutSec "renewal final exact fields" | Out-Null
        $finalOnline = Wait-NtripOnlineFieldPresent $seed $initialSession.Field $StartupTimeoutSec "renewal final online state"
        if ([long]$renewed.Record.online_time -ne [long]$initialSession.Record.online_time) {
            Fail "NTRIP active session online_time changed during renewal: initial=$($initialSession.Record.online_time) renewed=$($renewed.Record.online_time)"
        }
        Say "NTRIP Auth active session renewed update_time=$($initialUpdateTime)->$($renewed.Record.update_time) ttl=$($finalOnline.SessionTtl)/$($finalOnline.ActRecTtl)/$($finalOnline.UsrRecTtl)"

        $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
        Assert-NtripActiveAccountPayload $rest $seed $renewed "REST /api/accounts/active renewal"

        Close-NtripTcpConnection $clientConnection
        $clientConnection = $null
        Wait-NtripOnlineFieldGone $seed $initialSession.Field $StartupTimeoutSec "renewal client disconnect cleanup"

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        Remove-NtripAuthSessionSeed $seed
        $seed = $null
        $script:ntripAuthSeed = $null
        $script:ntripSourceConnection = $null
        $script:ntripClientConnection = $null
        Say "PASS NTRIP Auth active session renewal smoke"
    }
    finally {
        Close-NtripTcpConnection $clientConnection
        Close-NtripTcpConnection $sourceConnection
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
    }
}

function Invoke-NtripOnlineProtectionSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Scenario
    )

    $sourceConnection = $null
    $firstClient = $null
    $secondClient = $null
    $seed = $null

    try {
        $label = if ($Scenario -eq "RejectNew") { "nc018_reject" } else { "nc018_kick" }
        $seed = New-NtripAuthSessionSeed -Label $label -ConnectionLimit 1
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed

        $sourceConnection = Open-NtripSourceForSeed $seed $NtripHost $NtripPort "source-$Scenario"
        $script:ntripSourceConnection = $sourceConnection

        $firstClient = Open-NtripClientForSeed $seed $NtripHost $NtripPort "client-1-$Scenario"
        $script:ntripClientConnection = $firstClient
        $firstSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $firstSession.Field
        Wait-NtripOnlineExactFields $seed @($firstSession.Field) $StartupTimeoutSec "$Scenario first login" | Out-Null

        $secondClient = Open-NtripClientForSeed $seed $NtripHost $NtripPort "client-2-$Scenario" -AllowRejected:($Scenario -eq "RejectNew")
        $secondObserved = $false
        $secondSession = $null

        $deadline = (Get-Date).AddSeconds($StartupTimeoutSec)
        do {
            $sessionMap = Get-NtripSessionMap $seed
            $fields = @($sessionMap.Records.Keys)
            if ($Scenario -eq "RejectNew") {
                if ($fields.Count -eq 1 -and $fields[0] -eq $firstSession.Field) {
                    $secondObserved = $true
                    break
                }
            }
            else {
                if ($fields.Count -eq 1 -and $fields[0] -ne $firstSession.Field) {
                    $secondSession = [pscustomobject]@{
                        Key = $sessionMap.Key
                        Field = $fields[0]
                        Record = $sessionMap.Records[$fields[0]]
                    }
                    $seed.ExtraClientConnectKeys += $secondSession.Field
                    $secondObserved = $true
                    break
                }
            }
            Start-Sleep -Milliseconds 500
        } while ((Get-Date) -lt $deadline)

        if (-not $secondObserved) {
            $raw = Format-DebugText (Invoke-RedisCommand HGETALL "ACT:SESSION:$($seed.Account)")
            Fail "$Scenario Online_Protection session state did not reach expected result; raw=[$raw]"
        }

        if ($Scenario -eq "RejectNew") {
            Wait-NtripTcpConnectionClosed $secondClient 10 "$Scenario rejected second client"
            Wait-NtripOnlineExactFields $seed @($firstSession.Field) $StartupTimeoutSec "$Scenario rejected second client final state" | Out-Null
            $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
            Assert-NtripActiveAccountPayload $rest $seed $firstSession "REST /api/accounts/active $Scenario"
        }
        else {
            Wait-NtripTcpConnectionClosed $firstClient 10 "$Scenario evicted first client"
            Wait-NtripOnlineExactFields $seed @($secondSession.Field) $StartupTimeoutSec "$Scenario evicted first client final state" | Out-Null
            $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
            Assert-NtripActiveAccountPayload $rest $seed $secondSession "REST /api/accounts/active $Scenario"
        }

        Close-NtripTcpConnection $secondClient
        $secondClient = $null
        if ($Scenario -eq "KickOld" -and $secondSession) {
            Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "$Scenario second client disconnect cleanup" | Out-Null
        }

        Close-NtripTcpConnection $firstClient
        $firstClient = $null
        if ($Scenario -eq "RejectNew") {
            Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "$Scenario first client disconnect cleanup" | Out-Null
        }

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        Remove-NtripAuthSessionSeed $seed
        $seed = $null
        $script:ntripAuthSeed = $null
        $script:ntripSourceConnection = $null
        $script:ntripClientConnection = $null
        Say "PASS NTRIP Online_Protection $Scenario smoke"
    }
    finally {
        Close-NtripTcpConnection $secondClient
        Close-NtripTcpConnection $firstClient
        Close-NtripTcpConnection $sourceConnection
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
    }
}

function Invoke-NtripAnonymousAuthSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Scenario
    )

    $sourceConnection = $null
    $anonymousClient = $null
    $seed = $null

    try {
        $label = if ($Scenario -eq "AllowAnonymous") { "nc021_anon_allow" } else { "nc021_anon_reject" }
        $seed = New-NtripAnonymousAuthSeed -Label $label
        $script:ntripAuthSeed = $seed

        $sourceConnection = Open-NtripSourceForSeed $seed $NtripHost $NtripPort "source-$Scenario"
        $script:ntripSourceConnection = $sourceConnection

        if ($Scenario -eq "AllowAnonymous") {
            $anonymousClient = Open-NtripAnonymousClientForSeed $seed $NtripHost $NtripPort "anonymous-client-$Scenario"
            $script:ntripClientConnection = $anonymousClient
            $anonymousRecord = Wait-NtripAnonymousClientRecord $seed $StartupTimeoutSec "$Scenario anonymous client login"
            $seed.AnonymousUser = $anonymousRecord.User
            $seed.AnonymousConnectKey = $anonymousRecord.Field
            $seed.AnonymousUndKey = $anonymousRecord.Key

            Say "anonymous NTRIP client accepted user=[$($anonymousRecord.User)] field=$($anonymousRecord.Field) ttl=$($anonymousRecord.Ttl)"
            Assert-NtripNoAnonymousActiveDisplay $Base $Headers $anonymousRecord "$Scenario anonymous client"

            Close-NtripTcpConnection $anonymousClient
            $anonymousClient = $null
            $script:ntripClientConnection = $null
            Wait-NtripAnonymousClientRecordGone $seed $seed.AnonymousConnectKey $StartupTimeoutSec "$Scenario anonymous client disconnect cleanup"
        }
        else {
            $anonymousClient = Open-NtripAnonymousClientForSeed $seed $NtripHost $NtripPort "anonymous-client-$Scenario" -AllowRejected
            $script:ntripClientConnection = $anonymousClient
            Wait-NtripTcpConnectionClosed $anonymousClient 10 "$Scenario anonymous client"
            Assert-NtripNoAnonymousAuthResidue $Base $Headers $seed "$Scenario anonymous client"
            Close-NtripTcpConnection $anonymousClient
            $anonymousClient = $null
            $script:ntripClientConnection = $null
        }

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        Remove-NtripAnonymousAuthSeed $seed
        $seed = $null
        $script:ntripAuthSeed = $null
        $script:ntripSourceConnection = $null
        Say "PASS NTRIP anonymous auth $Scenario smoke"
    }
    finally {
        Close-NtripTcpConnection $anonymousClient
        Close-NtripTcpConnection $sourceConnection
        if ($seed) {
            Remove-NtripAnonymousAuthSeed $seed
        }
    }
}

function Invoke-NtripAuthBroadcastSmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [string]$NtripHost,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null
    $sourceConnection = $null
    $firstClient = $null
    $secondClient = $null
    $seed = $null

    try {
        $seed = New-NtripAuthSessionSeed -Label "nc022_broadcast" -ConnectionLimit 1
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc022-" + [guid]::NewGuid().ToString())
        $secondaryConfDir = Join-Path $secondaryConfRoot "conf"
        Copy-E2eServiceConfig `
            -SourceConfDir $confDir `
            -DestinationConfDir $secondaryConfDir `
            -HttpPort $SecondaryHttpPort `
            -NtripPort $SecondaryNtripPort `
            -HttpBindAddr $HttpBindAddr `
            -AdminUser $AdminUser `
            -AdminPassword $AdminPassword `
            -RedisHost $RedisHost `
            -RedisPort $RedisPort `
            -RedisPassword $RedisPassword `
            -RoverOnlineProtection $false `
            -RoverAnonymousLogin $false

        $secondaryBase = "http://${HttpBindAddr}:${SecondaryHttpPort}"
        $secondaryNode = Start-E2eCasterServiceNode "nc022-secondary" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec

        Say "logging in to secondary CasterService as $AdminUser"
        $loginBody = @{ username = $AdminUser; password = $AdminPassword } | ConvertTo-Json -Compress
        $secondaryLogin = Invoke-RestMethod -Method Post -ContentType "application/json" -Body $loginBody -Uri "$secondaryBase/api/auth/login" -TimeoutSec 10
        if (-not $secondaryLogin.token) {
            Fail "secondary login did not return token"
        }
        $secondaryHeaders = @{ Authorization = "Bearer $($secondaryLogin.token)" }
        $secondaryStatus = Invoke-RestMethod -Headers $secondaryHeaders -Uri "$secondaryBase/api/status" -TimeoutSec 10
        if ($secondaryStatus.redis_auth_connected -ne $true -or $secondaryStatus.redis_caster_connected -ne $true) {
            Fail "secondary status reports Redis disconnected: caster=$($secondaryStatus.redis_caster_connected) auth=$($secondaryStatus.redis_auth_connected)"
        }

        $sourceConnection = Open-NtripSourceForSeed $seed $NtripHost $PrimaryNtripPort "source-broadcast"
        $script:ntripSourceConnection = $sourceConnection

        $firstClient = Open-NtripClientForSeed $seed $NtripHost $PrimaryNtripPort "client-node-a"
        $script:ntripClientConnection = $firstClient
        $firstSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $firstSession.Field
        Wait-NtripOnlineExactFields $seed @($firstSession.Field) $StartupTimeoutSec "broadcast node A first login" | Out-Null

        $secondClient = Open-NtripClientForSeed $seed $NtripHost $SecondaryNtripPort "client-node-b"
        $secondSession = $null
        $deadline = (Get-Date).AddSeconds($StartupTimeoutSec)
        do {
            $sessionMap = Get-NtripSessionMap $seed
            $fields = @($sessionMap.Records.Keys)
            if ($fields.Count -eq 1 -and $fields[0] -ne $firstSession.Field) {
                $secondSession = [pscustomobject]@{
                    Key = $sessionMap.Key
                    Field = $fields[0]
                    Record = $sessionMap.Records[$fields[0]]
                }
                $seed.ExtraClientConnectKeys += $secondSession.Field
                break
            }
            Start-Sleep -Milliseconds 500
        } while ((Get-Date) -lt $deadline)

        if (-not $secondSession) {
            $raw = Format-DebugText (Invoke-RedisCommand HGETALL "ACT:SESSION:$($seed.Account)")
            Fail "broadcast session state did not converge to the secondary client; raw=[$raw]"
        }

        Wait-NtripTcpConnectionClosed $firstClient 10 "broadcast evicted node A client"
        Wait-NtripOnlineExactFields $seed @($secondSession.Field) $StartupTimeoutSec "broadcast node B final exact fields" | Out-Null
        Wait-NtripOnlineFieldGone $seed $firstSession.Field $StartupTimeoutSec "broadcast node A field cleanup"

        $primaryRest = Invoke-RestMethod -Headers $PrimaryHeaders -Uri "$PrimaryBase/api/accounts/active" -TimeoutSec 10
        Assert-NtripOnlyActiveAccountPayload $primaryRest $seed $secondSession $firstSession.Field "primary REST /api/accounts/active broadcast"

        $secondaryRest = Invoke-RestMethod -Headers $secondaryHeaders -Uri "$secondaryBase/api/accounts/active" -TimeoutSec 10
        Assert-NtripOnlyActiveAccountPayload $secondaryRest $seed $secondSession $firstSession.Field "secondary REST /api/accounts/active broadcast"

        Start-Sleep -Seconds 3
        if (Get-NtripActiveSessionField $seed $firstSession.Field) {
            Fail "broadcast old node A field was rewritten after eviction: $($firstSession.Field)"
        }
        Wait-NtripOnlineExactFields $seed @($secondSession.Field) $StartupTimeoutSec "broadcast post-renewal exact fields" | Out-Null

        Close-NtripTcpConnection $secondClient
        $secondClient = $null
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "broadcast node B disconnect cleanup" | Out-Null

        Close-NtripTcpConnection $firstClient
        $firstClient = $null
        $script:ntripClientConnection = $null

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        $script:ntripSourceConnection = $null

        Remove-NtripAuthSessionSeed $seed
        $seed = $null
        $script:ntripAuthSeed = $null

        Stop-E2eCasterServiceNode $secondaryNode
        $secondaryNode = $null
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            $secondaryConfRoot = $null
        }

        Say "PASS NTRIP Auth Broadcast cross-instance smoke"
    }
    finally {
        Close-NtripTcpConnection $secondClient
        Close-NtripTcpConnection $firstClient
        Close-NtripTcpConnection $sourceConnection
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove secondary service config: $($_.Exception.Message)"
            }
        }
    }
}

function New-ActiveSessionRecord {
    param(
        [string]$Account,
        [string]$ConnectKey,
        [string]$GroupUid,
        [string]$Marker,
        [long]$OnlineTime = 1710000000,
        [long]$UpdateTime = 1710000010,
        [switch]$IncludePasswordMaterial
    )

    $record = [ordered]@{
        uid = $ConnectKey
        connect_key = $ConnectKey
        account = $Account
        anonymous = $false
        auth_type = "client"
        online_time = $OnlineTime
        update_time = $UpdateTime
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

function New-ActiveAccountSseDeltaSeed {
    $prefix = "nc020-$PID-$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
    $account = "$prefix-account"
    $field = "$prefix-connect"
    return [pscustomobject]@{
        Prefix = $prefix
        Account = $account
        Field = $field
        SessionKey = "ACT:SESSION:$account"
        GroupUid = "$prefix-group"
        OnlineTime = 1710000100
        CreateUpdateTime = 1710000110
        UpdateUpdateTime = 1710000120
        CreatedMarker = "delta-created"
        UpdatedMarker = "delta-updated"
    }
}

function Set-ActiveAccountSseDeltaRecord {
    param(
        [pscustomobject]$Seed,
        [string]$Marker,
        [long]$UpdateTime
    )

    Invoke-RedisHSetValue $Seed.SessionKey $Seed.Field `
        (New-ActiveSessionRecord $Seed.Account $Seed.Field $Seed.GroupUid $Marker `
            -OnlineTime $Seed.OnlineTime -UpdateTime $UpdateTime -IncludePasswordMaterial) | Out-Null
}

function Remove-ActiveAccountSseDeltaSeed {
    param([pscustomobject]$Seed)

    if (-not $Seed) {
        return
    }
    Invoke-RedisCommand DEL $Seed.SessionKey | Out-Null
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

function Test-ActiveAccountDeltaPayload {
    param(
        [object]$Payload,
        [pscustomobject]$Seed,
        [string]$ExpectedMarker,
        [long]$ExpectedUpdateTime
    )

    $record = Get-JsonProperty $Payload $Seed.Field
    if ($null -eq $record) {
        return $false
    }
    return (
        $record.account -eq $Seed.Account -and
        $record.connect_key -eq $Seed.Field -and
        $record.group_uid -eq $Seed.GroupUid -and
        $record.marker -eq $ExpectedMarker -and
        [long]$record.online_time -eq [long]$Seed.OnlineTime -and
        [long]$record.update_time -eq [long]$ExpectedUpdateTime
    )
}

function Assert-ActiveAccountDeltaPayload {
    param(
        [object]$Payload,
        [pscustomobject]$Seed,
        [string]$ExpectedMarker,
        [long]$ExpectedUpdateTime,
        [string]$Label
    )

    $record = Require-JsonProperty $Payload $Seed.Field "$Label active account delta payload"
    if (-not (Test-ActiveAccountDeltaPayload $Payload $Seed $ExpectedMarker $ExpectedUpdateTime)) {
        Fail "$Label active account delta record mismatch for field $($Seed.Field)"
    }
    Assert-NoPasswordMaterial $record "$Label active account delta"
}

function Test-ActiveAccountDeltaAbsent {
    param(
        [object]$Payload,
        [pscustomobject]$Seed
    )

    return ($null -eq (Get-JsonProperty $Payload $Seed.Field))
}

function Assert-ActiveAccountDeltaAbsent {
    param(
        [object]$Payload,
        [pscustomobject]$Seed,
        [string]$Label
    )

    if (-not (Test-ActiveAccountDeltaAbsent $Payload $Seed)) {
        Fail "$Label still included active account delta field $($Seed.Field)"
    }
}

function New-SseClient {
    param(
        [string]$Uri,
        [int]$TimeoutSec
    )

    $client = $null
    $response = $null
    $reader = $null
    try {
        $client = New-Object System.Net.Http.HttpClient
        $client.Timeout = [TimeSpan]::FromSeconds([Math]::Max($TimeoutSec + 30, 60))
        $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, $Uri)
        $response = $client.SendAsync($request, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        if (-not $response.IsSuccessStatusCode) {
            Fail "SSE request failed with HTTP $([int]$response.StatusCode)"
        }

        $stream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $reader = New-Object System.IO.StreamReader($stream)
        return [pscustomobject]@{
            Client = $client
            Response = $response
            Reader = $reader
            LineTask = $reader.ReadLineAsync()
            CurrentEvent = ""
            DataLines = (New-Object System.Collections.Generic.List[string])
        }
    }
    catch {
        if ($reader) { $reader.Dispose() }
        if ($response) { $response.Dispose() }
        if ($client) { $client.Dispose() }
        throw
    }
}

function Close-SseClient {
    param([object]$SseClient)

    if (-not $SseClient) {
        return
    }
    if ($SseClient.Reader) { $SseClient.Reader.Dispose() }
    if ($SseClient.Response) { $SseClient.Response.Dispose() }
    if ($SseClient.Client) { $SseClient.Client.Dispose() }
}

function Read-SseClientEvent {
    param(
        [object]$SseClient,
        [string]$ExpectedEvent,
        [int]$TimeoutSec,
        [scriptblock]$Predicate,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $lastPayload = ""

    while ((Get-Date) -lt $deadline) {
        if (-not $SseClient.LineTask.Wait(1000)) {
            continue
        }
        $line = $SseClient.LineTask.Result
        if ($null -eq $line) {
            break
        }

        if ($line.Length -eq 0) {
            if ($SseClient.CurrentEvent -eq $ExpectedEvent -and $SseClient.DataLines.Count -gt 0) {
                $data = $SseClient.DataLines -join "`n"
                $lastPayload = Format-DebugText $data
                try {
                    $payload = $data | ConvertFrom-Json
                }
                catch {
                    Fail "$Context received invalid JSON for SSE event '$ExpectedEvent': $(Format-DebugText $data)"
                }
                if (-not $Predicate -or (& $Predicate $payload)) {
                    return $payload
                }
            }
            $SseClient.CurrentEvent = ""
            $SseClient.DataLines.Clear()
        }
        elseif ($line.StartsWith("event:")) {
            $SseClient.CurrentEvent = $line.Substring(6).Trim()
        }
        elseif ($line.StartsWith("data:")) {
            $SseClient.DataLines.Add($line.Substring(5).TrimStart())
        }

        $SseClient.LineTask = $SseClient.Reader.ReadLineAsync()
    }

    Fail "$Context SSE event '$ExpectedEvent' was not received before timeout; last_payload=[$lastPayload]"
}

function Read-SseEvent {
    param(
        [string]$Uri,
        [string]$ExpectedEvent,
        [int]$TimeoutSec
    )

    $sse = $null
    try {
        $sse = New-SseClient $Uri $TimeoutSec
        return Read-SseClientEvent $sse $ExpectedEvent $TimeoutSec $null "SSE"
    }
    finally {
        Close-SseClient $sse
    }
}

function Invoke-ActiveAccountSseDeltaSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Token
    )

    $script:activeAccountSseDeltaSeed = New-ActiveAccountSseDeltaSeed
    $sseUri = "$Base/api/events/stream?token=$Token&channels=account_actives"
    $script:activeAccountSseClient = $null

    try {
        Say "opening SSE account_actives runtime delta stream"
        $script:activeAccountSseClient = New-SseClient $sseUri $StartupTimeoutSec
        Start-Sleep -Milliseconds 500

        $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
        Assert-ActiveAccountDeltaAbsent $rest $script:activeAccountSseDeltaSeed "REST /api/accounts/active initial"

        Say "validating SSE account_actives create delta"
        Set-ActiveAccountSseDeltaRecord $script:activeAccountSseDeltaSeed `
            $script:activeAccountSseDeltaSeed.CreatedMarker `
            $script:activeAccountSseDeltaSeed.CreateUpdateTime
        $created = Read-SseClientEvent $script:activeAccountSseClient "account_actives" $StartupTimeoutSec `
            { param($payload) Test-ActiveAccountDeltaPayload $payload $script:activeAccountSseDeltaSeed $script:activeAccountSseDeltaSeed.CreatedMarker $script:activeAccountSseDeltaSeed.CreateUpdateTime } `
            "SSE account_actives create delta"
        Assert-ActiveAccountDeltaPayload $created $script:activeAccountSseDeltaSeed `
            $script:activeAccountSseDeltaSeed.CreatedMarker `
            $script:activeAccountSseDeltaSeed.CreateUpdateTime `
            "SSE account_actives create"
        $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
        Assert-ActiveAccountDeltaPayload $rest $script:activeAccountSseDeltaSeed `
            $script:activeAccountSseDeltaSeed.CreatedMarker `
            $script:activeAccountSseDeltaSeed.CreateUpdateTime `
            "REST /api/accounts/active create"

        Say "validating SSE account_actives update delta"
        Set-ActiveAccountSseDeltaRecord $script:activeAccountSseDeltaSeed `
            $script:activeAccountSseDeltaSeed.UpdatedMarker `
            $script:activeAccountSseDeltaSeed.UpdateUpdateTime
        $updated = Read-SseClientEvent $script:activeAccountSseClient "account_actives" $StartupTimeoutSec `
            { param($payload) Test-ActiveAccountDeltaPayload $payload $script:activeAccountSseDeltaSeed $script:activeAccountSseDeltaSeed.UpdatedMarker $script:activeAccountSseDeltaSeed.UpdateUpdateTime } `
            "SSE account_actives update delta"
        Assert-ActiveAccountDeltaPayload $updated $script:activeAccountSseDeltaSeed `
            $script:activeAccountSseDeltaSeed.UpdatedMarker `
            $script:activeAccountSseDeltaSeed.UpdateUpdateTime `
            "SSE account_actives update"
        $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
        Assert-ActiveAccountDeltaPayload $rest $script:activeAccountSseDeltaSeed `
            $script:activeAccountSseDeltaSeed.UpdatedMarker `
            $script:activeAccountSseDeltaSeed.UpdateUpdateTime `
            "REST /api/accounts/active update"

        Say "validating SSE account_actives delete delta"
        Remove-ActiveAccountSseDeltaSeed $script:activeAccountSseDeltaSeed
        $deleted = Read-SseClientEvent $script:activeAccountSseClient "account_actives" $StartupTimeoutSec `
            { param($payload) Test-ActiveAccountDeltaAbsent $payload $script:activeAccountSseDeltaSeed } `
            "SSE account_actives delete delta"
        Assert-ActiveAccountDeltaAbsent $deleted $script:activeAccountSseDeltaSeed "SSE account_actives delete"
        $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
        Assert-ActiveAccountDeltaAbsent $rest $script:activeAccountSseDeltaSeed "REST /api/accounts/active delete"

        Close-SseClient $script:activeAccountSseClient
        $script:activeAccountSseClient = $null
        $script:activeAccountSseDeltaSeed = $null
        Say "PASS active account SSE runtime delta smoke"
    }
    finally {
        Close-SseClient $script:activeAccountSseClient
        $script:activeAccountSseClient = $null
        if ($script:activeAccountSseDeltaSeed) {
            Remove-ActiveAccountSseDeltaSeed $script:activeAccountSseDeltaSeed
            $script:activeAccountSseDeltaSeed = $null
        }
    }
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
$activeAccountSseDeltaSeed = $null
$activeAccountSseClient = $null
$ntripAuthSeed = $null
$ntripSourceConnection = $null
$ntripClientConnection = $null
$ntripNeedsNamedRover = $IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAuthBroadcast
$ntripNeedsAuthFixture = $ntripNeedsNamedRover -or $IncludeNtripAnonymousAuth
$stdout = Join-Path $env:TEMP ("navcaster-e2e-" + [guid]::NewGuid().ToString() + ".out.log")
$stderr = Join-Path $env:TEMP ("navcaster-e2e-" + [guid]::NewGuid().ToString() + ".err.log")
$scriptFailed = $false

try {
    Say "root=$RootPath configuration=$Configuration redis_mode=$RedisMode"

    if ($IncludeNtripAnonymousAuth -and $ntripNeedsNamedRover) {
        Fail "NTRIP anonymous auth smoke must run in a separate service lifecycle from named-rover NTRIP smokes because Rover_Setting.Anonymous_Login is scenario-specific."
    }
    if ($IncludeNtripAuthBroadcast -and ($IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection)) {
        Fail "NTRIP Auth Broadcast smoke must run in a separate service lifecycle because it starts a second local CasterService instance and requires Online_Protection=false."
    }
    if ($IncludeNtripAuthBroadcast -and $HttpPort -eq $NtripBroadcastHttpPort) {
        Fail "NTRIP Auth Broadcast secondary HTTP port must differ from primary HTTP port."
    }
    if ($IncludeNtripAuthBroadcast -and $NtripPort -eq $NtripBroadcastNtripPort) {
        Fail "NTRIP Auth Broadcast secondary NTRIP port must differ from primary NTRIP port."
    }

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
    $serviceText = Set-YamlValueInSection $serviceText "Ntrip_Listener_Setting" "Listen_Port" ([string]$NtripPort)
    $serviceText = Set-YamlValueInSection $serviceText "Ntrip_Listener_Setting" "Enable_Server_Login" "true"
    $serviceText = Set-YamlValueInSection $serviceText "Ntrip_Listener_Setting" "Enable_Client_Login" "true"
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
    if ($ntripNeedsAuthFixture) {
        $coreText = Set-YamlValueInSection $coreText "Caster_Setting" "Update_Intv" "1"
        $coreText = Set-YamlValueInSection $coreText "Caster_Setting" "Key_Expire_Time" "10"
        $coreText = Set-YamlValueInSection $coreText "Rover_Setting" "Enable_Mult" "true"
        $coreText = Set-YamlValueInSection $coreText "Rover_Setting" "Keep_Early" "false"
    }
    Write-TextFile $coreConfig $coreText

    $authText = $originals[$authConfig]
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "IP" $RedisHost
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Port" ([string]$RedisPort)
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    if ($ntripNeedsAuthFixture) {
        $roverOnlineProtection = if ($IncludeNtripAuthBroadcast) { "false" } elseif ($NtripOnlineProtectionScenario -eq "RejectNew") { "true" } else { "false" }
        $roverAnonymousLogin = if ($IncludeNtripAnonymousAuth -and $NtripAnonymousScenario -eq "AllowAnonymous") { "true" } else { "false" }
        $authText = Set-YamlValueInSection $authText "Base_Setting" "Anonymous_Login" "true"
        $authText = Set-YamlValueInSection $authText "Rover_Setting" "Anonymous_Login" $roverAnonymousLogin
        $authText = Set-YamlValueInSection $authText "Rover_Setting" "Online_Protection" $roverOnlineProtection
        $authText = Set-YamlValueInSection $authText "Source_Setting" "Anonymous_Login" "true"
    }
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

    if ($IncludeActiveAccountSseDelta) {
        Invoke-ActiveAccountSseDeltaSmoke $base $headers $login.token
    }

    if ($IncludeNtripAuthSession) {
        Invoke-NtripAuthSessionSmoke $base $headers $HttpBindAddr $NtripPort
    }

    if ($IncludeNtripAuthSessionRenewal) {
        Invoke-NtripAuthSessionRenewalSmoke $base $headers $HttpBindAddr $NtripPort $NtripRenewalWaitSec
    }

    if ($IncludeNtripOnlineProtection) {
        Invoke-NtripOnlineProtectionSmoke $base $headers $HttpBindAddr $NtripPort $NtripOnlineProtectionScenario
    }

    if ($IncludeNtripAnonymousAuth) {
        Invoke-NtripAnonymousAuthSmoke $base $headers $HttpBindAddr $NtripPort $NtripAnonymousScenario
    }

    if ($IncludeNtripAuthBroadcast) {
        Invoke-NtripAuthBroadcastSmoke $base $headers $HttpBindAddr $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    Say "PASS health/login/status/cluster smoke"
}
catch {
    $scriptFailed = $true
    [Console]::Error.WriteLine("[e2e-smoke FAIL] $($_.Exception.Message)")
}
finally {
    try {
        Close-NtripTcpConnection $ntripClientConnection
        $ntripClientConnection = $null
    }
    catch {
        Write-Warning "failed to close NTRIP client connection: $($_.Exception.Message)"
    }

    try {
        Close-NtripTcpConnection $ntripSourceConnection
        $ntripSourceConnection = $null
    }
    catch {
        Write-Warning "failed to close NTRIP source connection: $($_.Exception.Message)"
    }

    try {
        if ($activeAccountSseClient) {
            Close-SseClient $activeAccountSseClient
            $activeAccountSseClient = $null
        }
    }
    catch {
        Write-Warning "failed to close active account SSE client: $($_.Exception.Message)"
    }

    try {
        if ($activeAccountSseDeltaSeed) {
            Remove-ActiveAccountSseDeltaSeed $activeAccountSseDeltaSeed
            $activeAccountSseDeltaSeed = $null
        }
    }
    catch {
        Write-Warning "failed to remove active account SSE delta Redis seed: $($_.Exception.Message)"
    }

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
        if ($ntripAuthSeed) {
            Remove-NtripAuthSessionSeed $ntripAuthSeed
            $ntripAuthSeed = $null
        }
    }
    catch {
        Write-Warning "failed to remove NTRIP Auth Redis seed: $($_.Exception.Message)"
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
