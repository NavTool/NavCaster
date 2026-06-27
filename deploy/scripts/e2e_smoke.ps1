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
    [switch]$IncludeNtripAuthBroadcast,
    [switch]$IncludeLocalDualNodeIdentity,
    [switch]$IncludeMasterLeaseFailover,
    [switch]$IncludeMasterLeaseStability,
    [switch]$IncludeDockerBridgeCluster,
    [string]$NavCasterImage = "",
    [string]$DockerBridgeNetworkName = "navcaster-e2e-nc031-$PID",
    [switch]$IncludeHttpIngressStrategy,
    [string]$NginxImage = "nginx:latest",
    [string]$HttpIngressNetworkName = "navcaster-e2e-nc032-$PID",
    [int]$HttpIngressStickyPort = 18080,
    [int]$HttpIngressRoundRobinPort = 18081,
    [switch]$IncludeRelayPullStartStop,
    [switch]$IncludeRelayPushStartStop,
    [switch]$IncludeRelayDataForwarding,
    [switch]$IncludeRelayFailover,
    [switch]$IncludeRelayPushFailover,
    [switch]$IncludeNtripDisabledAccount,
    [switch]$IncludeRedisReconnect,
    [switch]$IncludeOperationsApi,
    [switch]$IncludeSelfServiceApi,
    [switch]$IncludeAccessRuntimeEnforcement,
    [switch]$IncludeWebThreeRoleBrowserSmoke,
    [switch]$IncludeDataPushRelayPushE2E,
    [int]$WebPort = 15173,
    [int]$BrowserDebugPort = 19222
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
        TargetMount = "${prefix}_TARGET_MPT"
        Account = "${prefix}_rover"
        Password = "${prefix}_pw"
        SourceUser = "${prefix}_source"
        SourcePassword = "${prefix}_source_pw"
        GroupUid = "default"
        ConnectionLimit = $ConnectionLimit
        SourceConnectKey = ""
        TargetSourceConnectKey = ""
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

function Add-NtripSourceAuthAccountSeed {
    param([pscustomobject]$Seed)

    $accounts = @(
        [pscustomobject]@{ Account = $Seed.SourceUser; Password = $Seed.SourcePassword },
        [pscustomobject]@{ Account = $Seed.SourcePassword; Password = $Seed.SourcePassword }
    )

    foreach ($account in $accounts) {
        Say "seeding NTRIP source auth ACT:ACTIVE record account=$($account.Account) mount=$($Seed.Mount)"
        $activeIndex = [ordered]@{
            schema_version = 1
            uid = $account.Account
            account = $account.Account
            password = $account.Password
            legacy_plain_password = $true
            group_uid = $Seed.GroupUid
            connection_limit = $Seed.ConnectionLimit
            type = 0
            state = 1
            active = 1
            expire_time = 0
        }
        Invoke-RedisHSetValue "ACT:ACTIVE" $account.Account (ConvertTo-CompactJson $activeIndex) | Out-Null
    }
}

function Remove-NtripAuthSessionSeed {
    param([pscustomobject]$Seed)

    if (-not $Seed) {
        return
    }

    Invoke-RedisCommand HDEL "ACT:ACTIVE" $Seed.Account | Out-Null
    Invoke-RedisCommand HDEL "ACT:ACTIVE" $Seed.SourceUser | Out-Null
    Invoke-RedisCommand HDEL "ACT:ACTIVE" $Seed.SourcePassword | Out-Null
    Invoke-RedisCommand HDEL "ACT:UNNAMED" $Seed.SourceUser | Out-Null
    Invoke-RedisCommand HDEL "ACT:UNNAMED" $Seed.SourcePassword | Out-Null
    Invoke-RedisCommand HDEL "MPT:LIST" $Seed.Mount | Out-Null
    if ($Seed.PSObject.Properties["TargetMount"] -and -not [string]::IsNullOrWhiteSpace($Seed.TargetMount)) {
        Invoke-RedisCommand HDEL "MPT:LIST" $Seed.TargetMount | Out-Null
        Invoke-RedisCommand HDEL "MPT:SOURCE" $Seed.TargetMount | Out-Null
    }
    Invoke-RedisCommand HDEL "USR:LIST" $Seed.Account | Out-Null
    Invoke-RedisCommand HDEL "MPT:SOURCE" $Seed.Mount | Out-Null
    if ($Seed.SourceConnectKey) {
        Invoke-RedisCommand HDEL "MPT:STAT" $Seed.SourceConnectKey | Out-Null
        Invoke-RedisCommand HDEL "STR:STAT" $Seed.SourceConnectKey | Out-Null
    }
    if ($Seed.PSObject.Properties["TargetSourceConnectKey"] -and $Seed.TargetSourceConnectKey) {
        Invoke-RedisCommand HDEL "MPT:STAT" $Seed.TargetSourceConnectKey | Out-Null
        Invoke-RedisCommand HDEL "STR:STAT" $Seed.TargetSourceConnectKey | Out-Null
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
        "ACT:SESSION:$($Seed.SourceUser)" `
        "ACT:SESSION:$($Seed.SourcePassword)" `
        "ACT:REC:$($Seed.SourceUser)" `
        "ACT:REC:$($Seed.SourcePassword)" `
        "ACT:REC:$($Seed.Account)" `
        "ACT:UND:$($Seed.SourceUser)" `
        "ACT:UND:$($Seed.SourcePassword)" `
        "USR:REC:$($Seed.SourceUser)" `
        "USR:REC:$($Seed.SourcePassword)" `
        "USR:REC:$($Seed.Account)" `
        "USR:SUB:$($Seed.Account)" `
        "MPT:REC:$($Seed.Mount)" `
        "MPT:SUB:$($Seed.Mount)" `
        "LOG:USR:$($Seed.Account)" `
        "LOG:MPT:$($Seed.Mount)" | Out-Null
    if ($Seed.PSObject.Properties["TargetMount"] -and -not [string]::IsNullOrWhiteSpace($Seed.TargetMount)) {
        Invoke-RedisCommand DEL `
            "MPT:REC:$($Seed.TargetMount)" `
            "MPT:SUB:$($Seed.TargetMount)" `
            "LOG:MPT:$($Seed.TargetMount)" | Out-Null
    }
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

function Find-ByteSequenceIndex {
    param(
        [byte[]]$Haystack,
        [byte[]]$Needle
    )

    if ($null -eq $Haystack -or $null -eq $Needle -or $Needle.Length -eq 0 -or $Haystack.Length -lt $Needle.Length) {
        return -1
    }

    for ($i = 0; $i -le ($Haystack.Length - $Needle.Length); $i++) {
        $matched = $true
        for ($j = 0; $j -lt $Needle.Length; $j++) {
            if ($Haystack[$i + $j] -ne $Needle[$j]) {
                $matched = $false
                break
            }
        }
        if ($matched) {
            return $i
        }
    }

    return -1
}

function Write-NtripPayload {
    param(
        [object]$Connection,
        [string]$Payload,
        [string]$Context
    )

    if (-not $Connection -or -not $Connection.Client -or -not $Connection.Client.Connected) {
        Fail "$Context source connection is not open"
    }

    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Payload)
    $stream = $Connection.Client.GetStream()
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
    Say "$Context wrote payload bytes=$($bytes.Length)"
}

function Wait-NtripPayload {
    param(
        [object]$Connection,
        [string]$ExpectedPayload,
        [int]$TimeoutSec,
        [string]$Context
    )

    if (-not $Connection -or -not $Connection.Client -or -not $Connection.Client.Connected) {
        Fail "$Context client connection is not open"
    }

    $expected = [System.Text.Encoding]::ASCII.GetBytes($ExpectedPayload)
    $buffer = New-Object byte[] 4096
    $received = New-Object System.Collections.Generic.List[byte]
    $stream = $Connection.Client.GetStream()
    $deadline = (Get-Date).AddSeconds($TimeoutSec)

    do {
        while ($Connection.Client.Available -gt 0 -or $stream.DataAvailable) {
            $available = $Connection.Client.Available
            if ($available -le 0) {
                $available = $buffer.Length
            }
            $read = $stream.Read($buffer, 0, [Math]::Min($buffer.Length, $available))
            if ($read -le 0) {
                break
            }
            for ($i = 0; $i -lt $read; $i++) {
                $received.Add($buffer[$i])
            }
            if ((Find-ByteSequenceIndex ([byte[]]$received.ToArray()) $expected) -ge 0) {
                Say "$Context received expected payload bytes=$($expected.Length)"
                return
            }
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)

    $receivedText = [System.Text.Encoding]::ASCII.GetString([byte[]]$received.ToArray())
    Fail "$Context did not receive expected payload before timeout; expected=[$ExpectedPayload] received=[$(Format-DebugText $receivedText)]"
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

function Wait-NtripAuthActiveIndexPresent {
    param(
        [pscustomobject]$Seed,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $raw = Invoke-RedisCommand HGET "ACT:ACTIVE" $Seed.Account
        if (-not [string]::IsNullOrWhiteSpace($raw)) {
            return $raw
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $activeRaw = Format-DebugText (Invoke-RedisCommand HGETALL "ACT:ACTIVE")
    Fail "$Context expected ACT:ACTIVE index for $($Seed.Account); active=[$activeRaw]"
}

function Wait-NtripAuthActiveIndexGone {
    param(
        [pscustomobject]$Seed,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $raw = Invoke-RedisCommand HGET "ACT:ACTIVE" $Seed.Account
        if ([string]::IsNullOrWhiteSpace($raw)) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGET "ACT:ACTIVE" $Seed.Account)
    Fail "$Context expected ACT:ACTIVE index removed for $($Seed.Account); raw=[$raw]"
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
        [bool]$RoverAnonymousLogin,
        [bool]$BaseAnonymousLogin = $true,
        [bool]$SourceAnonymousLogin = $true
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
    $authText = Set-YamlValueInSection $authText "Base_Setting" "Anonymous_Login" ($BaseAnonymousLogin.ToString().ToLowerInvariant())
    $authText = Set-YamlValueInSection $authText "Rover_Setting" "Anonymous_Login" ($RoverAnonymousLogin.ToString().ToLowerInvariant())
    $authText = Set-YamlValueInSection $authText "Rover_Setting" "Online_Protection" ($RoverOnlineProtection.ToString().ToLowerInvariant())
    $authText = Set-YamlValueInSection $authText "Source_Setting" "Anonymous_Login" ($SourceAnonymousLogin.ToString().ToLowerInvariant())
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
            if (-not $Node.Process.WaitForExit(5000)) {
                Write-Warning "CasterService $($Node.Label) did not exit within 5 seconds"
            }
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

function Open-NtripClientForMount {
    param(
        [pscustomobject]$Seed,
        [string]$Mount,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Label = "client",
        [switch]$AllowRejected
    )

    $clientAuth = ConvertTo-BasicAuthValue $Seed.Account $Seed.Password
    $clientRequest = "GET /$Mount HTTP/1.1`r`n" +
        "Host: ${NtripHost}:$NtripPort`r`n" +
        "Ntrip-Version: Ntrip/2.0`r`n" +
        "Authorization: Basic $clientAuth`r`n" +
        "User-Agent: NTRIP NavCasterE2E/$($Seed.Prefix)`r`n" +
        "Connection: close`r`n`r`n"

    Say "opening NTRIP client account=$($Seed.Account) mount=$Mount label=$Label"
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

function Open-NtripClientForSeed {
    param(
        [pscustomobject]$Seed,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Label = "client",
        [switch]$AllowRejected
    )

    return Open-NtripClientForMount `
        -Seed $Seed `
        -Mount $Seed.Mount `
        -NtripHost $NtripHost `
        -NtripPort $NtripPort `
        -Label $Label `
        -AllowRejected:$AllowRejected
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

function New-NtripDisabledAccountBody {
    param(
        [pscustomobject]$Seed,
        [string]$Scenario = "Enabled"
    )

    $body = [ordered]@{
        uid = $Seed.Account
        account = $Seed.Account
        password = $Seed.Password
        group_uid = $Seed.GroupUid
        connection_limit = $Seed.ConnectionLimit
        type = 1
        state = 1
        active = 1
        expire_time = 0
    }

    if ($Scenario -eq "Frozen") {
        $body.state = 2
    }
    elseif ($Scenario -eq "Inactive") {
        $body.active = 2
    }
    elseif ($Scenario -eq "Expired") {
        $body.type = 2
        $body.expire_time = 1
    }

    return $body
}

function Invoke-NtripCreateAccount {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [pscustomobject]$Seed
    )

    $body = New-NtripDisabledAccountBody $Seed "Enabled" | ConvertTo-Json -Compress
    $response = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body $body -Uri "$Base/api/accounts" -TimeoutSec 10
    if ($response.account -ne $Seed.Account) {
        Fail "create account response mismatch: expected=$($Seed.Account) actual=$($response.account)"
    }
}

function Invoke-NtripUpdateAccountState {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [pscustomobject]$Seed,
        [string]$Scenario
    )

    $body = New-NtripDisabledAccountBody $Seed $Scenario | ConvertTo-Json -Compress
    $response = Invoke-RestMethod -Method Put -Headers $Headers -ContentType "application/json" -Body $body -Uri "$Base/api/accounts/$($Seed.Account)" -TimeoutSec 10
    if ($response.ok -ne $true) {
        Fail "update account $Scenario did not return ok"
    }
}

function Remove-NtripHttpAccount {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [pscustomobject]$Seed
    )

    if (-not $Seed) {
        return
    }

    try {
        Invoke-RestMethod -Method Delete -Headers $Headers -Uri "$Base/api/accounts/$($Seed.Account)" -TimeoutSec 10 | Out-Null
    }
    catch {
    }

    Remove-NtripAuthSessionSeed $Seed
}

function Assert-NtripNoNamedAuthResidue {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [pscustomobject]$Seed,
        [string]$Context
    )

    $sessionRaw = Format-DebugText (Invoke-RedisCommand HGETALL "ACT:SESSION:$($Seed.Account)")
    $actRecRaw = Format-DebugText (Invoke-RedisCommand HGETALL "ACT:REC:$($Seed.Account)")
    $usrRecRaw = Format-DebugText (Invoke-RedisCommand HGETALL "USR:REC:$($Seed.Account)")
    if (-not [string]::IsNullOrWhiteSpace($sessionRaw) -or
        -not [string]::IsNullOrWhiteSpace($actRecRaw) -or
        -not [string]::IsNullOrWhiteSpace($usrRecRaw)) {
        Fail "$Context left named auth records: ACT:SESSION=[$sessionRaw] ACT:REC=[$actRecRaw] USR:REC=[$usrRecRaw]"
    }

    $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
    foreach ($prop in @($rest.PSObject.Properties)) {
        $value = $prop.Value
        if ($prop.Name -like "$($Seed.Prefix)*" -or
            $prop.Name -eq $Seed.ClientConnectKey -or
            $prop.Name -in @($Seed.ExtraClientConnectKeys) -or
            ($value -and $value.account -eq $Seed.Account)) {
            Fail "$Context REST /api/accounts/active exposed disabled account residue field=$($prop.Name)"
        }
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

function Invoke-NtripDisabledAccountSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NtripHost,
        [int]$NtripPort
    )

    $sourceConnection = $null
    $clientConnection = $null
    $rejectedClient = $null
    $seed = $null

    try {
        $seed = New-NtripAuthSessionSeed -Label "nc023_disabled" -ConnectionLimit 1
        $script:ntripAuthSeed = $seed

        Invoke-NtripCreateAccount $Base $Headers $seed
        Wait-NtripAuthActiveIndexPresent $seed $StartupTimeoutSec "disabled account enabled create" | Out-Null

        $sourceConnection = Open-NtripSourceForSeed $seed $NtripHost $NtripPort "source-disabled-account"
        $script:ntripSourceConnection = $sourceConnection

        $clientConnection = Open-NtripClientForSeed $seed $NtripHost $NtripPort "client-enabled-account"
        $script:ntripClientConnection = $clientConnection
        $enabledSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $enabledSession.Field
        Wait-NtripOnlineExactFields $seed @($enabledSession.Field) $StartupTimeoutSec "disabled account enabled login" | Out-Null

        $rest = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/active" -TimeoutSec 10
        Assert-NtripActiveAccountPayload $rest $seed $enabledSession "REST /api/accounts/active enabled account"

        Close-NtripTcpConnection $clientConnection
        $clientConnection = $null
        $script:ntripClientConnection = $null
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "disabled account enabled cleanup" | Out-Null

        foreach ($scenario in @("Frozen", "Inactive", "Expired")) {
            Say "validating disabled account scenario $scenario account=$($seed.Account)"
            Invoke-NtripUpdateAccountState $Base $Headers $seed $scenario
            Wait-NtripAuthActiveIndexGone $seed $StartupTimeoutSec "disabled account $scenario update"

            $rejectedClient = Open-NtripClientForSeed $seed $NtripHost $NtripPort "client-disabled-$scenario" -AllowRejected
            $script:ntripClientConnection = $rejectedClient
            Wait-NtripTcpConnectionClosed $rejectedClient 10 "disabled account $scenario rejected client"
            Close-NtripTcpConnection $rejectedClient
            $rejectedClient = $null
            $script:ntripClientConnection = $null

            Start-Sleep -Milliseconds 500
            Assert-NtripNoNamedAuthResidue $Base $Headers $seed "disabled account $scenario rejected login"
        }

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        $script:ntripSourceConnection = $null

        Remove-NtripHttpAccount $Base $Headers $seed
        $seed = $null
        $script:ntripAuthSeed = $null
        Say "PASS NTRIP disabled account matrix smoke"
    }
    finally {
        Close-NtripTcpConnection $rejectedClient
        Close-NtripTcpConnection $clientConnection
        Close-NtripTcpConnection $sourceConnection
        if ($seed) {
            Remove-NtripHttpAccount $Base $Headers $seed
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

function Copy-E2eHeaders {
    param([hashtable]$Headers)

    $copy = @{}
    if ($Headers) {
        foreach ($key in $Headers.Keys) {
            $copy[$key] = $Headers[$key]
        }
    }
    return $copy
}

function Invoke-E2eLoginWithHeaders {
    param(
        [string]$Base,
        [hashtable]$ExtraHeaders = @{},
        [string]$Context = "login",
        [string]$Username = $AdminUser,
        [string]$Password = $AdminPassword
    )

    Say "logging in as $Username ($Context)"
    $loginBody = @{ username = $Username; password = $Password } | ConvertTo-Json -Compress
    $requestHeaders = Copy-E2eHeaders $ExtraHeaders
    $login = Invoke-RestMethod -Method Post -Headers $requestHeaders -ContentType "application/json" -Body $loginBody -Uri "$Base/api/auth/login" -TimeoutSec 10
    if (-not $login.token) {
        Fail "login did not return token"
    }

    $headers = Copy-E2eHeaders $ExtraHeaders
    $headers["Authorization"] = "Bearer $($login.token)"

    return [pscustomobject]@{
        Token = $login.token
        Headers = $headers
    }
}

function Invoke-E2eLogin {
    param([string]$Base)

    return Invoke-E2eLoginWithHeaders -Base $Base -Context "default"
}

function Assert-E2eHealth {
    param(
        [string]$Base,
        [string]$Context
    )

    $health = Invoke-RestMethod -Uri "$Base/api/status/health" -TimeoutSec 5
    if ($health.status -ne "ok") {
        Fail "$Context health endpoint returned unexpected status: $($health.status)"
    }
}

function Assert-E2eStatusAndCluster {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Context,
        [switch]$RequireRedisConnected
    )

    $status = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/status" -TimeoutSec 10
    if ($null -eq $status.node_id -or $null -eq $status.cpu_percent) {
        Fail "$Context status response missing node_id or cpu_percent"
    }
    if ($RequireRedisConnected -and ($status.redis_auth_connected -ne $true -or $status.redis_caster_connected -ne $true)) {
        Fail "$Context status response reports Redis disconnected: caster=$($status.redis_caster_connected) auth=$($status.redis_auth_connected)"
    }

    $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
    if ($null -eq $cluster.nodes) {
        Fail "$Context cluster monitor response missing nodes"
    }

    return $status
}

function Assert-E2eNodeIdFormat {
    param(
        [string]$NodeId,
        [string]$Context
    )

    if ([string]::IsNullOrWhiteSpace($NodeId) -or $NodeId -notmatch '^Node_[0-9A-F]{5}$') {
        Fail "$Context returned invalid node_id: $NodeId"
    }
}

function Invoke-OperationsApiSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers
    )

    Say "validating /api/v1 operations admin API"
    $prefix = "nc053_$PID"
    $accountId = "${prefix}_acc"
    $username = "${prefix}_user"
    $groupId = "${prefix}_grp"
    $mount = "${prefix}_MPT"
    $subscriptionId = "${prefix}_sub"
    $planId = "${prefix}_plan"
    $planSubscriptionId = "${prefix}_plan_sub"
    $ledgerId = "${prefix}_ledger"

    $session = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/auth/session" -TimeoutSec 10
    if ($session.role -ne "admin" -or $session.compat_admin -ne $true) {
        Fail "operations session subject unexpected: $(ConvertTo-CompactJson $session)"
    }

    $accountBody = @{
        account_id = $accountId
        username = $username
        role = "user"
        balance_cents = 5000
        concurrency_limit = 2
    } | ConvertTo-Json -Compress
    $account = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body $accountBody -Uri "$Base/api/v1/admin/accounts" -TimeoutSec 10
    if ($account.account_id -ne $accountId -or $account.role -ne "user") {
        Fail "operations create account response mismatch: $(ConvertTo-CompactJson $account)"
    }

    $accounts = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/accounts" -TimeoutSec 10
    if (-not $accounts.PSObject.Properties[$accountId]) {
        Fail "operations account list missing $accountId"
    }

    $groupBody = @{ group_id = $groupId; name = "NC-053 group"; billing_multiplier = 1.0 } | ConvertTo-Json -Compress
    $group = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body $groupBody -Uri "$Base/api/v1/admin/mount-point-groups" -TimeoutSec 10
    if ($group.group_id -ne $groupId) {
        Fail "operations create group response mismatch: $(ConvertTo-CompactJson $group)"
    }

    $mountBody = @{ hourly_price_cents = 120 } | ConvertTo-Json -Compress
    $mountPoint = Invoke-RestMethod -Method Put -Headers $Headers -ContentType "application/json" -Body $mountBody -Uri "$Base/api/v1/admin/mount-points/$mount" -TimeoutSec 10
    if ($mountPoint.mountpoint -ne $mount) {
        Fail "operations create mount response mismatch: $(ConvertTo-CompactJson $mountPoint)"
    }

    $memberBody = @{ mountpoint = $mount } | ConvertTo-Json -Compress
    $member = Invoke-RestMethod -Method Put -Headers $Headers -ContentType "application/json" -Body $memberBody -Uri "$Base/api/v1/admin/mount-point-groups/$groupId/members" -TimeoutSec 10
    if ($member.mountpoint -ne $mount -or $member.group_id -ne $groupId) {
        Fail "operations group member response mismatch: $(ConvertTo-CompactJson $member)"
    }

    $grantBody = @{ group_id = $groupId } | ConvertTo-Json -Compress
    $grant = Invoke-RestMethod -Method Put -Headers $Headers -ContentType "application/json" -Body $grantBody -Uri "$Base/api/v1/admin/accounts/$accountId/group-grants" -TimeoutSec 10
    if ($grant.account_id -ne $accountId -or $grant.group_id -ne $groupId) {
        Fail "operations account grant response mismatch: $(ConvertTo-CompactJson $grant)"
    }

    $planBody = @{
        plan_id = $planId
        name = "NC-075 plan"
        group_ids = @($groupId)
        price_cents = 9900
        duration_days = 30
    } | ConvertTo-Json -Compress
    $plan = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body $planBody -Uri "$Base/api/v1/admin/subscription-plans" -TimeoutSec 10
    if ($plan.plan_id -ne $planId -or $plan.price_cents -ne 9900) {
        Fail "operations subscription plan response mismatch: $(ConvertTo-CompactJson $plan)"
    }
    $plan = Invoke-RestMethod -Method Put -Headers $Headers -ContentType "application/json" -Body (@{
        price_cents = 12900
        duration_days = 45
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscription-plans/$planId" -TimeoutSec 10
    if ($plan.price_cents -ne 12900 -or $plan.duration_days -ne 45) {
        Fail "operations subscription plan update mismatch: $(ConvertTo-CompactJson $plan)"
    }
    $plans = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/subscription-plans" -TimeoutSec 10
    if (-not $plans.PSObject.Properties[$planId]) {
        Fail "operations subscription plan list missing $planId"
    }

    $subscriptionBody = @{
        subscription_id = $subscriptionId
        account_id = $accountId
        group_ids = @($groupId)
        expire_time = 9999999999
    } | ConvertTo-Json -Compress
    $subscription = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body $subscriptionBody -Uri "$Base/api/v1/admin/subscriptions" -TimeoutSec 10
    if ($subscription.subscription_id -ne $subscriptionId) {
        Fail "operations subscription response mismatch: $(ConvertTo-CompactJson $subscription)"
    }
    $planSubscription = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body (@{
        subscription_id = $planSubscriptionId
        account_id = $accountId
        plan_id = $planId
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscriptions" -TimeoutSec 10
    if ($planSubscription.subscription_id -ne $planSubscriptionId -or $planSubscription.plan_id -ne $planId -or $planSubscription.price_cents -ne 12900) {
        Fail "operations plan subscription response mismatch: $(ConvertTo-CompactJson $planSubscription)"
    }
    if ($null -eq $planSubscription.plan_snapshot -or $planSubscription.plan_snapshot.plan_id -ne $planId) {
        Fail "operations plan subscription missing plan snapshot: $(ConvertTo-CompactJson $planSubscription)"
    }
    $deletedPlan = Invoke-RestMethod -Method Delete -Headers $Headers -Uri "$Base/api/v1/admin/subscription-plans/$planId" -TimeoutSec 10
    if ($deletedPlan.status -ne "deleted") {
        Fail "operations subscription plan delete mismatch: $(ConvertTo-CompactJson $deletedPlan)"
    }
    $deletedPlanSubStatus = Get-HttpStatusCode -Context "subscription from deleted plan" -Request {
        Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body (@{
            subscription_id = "${prefix}_deleted_plan_sub"
            account_id = $accountId
            plan_id = $planId
        } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscriptions" -TimeoutSec 10
    }
    if ($deletedPlanSubStatus -ne 404) {
        Fail "subscription from deleted plan expected 404, got $deletedPlanSubStatus"
    }

    $ledgerBody = @{
        ledger_id = $ledgerId
        period = "202606"
        delta_cents = 1000
        balance_after_cents = 6000
        source = "e2e_manual_adjustment"
    } | ConvertTo-Json -Compress
    $ledger = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body $ledgerBody -Uri "$Base/api/v1/admin/accounts/$accountId/balance-adjustments" -TimeoutSec 10
    if ($ledger.ledger_id -ne $ledgerId -or $ledger.account_id -ne $accountId) {
        Fail "operations balance adjustment response mismatch: $(ConvertTo-CompactJson $ledger)"
    }

    $accessAccounts = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/access-accounts" -TimeoutSec 10
    if ($null -eq $accessAccounts) {
        Fail "operations access account list returned null"
    }

    $stations = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/stations" -TimeoutSec 10
    if ($null -eq $stations) {
        Fail "operations station list returned null"
    }

    $usage = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/usage?period=202606" -TimeoutSec 10
    if ($null -eq $usage) {
        Fail "operations usage list returned null"
    }

    $supply = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/supply-usage?period=202606" -TimeoutSec 10
    if ($null -eq $supply) {
        Fail "operations supply usage list returned null"
    }

    $alertPolicy = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/operations-alert-policy" -TimeoutSec 10
    if ($alertPolicy.policy_id -ne "default" -or $alertPolicy.low_balance_threshold_cents -ne 1000) {
        Fail "operations alert policy default mismatch: $(ConvertTo-CompactJson $alertPolicy)"
    }
    $alertPolicy = Invoke-RestMethod -Method Put -Headers $Headers -ContentType "application/json" -Body (@{
        enabled = $true
        low_balance_enabled = $true
        low_balance_threshold_cents = 7000
        data_push_failed_enabled = $true
        data_push_failed_threshold = 1
        supplier_pending_payment_enabled = $false
        supplier_pending_payment_threshold = 99
        supplier_usage_pending_enabled = $false
        supplier_usage_pending_threshold = 99
        negative_balance_enabled = $true
        data_push_maintenance_disabled_enabled = $true
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/operations-alert-policy" -TimeoutSec 10
    if ($alertPolicy.low_balance_threshold_cents -ne 7000 -or $alertPolicy.supplier_pending_payment_enabled -ne $false) {
        Fail "operations alert policy update mismatch: $(ConvertTo-CompactJson $alertPolicy)"
    }

    $monitor = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/operations-monitor?period=202606" -TimeoutSec 10
    if ($monitor.alert_policy.low_balance_threshold_cents -ne 7000 -or $monitor.accounts.low_balance_threshold_cents -ne 7000) {
        Fail "operations monitor alert policy snapshot mismatch: $(ConvertTo-CompactJson $monitor.alert_policy)"
    }
    $alertCodes = @($monitor.alerts | ForEach-Object { $_.code })
    if ($alertCodes -notcontains "low_balance") {
        Fail "operations monitor expected low_balance alert after policy update: $(ConvertTo-CompactJson $monitor.alerts)"
    }
    if ($alertCodes -contains "supplier_pending_payment") {
        Fail "operations monitor supplier_pending_payment alert should be disabled by policy: $(ConvertTo-CompactJson $monitor.alerts)"
    }
    $alertSync = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body "{}" -Uri "$Base/api/v1/admin/operations-alert-events?action=sync&period=202606" -TimeoutSec 10
    if ($alertSync.period -ne "202606" -or $alertSync.synced_count -lt 1) {
        Fail "operations alert events sync mismatch: $(ConvertTo-CompactJson $alertSync)"
    }
    $alertEvents = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/operations-alert-events?period=202606" -TimeoutSec 10
    if ($null -eq $alertEvents.'opsalert:202606:low_balance') {
        Fail "operations alert events missing low_balance: $(ConvertTo-CompactJson $alertEvents)"
    }
    $ackAlert = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body (@{ operator = "e2e"; operator_note = "ack from e2e" } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/operations-alert-events/opsalert%3A202606%3Alow_balance/acknowledge?period=202606" -TimeoutSec 10
    if ($ackAlert.status -ne "acknowledged") {
        Fail "operations alert event acknowledge mismatch: $(ConvertTo-CompactJson $ackAlert)"
    }
    $resolvedAlert = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body (@{ operator = "e2e"; operator_note = "resolve from e2e" } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/operations-alert-events/opsalert%3A202606%3Alow_balance/resolve?period=202606" -TimeoutSec 10
    if ($resolvedAlert.status -ne "resolved") {
        Fail "operations alert event resolve mismatch: $(ConvertTo-CompactJson $resolvedAlert)"
    }
    $resolvedEvents = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/operations-alert-events?period=202606&status=resolved" -TimeoutSec 10
    if ($null -eq $resolvedEvents.'opsalert:202606:low_balance') {
        Fail "operations alert events resolved filter missing low_balance: $(ConvertTo-CompactJson $resolvedEvents)"
    }
    $monitorWithEvents = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/v1/admin/operations-monitor?period=202606" -TimeoutSec 10
    if ($monitorWithEvents.alert_events.total_count -lt 1) {
        Fail "operations monitor alert event summary mismatch: $(ConvertTo-CompactJson $monitorWithEvents.alert_events)"
    }
    $invalidAlertStatus = Get-HttpStatusCode -Context "invalid operations alert policy" -Request {
        Invoke-RestMethod -Method Put -Headers $Headers -ContentType "application/json" -Body (@{ low_balance_threshold_cents = -1 } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/operations-alert-policy" -TimeoutSec 10
    }
    if ($invalidAlertStatus -ne 400) {
        Fail "invalid operations alert policy expected 400, got $invalidAlertStatus"
    }
}

function Get-HttpStatusCode {
    param(
        [scriptblock]$Request,
        [string]$Context
    )

    try {
        & $Request | Out-Null
        return 200
    }
    catch {
        if ($_.Exception.Response -and $_.Exception.Response.StatusCode) {
            return [int]$_.Exception.Response.StatusCode
        }
        Fail "$Context failed without HTTP status: $($_.Exception.Message)"
    }
}

function Invoke-SelfServiceApiSmoke {
    param(
        [string]$Base,
        [hashtable]$AdminHeaders
    )

    Say "validating /api/v1 self-service API"
    $prefix = "nc054_$PID"
    $userAccountId = "${prefix}_user_acc"
    $supplierAccountId = "${prefix}_supplier_acc"
    $adminAccountId = "${prefix}_admin_acc"
    $userName = "${prefix}_user"
    $supplierName = "${prefix}_supplier"
    $adminName = "${prefix}_admin"
    $password = "self-service-pass"
    $groupId = "${prefix}_grp"
    $otherGroupId = "${prefix}_other_grp"
    $mount = "${prefix}_MPT"
    $userAccessId = "${prefix}_user_aacc"
    $supplierAccessId = "${prefix}_supplier_aacc"

    foreach ($body in @(
        @{ account_id = $userAccountId; username = $userName; role = "user"; password = $password; balance_cents = 5000; concurrency_limit = 2 },
        @{ account_id = $supplierAccountId; username = $supplierName; role = "supplier"; password = $password; balance_cents = 0; concurrency_limit = 2 },
        @{ account_id = $adminAccountId; username = $adminName; role = "admin"; password = $password; balance_cents = 0; concurrency_limit = 2 }
    )) {
        $created = Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body ($body | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/accounts" -TimeoutSec 10
        if ($created.account_id -ne $body.account_id -or $created.PSObject.Properties["password_hash"]) {
            Fail "self-service fixture account create mismatch: $(ConvertTo-CompactJson $created)"
        }
    }

    foreach ($body in @(
        @{ group_id = $groupId; name = "NC-054 group"; billing_multiplier = 1.0 },
        @{ group_id = $otherGroupId; name = "NC-054 other group"; billing_multiplier = 1.0 }
    )) {
        Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body ($body | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-point-groups" -TimeoutSec 10 | Out-Null
    }
    Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{ hourly_price_cents = 100 } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-points/$mount" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{ mountpoint = $mount } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-point-groups/$groupId/members" -TimeoutSec 10 | Out-Null
    foreach ($accountId in @($userAccountId, $supplierAccountId, $adminAccountId)) {
        Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{ group_id = $groupId } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/accounts/$accountId/group-grants" -TimeoutSec 10 | Out-Null
    }
    $planId = "${prefix}_plan"
    $overBalancePlanId = "${prefix}_over_balance_plan"
    Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        plan_id = $planId
        name = "NC-076 self plan"
        group_ids = @($groupId)
        price_cents = 1200
        duration_days = 30
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscription-plans" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        plan_id = $overBalancePlanId
        name = "NC-076 over balance plan"
        group_ids = @($groupId)
        price_cents = 999999
        duration_days = 30
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscription-plans" -TimeoutSec 10 | Out-Null
    $redeemCode = "${prefix}_RC"
    Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        code = $redeemCode
        amount_cents = 700
        max_redemptions = 1
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/redeem-codes" -TimeoutSec 10 | Out-Null

    $userLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "self-service user" -Username $userName -Password $password
    $supplierLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "self-service supplier" -Username $supplierName -Password $password
    $adminLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "self-service admin" -Username $adminName -Password $password

    $session = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/auth/session" -TimeoutSec 10
    if ($session.role -ne "user" -or $session.account_id -ne $userAccountId -or $session.compat_admin -ne $false) {
        Fail "self-service user session mismatch: $(ConvertTo-CompactJson $session)"
    }

    $adminStatus = Get-HttpStatusCode -Context "user calling admin API" -Request {
        Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/admin/accounts" -TimeoutSec 10
    }
    if ($adminStatus -ne 403) {
        Fail "self-service user admin API expected 403, got $adminStatus"
    }

    $supplierScopeStatus = Get-HttpStatusCode -Context "user calling supplier API" -Request {
        Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/supplier/profile" -TimeoutSec 10
    }
    if ($supplierScopeStatus -ne 403) {
        Fail "self-service user supplier API expected 403, got $supplierScopeStatus"
    }

    $groups = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/me/allowed-groups" -TimeoutSec 10
    if (-not $groups.PSObject.Properties[$groupId]) {
        Fail "self-service user allowed groups missing $groupId"
    }

    $mounts = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/me/mount-points" -TimeoutSec 10
    if (-not $mounts.PSObject.Properties[$mount]) {
        Fail "self-service user mount-points missing $mount"
    }

    $plans = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/me/subscription-plans" -TimeoutSec 10
    if (-not $plans.PSObject.Properties[$planId]) {
        Fail "self-service subscription plans missing $planId"
    }
    $purchase = Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
        period = "202606"
        subscription_id = "${prefix}_purchase_sub"
        ledger_id = "${prefix}_purchase_ledger"
        account_id = $supplierAccountId
        operator_note = "self purchase"
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/subscription-plans/$planId/purchase" -TimeoutSec 10
    if ($purchase.account_id -ne $userAccountId -or $purchase.plan_id -ne $planId -or $purchase.ledger_id -ne "${prefix}_purchase_ledger" -or [int64]$purchase.balance_after_cents -ne 3800) {
        Fail "self-service purchase response mismatch: $(ConvertTo-CompactJson $purchase)"
    }
    $subs = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/me/subscriptions" -TimeoutSec 10
    if (-not $subs.PSObject.Properties["${prefix}_purchase_sub"]) {
        Fail "self-service subscriptions missing purchase"
    }
    $overBalanceStatus = Get-HttpStatusCode -Context "self-service purchase insufficient balance" -Request {
        Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
            period = "202606"
            subscription_id = "${prefix}_over_balance_sub"
            ledger_id = "${prefix}_over_balance_ledger"
        } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/subscription-plans/$overBalancePlanId/purchase" -TimeoutSec 10
    }
    if ($overBalanceStatus -ne 409) {
        Fail "self-service insufficient balance expected 409, got $overBalanceStatus"
    }
    $redeemed = Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
        period = "202606"
        account_id = $supplierAccountId
        amount_cents = 999999
        operator_note = "self redeem"
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/redeem-codes/$redeemCode/redeem" -TimeoutSec 10
    if ($redeemed.account_id -ne $userAccountId -or $redeemed.code -ne $redeemCode -or [int64]$redeemed.balance_after_cents -ne 4500) {
        Fail "self-service redeem response mismatch: $(ConvertTo-CompactJson $redeemed)"
    }
    $duplicateRedeemStatus = Get-HttpStatusCode -Context "self-service duplicate redeem" -Request {
        Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{ period = "202606" } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/redeem-codes/$redeemCode/redeem" -TimeoutSec 10
    }
    if ($duplicateRedeemStatus -ne 409) {
        Fail "self-service duplicate redeem expected 409, got $duplicateRedeemStatus"
    }
    $redemptions = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/me/redeem-redemptions" -TimeoutSec 10
    if (-not $redemptions.PSObject.Properties["redeem:${redeemCode}:${userAccountId}"]) {
        Fail "self-service redeem redemptions missing code"
    }

    $badGroupStatus = Get-HttpStatusCode -Context "self-service user ungranted group" -Request {
        $badBody = @{
            access_account_id = "${prefix}_bad_group"
            username = "${prefix}_bad_group_user"
            mount_point_group_id = $otherGroupId
        } | ConvertTo-Json -Compress
        Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body $badBody -Uri "$Base/api/v1/me/access-accounts" -TimeoutSec 10
    }
    if ($badGroupStatus -ne 400) {
        Fail "self-service ungranted group expected 400, got $badGroupStatus"
    }

    $userAccessBody = @{
        access_account_id = $userAccessId
        owner_account_id = $supplierAccountId
        username = "${prefix}_rover"
        kind = "supplier_station"
        password = "rover-pass"
        mount_point_group_id = $groupId
        concurrency_limit = 1
    } | ConvertTo-Json -Compress
    $userAccess = Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body $userAccessBody -Uri "$Base/api/v1/me/access-accounts" -TimeoutSec 10
    if ($userAccess.owner_account_id -ne $userAccountId -or $userAccess.kind -ne "user_client" -or $userAccess.PSObject.Properties["password_hash"]) {
        Fail "self-service user access create mismatch: $(ConvertTo-CompactJson $userAccess)"
    }

    $userAccessList = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/me/access-accounts" -TimeoutSec 10
    if (-not $userAccessList.PSObject.Properties[$userAccessId]) {
        Fail "self-service user access list missing $userAccessId"
    }

    $supplierAccessBody = @{
        access_account_id = $supplierAccessId
        username = "${prefix}_station"
        kind = "user_client"
        password = "station-pass"
        mount_point_group_id = $groupId
        concurrency_limit = 1
    } | ConvertTo-Json -Compress
    $supplierAccess = Invoke-RestMethod -Method Post -Headers $supplierLogin.Headers -ContentType "application/json" -Body $supplierAccessBody -Uri "$Base/api/v1/supplier/access-accounts" -TimeoutSec 10
    if ($supplierAccess.owner_account_id -ne $supplierAccountId -or $supplierAccess.kind -ne "supplier_station") {
        Fail "self-service supplier access create mismatch: $(ConvertTo-CompactJson $supplierAccess)"
    }

    $crossReadStatus = Get-HttpStatusCode -Context "user reading supplier access account" -Request {
        Invoke-RestMethod -Headers $userLogin.Headers -Uri "$Base/api/v1/me/access-accounts/$supplierAccessId" -TimeoutSec 10
    }
    if ($crossReadStatus -ne 404) {
        Fail "self-service cross-owner read expected 404, got $crossReadStatus"
    }

    $updated = Invoke-RestMethod -Method Put -Headers $userLogin.Headers -ContentType "application/json" -Body (@{ owner_account_id = $supplierAccountId; kind = "supplier_station"; status = "disabled"; mount_point_group_id = $groupId; concurrency_limit = 1 } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts/$userAccessId" -TimeoutSec 10
    if ($updated.owner_account_id -ne $userAccountId -or $updated.kind -ne "user_client" -or $updated.status -ne "disabled") {
        Fail "self-service user access update mismatch: $(ConvertTo-CompactJson $updated)"
    }

    Invoke-RestMethod -Method Put -Headers $userLogin.Headers -ContentType "application/json" -Body (@{ password = "rotated-pass" } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts/$userAccessId/password" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Delete -Headers $userLogin.Headers -Uri "$Base/api/v1/me/access-accounts/$userAccessId" -TimeoutSec 10 | Out-Null

    $adminSelfAccess = Invoke-RestMethod -Method Post -Headers $adminLogin.Headers -ContentType "application/json" -Body (@{ access_account_id = "${prefix}_admin_station"; username = "${prefix}_admin_station"; password = "admin-station-pass"; mount_point_group_id = $groupId } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/supplier/access-accounts" -TimeoutSec 10
    if ($adminSelfAccess.owner_account_id -ne $adminAccountId -or $adminSelfAccess.kind -ne "supplier_station") {
        Fail "self-service admin supplier scope mismatch: $(ConvertTo-CompactJson $adminSelfAccess)"
    }
}

function New-WebThreeRoleBrowserFixture {
    param(
        [string]$Base,
        [hashtable]$AdminHeaders
    )

    Say "seeding Web three-role browser smoke fixture"
    $prefix = "nc072_$PID"
    $password = "web-role-pass"
    $userAccountId = "${prefix}_user_acc"
    $supplierAccountId = "${prefix}_supplier_acc"
    $adminAccountId = "${prefix}_admin_acc"
    $userName = "${prefix}_user"
    $supplierName = "${prefix}_supplier"
    $adminName = "${prefix}_admin"
    $groupId = "${prefix}_grp"
    $mount = "${prefix}_MPT"

    foreach ($body in @(
        @{ account_id = $userAccountId; username = $userName; role = "user"; password = $password; balance_cents = 12000; concurrency_limit = 2 },
        @{ account_id = $supplierAccountId; username = $supplierName; role = "supplier"; password = $password; balance_cents = 0; concurrency_limit = 2 },
        @{ account_id = $adminAccountId; username = $adminName; role = "admin"; password = $password; balance_cents = 0; concurrency_limit = 2 }
    )) {
        Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body ($body | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/accounts" -TimeoutSec 10 | Out-Null
    }

    Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        group_id = $groupId
        name = "NC-072 browser group"
        billing_multiplier = 1.0
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-point-groups" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        hourly_price_cents = 100
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-points/$mount" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        mountpoint = $mount
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-point-groups/$groupId/members" -TimeoutSec 10 | Out-Null

    foreach ($accountId in @($userAccountId, $supplierAccountId, $adminAccountId)) {
        Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{
            group_id = $groupId
        } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/accounts/$accountId/group-grants" -TimeoutSec 10 | Out-Null
    }

    $userLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "web browser user" -Username $userName -Password $password
    $supplierLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "web browser supplier" -Username $supplierName -Password $password
    $adminLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "web browser admin" -Username $adminName -Password $password

    Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
        access_account_id = "${prefix}_user_aacc"
        username = "${prefix}_rover"
        password = "web-rover-pass"
        mount_point_group_id = $groupId
        concurrency_limit = 1
        private_remark = "NC-072 browser fixture user access"
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Post -Headers $supplierLogin.Headers -ContentType "application/json" -Body (@{
        access_account_id = "${prefix}_supplier_aacc"
        username = "${prefix}_station"
        password = "web-station-pass"
        mount_point_group_id = $groupId
        concurrency_limit = 1
        private_remark = "NC-072 browser fixture supplier access"
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/supplier/access-accounts" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Post -Headers $adminLogin.Headers -ContentType "application/json" -Body (@{
        access_account_id = "${prefix}_admin_supplier_aacc"
        username = "${prefix}_admin_station"
        password = "web-admin-station-pass"
        mount_point_group_id = $groupId
        concurrency_limit = 1
        private_remark = "NC-072 browser fixture admin supplier access"
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/supplier/access-accounts" -TimeoutSec 10 | Out-Null

    return [pscustomobject]@{
        UserName = $userName
        SupplierName = $supplierName
        AdminName = $adminName
        Password = $password
    }
}

function Wait-WebDevServer {
    param(
        [string]$WebBase,
        [int]$TimeoutSec
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 500
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri $WebBase -TimeoutSec 3
            if ($response.StatusCode -ge 200 -and $response.StatusCode -lt 500) {
                return
            }
        }
        catch {
        }
    } while ((Get-Date) -lt $deadline)

    Fail "Web dev server did not become ready at $WebBase"
}

function Invoke-WebThreeRoleBrowserSmoke {
    param(
        [string]$Base,
        [hashtable]$AdminHeaders
    )

    $webDir = Join-Path $RootPath "web"
    $smokeScript = Join-Path $webDir "scripts\three_role_browser_smoke.mjs"
    if (-not (Test-Path -LiteralPath $smokeScript)) {
        Fail "missing Web browser smoke script: $smokeScript"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $webDir "node_modules"))) {
        Fail "missing web/node_modules. Run npm install in web or link an existing node_modules before browser smoke."
    }

    $nodeCmd = Get-Command node -ErrorAction SilentlyContinue
    if (-not $nodeCmd) {
        Fail "missing node. Web three-role browser smoke requires Node.js."
    }
    $npmCmd = Get-Command npm.cmd -ErrorAction SilentlyContinue
    if (-not $npmCmd) {
        $npmCmd = Get-Command npm -ErrorAction SilentlyContinue
    }
    if (-not $npmCmd) {
        Fail "missing npm. Web three-role browser smoke requires npm to start Vite."
    }

    $fixture = New-WebThreeRoleBrowserFixture -Base $Base -AdminHeaders $AdminHeaders
    $webBase = "http://127.0.0.1:$WebPort"
    $viteStdout = Join-Path $env:TEMP ("navcaster-web-vite-" + [guid]::NewGuid().ToString() + ".out.log")
    $viteStderr = Join-Path $env:TEMP ("navcaster-web-vite-" + [guid]::NewGuid().ToString() + ".err.log")
    $viteProcess = $null

    try {
        Say "starting Vite dev server at $webBase"
        $viteProcess = Start-Process -FilePath $npmCmd.Source `
            -ArgumentList @("run", "dev", "--", "--host", "127.0.0.1", "--port", ([string]$WebPort), "--strictPort") `
            -WorkingDirectory $webDir `
            -WindowStyle Hidden `
            -RedirectStandardOutput $viteStdout `
            -RedirectStandardError $viteStderr `
            -PassThru

        Wait-WebDevServer -WebBase $webBase -TimeoutSec $StartupTimeoutSec

        $env:WEB_BASE_URL = $webBase
        $env:NAVCASTER_API_HOST = $HttpBindAddr
        $env:NAVCASTER_API_PORT = [string]$HttpPort
        $env:NAVCASTER_WEB_ADMIN_USER = $fixture.AdminName
        $env:NAVCASTER_WEB_ADMIN_PASSWORD = $fixture.Password
        $env:NAVCASTER_WEB_USER = $fixture.UserName
        $env:NAVCASTER_WEB_USER_PASSWORD = $fixture.Password
        $env:NAVCASTER_WEB_SUPPLIER = $fixture.SupplierName
        $env:NAVCASTER_WEB_SUPPLIER_PASSWORD = $fixture.Password
        $env:BROWSER_DEBUG_PORT = [string]$BrowserDebugPort

        Say "running Web three-role browser smoke"
        $result = Invoke-NativeCommand "node" @($smokeScript)
        $result.Output | ForEach-Object { Write-Host $_ }
        if ($result.ExitCode -ne 0) {
            Fail "Web three-role browser smoke failed"
        }
    }
    finally {
        foreach ($name in @(
            "WEB_BASE_URL",
            "NAVCASTER_API_HOST",
            "NAVCASTER_API_PORT",
            "NAVCASTER_WEB_ADMIN_USER",
            "NAVCASTER_WEB_ADMIN_PASSWORD",
            "NAVCASTER_WEB_USER",
            "NAVCASTER_WEB_USER_PASSWORD",
            "NAVCASTER_WEB_SUPPLIER",
            "NAVCASTER_WEB_SUPPLIER_PASSWORD",
            "BROWSER_DEBUG_PORT"
        )) {
            Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        }

        try {
            if ($viteProcess -and -not $viteProcess.HasExited) {
                $taskkill = Get-Command taskkill.exe -ErrorAction SilentlyContinue
                if ($taskkill) {
                    Invoke-NativeCommand $taskkill.Source @("/F", "/T", "/PID", ([string]$viteProcess.Id)) | Out-Null
                }
                else {
                    Stop-Process -Id $viteProcess.Id -Force
                    if (-not $viteProcess.WaitForExit(5000)) {
                        Write-Warning "Vite dev server did not exit within 5 seconds"
                    }
                }
            }
        }
        catch {
            Write-Warning "failed to stop Vite dev server: $($_.Exception.Message)"
        }

        try {
            Remove-Item -LiteralPath $viteStdout -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $viteStderr -Force -ErrorAction SilentlyContinue
        }
        catch {
        }
    }
}

function Get-E2eRuntimePeriod {
    return (Get-Date).ToUniversalTime().ToString("yyyyMM")
}

function Wait-AccessRuntimeOnlineSession {
    param(
        [string]$OwnerAccountId,
        [string]$AccessAccountId,
        [string]$AccessUsername,
        [string]$Mount,
        [int]$TimeoutSec,
        [string]$Context
    )

    $key = "ONLINE:SESSION:$OwnerAccountId"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $sessions = Get-RedisHashMap $key
        foreach ($field in $sessions.Keys) {
            try {
                $record = $sessions[$field] | ConvertFrom-Json
                if ($record.access_account_id -eq $AccessAccountId -and
                    $record.access_username -eq $AccessUsername -and
                    $record.mountpoint -eq $Mount) {
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
    Fail "$Context access runtime online session did not appear in $key; raw=[$raw]"
}

function Wait-AccessRuntimeOnlineSessionGone {
    param(
        [string]$Key,
        [string]$Field,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $raw = Invoke-RedisCommand HGET $Key $Field
        if ([string]::IsNullOrEmpty($raw)) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGET $Key $Field)
    Fail "$Context access runtime online session did not disappear; key=$Key field=$Field raw=[$raw]"
}

function Wait-AccessRuntimeBillingEntry {
    param(
        [string]$AccountId,
        [string]$AccessAccountId,
        [string]$Mount,
        [string]$Period,
        [int]$TimeoutSec,
        [string]$Context
    )

    $key = "BILL:ENTRY:$Period"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $entries = Get-RedisHashMap $key
        foreach ($field in $entries.Keys) {
            try {
                $record = $entries[$field] | ConvertFrom-Json
                if ($record.account_id -eq $AccountId -and
                    $record.access_account_id -eq $AccessAccountId -and
                    $record.mountpoint -eq $Mount) {
                    return [pscustomobject]@{
                        Key = $key
                        Field = $field
                        Record = $record
                        Raw = $entries[$field]
                    }
                }
            }
            catch {
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL $key)
    Fail "$Context billing entry missing from $key; raw=[$raw]"
}

function Wait-AccessRuntimeLedgerEntry {
    param(
        [string]$AccountId,
        [string]$AccessAccountId,
        [string]$BillingId,
        [string]$Period,
        [int]$TimeoutSec,
        [string]$Context
    )

    $key = "ACC:BALANCE:LEDGER:$Period"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $entries = Get-RedisHashMap $key
        foreach ($field in $entries.Keys) {
            try {
                $record = $entries[$field] | ConvertFrom-Json
                if ($record.account_id -eq $AccountId -and
                    $record.access_account_id -eq $AccessAccountId -and
                    $record.billing_id -eq $BillingId) {
                    return [pscustomobject]@{
                        Key = $key
                        Field = $field
                        Record = $record
                        Raw = $entries[$field]
                    }
                }
            }
            catch {
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL $key)
    Fail "$Context ledger entry missing from $key; raw=[$raw]"
}

function Wait-AccessRuntimeSupplyUsage {
    param(
        [string]$SupplierAccountId,
        [string]$AccessAccountId,
        [string]$Mount,
        [string]$Period,
        [int]$TimeoutSec,
        [string]$Context
    )

    $key = "SUPPLY:USAGE:$Period"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $entries = Get-RedisHashMap $key
        foreach ($field in $entries.Keys) {
            try {
                $record = $entries[$field] | ConvertFrom-Json
                if ($record.supplier_account_id -eq $SupplierAccountId -and
                    $record.access_account_id -eq $AccessAccountId -and
                    $record.mountpoint -eq $Mount) {
                    return [pscustomobject]@{
                        Key = $key
                        Field = $field
                        Record = $record
                        Raw = $entries[$field]
                    }
                }
            }
            catch {
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL $key)
    Fail "$Context supply usage missing from $key; raw=[$raw]"
}

function Wait-AccessRuntimeStationRecord {
    param(
        [string]$SupplierAccountId,
        [string]$AccessAccountId,
        [string]$Mount,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $raw = Invoke-RedisCommand HGET "STATION:RECORD" $Mount
        if (-not [string]::IsNullOrWhiteSpace($raw)) {
            try {
                $record = $raw | ConvertFrom-Json
                if ($record.mountpoint -eq $Mount -and
                    $record.last_supplier_account_id -eq $SupplierAccountId -and
                    $record.last_access_account_id -eq $AccessAccountId) {
                    return [pscustomobject]@{
                        Key = "STATION:RECORD"
                        Field = $Mount
                        Record = $record
                        Raw = $raw
                    }
                }
            }
            catch {
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGET "STATION:RECORD" $Mount)
    Fail "$Context station record missing for $Mount; raw=[$raw]"
}

function Wait-AccessRuntimeStationEvents {
    param(
        [string]$SupplierAccountId,
        [string]$AccessAccountId,
        [string]$Mount,
        [int]$TimeoutSec,
        [string]$Context
    )

    $key = "STATION:EVENT:$Mount"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $raw = Invoke-RedisCommand LRANGE $key 0 -1
        $hasLogin = $false
        $hasDisconnect = $false
        foreach ($line in @($raw -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })) {
            try {
                $record = $line | ConvertFrom-Json
                if ($record.supplier_account_id -eq $SupplierAccountId -and
                    $record.access_account_id -eq $AccessAccountId -and
                    $record.mountpoint -eq $Mount) {
                    if ($record.event_type -eq "login") {
                        $hasLogin = $true
                    }
                    elseif ($record.event_type -eq "disconnect") {
                        $hasDisconnect = $true
                    }
                }
            }
            catch {
            }
        }
        if ($hasLogin -and $hasDisconnect) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand LRANGE $key 0 -1)
    Fail "$Context station events missing login/disconnect in $key; raw=[$raw]"
}

function Get-RedisJsonHashField {
    param(
        [string]$Key,
        [string]$Field,
        [string]$Context
    )

    $raw = Invoke-RedisCommand HGET $Key $Field
    if ([string]::IsNullOrWhiteSpace($raw)) {
        Fail "$Context missing Redis hash field $Key/$Field"
    }
    try {
        return ($raw | ConvertFrom-Json)
    }
    catch {
        Fail "$Context Redis hash field $Key/$Field is not JSON: $(Format-DebugText $raw)"
    }
}

function Count-AccessRuntimeBillingEntries {
    param(
        [string]$AccessAccountId
    )

    $count = 0
    foreach ($key in (Get-RedisKeys "BILL:ENTRY:*")) {
        $entries = Get-RedisHashMap $key
        foreach ($field in $entries.Keys) {
            try {
                $record = $entries[$field] | ConvertFrom-Json
                if ($record.access_account_id -eq $AccessAccountId) {
                    $count++
                }
            }
            catch {
            }
        }
    }
    return $count
}

function Wait-AccessRuntimeRejection {
    param(
        [string]$Period,
        [string]$OwnerAccountId,
        [string]$AccessAccountId,
        [string]$AccessUsername,
        [string]$ExpectedReason,
        [int]$TimeoutSec,
        [string]$Context
    )

    $key = "RUNTIME:REJECTION:$Period"
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $records = Get-RedisHashMap $key
        foreach ($field in $records.Keys) {
            try {
                $record = $records[$field] | ConvertFrom-Json
                if ($record.owner_account_id -eq $OwnerAccountId -and
                    $record.access_account_id -eq $AccessAccountId -and
                    $record.access_username -eq $AccessUsername -and
                    $record.reason -eq $ExpectedReason) {
                    return [pscustomobject]@{
                        Key = $key
                        Field = $field
                        Record = $record
                        Raw = $records[$field]
                    }
                }
            }
            catch {
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL $key)
    Fail "$Context runtime rejection missing from $key; expectedReason=$ExpectedReason raw=[$raw]"
}

function Get-AccessRuntimeBillingEntries {
    param(
        [string]$Period
    )

    $key = "BILL:ENTRY:$Period"
    $result = @()
    $entries = Get-RedisHashMap $key
    foreach ($field in $entries.Keys) {
        try {
            $record = $entries[$field] | ConvertFrom-Json
            $result += [pscustomobject]@{
                Key = $key
                Field = $field
                Record = $record
                Raw = $entries[$field]
            }
        }
        catch {
        }
    }
    return @($result)
}

function Get-AccessRuntimeBillingIds {
    param(
        [string]$AccessAccountId,
        [string]$Period
    )

    $ids = @{}
    foreach ($entry in @(Get-AccessRuntimeBillingEntries $Period)) {
        $record = $entry.Record
        if ($record.access_account_id -eq $AccessAccountId) {
            $billingId = [string]$record.billing_id
            if (-not [string]::IsNullOrWhiteSpace($billingId)) {
                $ids[$billingId] = $true
            }
        }
    }
    return $ids
}

function Wait-AccessRuntimeNewBillingEntry {
    param(
        [string]$AccountId,
        [string]$AccessAccountId,
        [string]$Mount,
        [string]$Period,
        [hashtable]$KnownBillingIds,
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        foreach ($entry in @(Get-AccessRuntimeBillingEntries $Period)) {
            $record = $entry.Record
            $billingId = [string]$record.billing_id
            if ($record.account_id -eq $AccountId -and
                $record.access_account_id -eq $AccessAccountId -and
                $record.mountpoint -eq $Mount -and
                -not [string]::IsNullOrWhiteSpace($billingId) -and
                ($null -eq $KnownBillingIds -or -not $KnownBillingIds.ContainsKey($billingId))) {
                return $entry
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $key = "BILL:ENTRY:$Period"
    $raw = Format-DebugText (Invoke-RedisCommand HGETALL $key)
    Fail "$Context new billing entry missing from $key; raw=[$raw]"
}

function Test-AccessRuntimeLedgerEntryExists {
    param(
        [string]$BillingId,
        [string]$Period
    )

    $key = "ACC:BALANCE:LEDGER:$Period"
    $entries = Get-RedisHashMap $key
    foreach ($field in $entries.Keys) {
        try {
            $record = $entries[$field] | ConvertFrom-Json
            if ($record.billing_id -eq $BillingId) {
                return $true
            }
        }
        catch {
        }
    }
    return $false
}

function Invoke-AccessRuntimeBalanceAdjustment {
    param(
        [string]$Base,
        [hashtable]$AdminHeaders,
        [string]$AccountId,
        [int64]$DeltaCents,
        [string]$LedgerId,
        [string]$Period,
        [string]$Source
    )

    $body = @{
        ledger_id = $LedgerId
        period = $Period
        delta_cents = $DeltaCents
        source = $Source
    } | ConvertTo-Json -Compress
    return Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body $body -Uri "$Base/api/v1/admin/accounts/$AccountId/balance-adjustments" -TimeoutSec 10
}

function New-AccessRuntimeUserClientSeed {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Prefix,
        [string]$Mount,
        [string]$AccessAccountId,
        [string]$AccessUsername,
        [string]$AccessPassword,
        [string]$GroupId
    )

    Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body (@{
        access_account_id = $AccessAccountId
        username = $AccessUsername
        password = $AccessPassword
        mount_point_group_id = $GroupId
        concurrency_limit = 1
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts" -TimeoutSec 10 | Out-Null

    return [pscustomobject]@{
        Prefix = $Prefix
        Mount = $Mount
        Account = $AccessUsername
        Password = $AccessPassword
    }
}

function Assert-AccessRuntimeForcedDisconnect {
    param(
        [pscustomobject]$Seed,
        [string]$OwnerAccountId,
        [string]$AccessAccountId,
        [string]$AccessUsername,
        [string]$Mount,
        [string]$Period,
        [string]$NtripHost,
        [int]$NtripPort,
        [scriptblock]$Mutation,
        [string]$Label,
        [string]$ExpectedReason,
        [string]$ExpectedBillingMode = "",
        [string]$ExpectedSubscriptionId = "",
        [bool]$ExpectBilling = $true,
        [bool]$ExpectPositiveDebit = $false,
        [bool]$ExpectZeroDebit = $false,
        [int64]$ExpectedBalanceCents = ([int64]::MinValue)
    )

    $knownBillingIds = Get-AccessRuntimeBillingIds $AccessAccountId $Period
    $connection = $null
    $online = $null
    try {
        $connection = Open-NtripClientForSeed $Seed $NtripHost $NtripPort $Label
        $online = Wait-AccessRuntimeOnlineSession $OwnerAccountId $AccessAccountId $AccessUsername $Mount $StartupTimeoutSec $Label
        if (-not [string]::IsNullOrWhiteSpace($ExpectedBillingMode) -and $online.Record.billing_mode -ne $ExpectedBillingMode) {
            Fail "$Label online session billing mode mismatch: expected=$ExpectedBillingMode actual=$($online.Record.billing_mode) record=$(ConvertTo-CompactJson $online.Record)"
        }
        if (-not [string]::IsNullOrWhiteSpace($ExpectedSubscriptionId) -and $online.Record.subscription_id -ne $ExpectedSubscriptionId) {
            Fail "$Label online session subscription mismatch: expected=$ExpectedSubscriptionId actual=$($online.Record.subscription_id) record=$(ConvertTo-CompactJson $online.Record)"
        }

        Start-Sleep -Seconds 2
        $mutationError = ""
        try {
            & $Mutation
        }
        catch {
            $mutationError = $_.Exception.Message
        }
        Wait-NtripTcpConnectionClosed $connection $StartupTimeoutSec $Label
        Wait-AccessRuntimeOnlineSessionGone $online.Key $online.Field $StartupTimeoutSec $Label
    }
    finally {
        Close-NtripTcpConnection $connection
    }

    if (-not $ExpectBilling) {
        return $null
    }

    $billing = Wait-AccessRuntimeNewBillingEntry $OwnerAccountId $AccessAccountId $Mount $Period $knownBillingIds $StartupTimeoutSec $Label
    if ([int64]$billing.Record.used_seconds -le 0) {
        Fail "$Label billing entry did not carry positive usage: $(ConvertTo-CompactJson $billing.Record)"
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedReason) -and $billing.Record.disconnect_reason -ne $ExpectedReason) {
        Fail "$Label billing disconnect reason mismatch: expected=$ExpectedReason actual=$($billing.Record.disconnect_reason) billing=$(ConvertTo-CompactJson $billing.Record)"
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedBillingMode) -and $billing.Record.billing_mode -ne $ExpectedBillingMode) {
        Fail "$Label billing mode mismatch: expected=$ExpectedBillingMode actual=$($billing.Record.billing_mode) billing=$(ConvertTo-CompactJson $billing.Record)"
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedSubscriptionId) -and $billing.Record.subscription_id -ne $ExpectedSubscriptionId) {
        Fail "$Label billing subscription mismatch: expected=$ExpectedSubscriptionId actual=$($billing.Record.subscription_id) billing=$(ConvertTo-CompactJson $billing.Record)"
    }
    if ($ExpectPositiveDebit -and [int64]$billing.Record.actual_debit_cents -le 0) {
        Fail "$Label billing entry did not carry positive debit: $(ConvertTo-CompactJson $billing.Record)"
    }
    if ($ExpectZeroDebit -and [int64]$billing.Record.actual_debit_cents -ne 0) {
        Fail "$Label billing entry should not debit balance: $(ConvertTo-CompactJson $billing.Record)"
    }

    if ([int64]$billing.Record.actual_debit_cents -gt 0) {
        $ledger = Wait-AccessRuntimeLedgerEntry $OwnerAccountId $AccessAccountId $billing.Record.billing_id $Period $StartupTimeoutSec $Label
        if ([int64]$ledger.Record.delta_cents -ne -[int64]$billing.Record.actual_debit_cents) {
            Fail "$Label ledger delta mismatch: billing=$(ConvertTo-CompactJson $billing.Record) ledger=$(ConvertTo-CompactJson $ledger.Record)"
        }
    }
    elseif (Test-AccessRuntimeLedgerEntryExists $billing.Record.billing_id $Period) {
        Fail "$Label zero-debit billing unexpectedly created a balance ledger entry: $(ConvertTo-CompactJson $billing.Record)"
    }

    if ($ExpectedBalanceCents -ne [int64]::MinValue) {
        $ownerRecord = Get-RedisJsonHashField "ACC:RECORD" $OwnerAccountId "$Label owner balance"
        if ([int64]$ownerRecord.balance_cents -ne $ExpectedBalanceCents) {
            Fail "$Label owner balance changed unexpectedly: expected=$ExpectedBalanceCents actual=$($ownerRecord.balance_cents) owner=$(ConvertTo-CompactJson $ownerRecord)"
        }
    }

    return $billing
}

function Assert-AccessRuntimeRejected {
    param(
        [pscustomobject]$Seed,
        [string]$Mount,
        [string]$NtripHost,
        [int]$NtripPort,
        [string]$Label,
        [string]$OwnerAccountId,
        [string]$AccessAccountId,
        [string]$AccessUsername,
        [string]$ExpectedRejectionReason = "",
        [string]$Period = "",
        [int64]$ExpectedLimit = ([int64]::MinValue),
        [int64]$ExpectedCurrentCount = ([int64]::MinValue)
    )

    $before = Count-AccessRuntimeBillingEntries $AccessAccountId
    $onlineKey = "ONLINE:SESSION:$OwnerAccountId"
    $beforeOnlineFields = @{}
    $beforeOnline = Get-RedisHashMap $onlineKey
    foreach ($field in $beforeOnline.Keys) {
        try {
            $record = $beforeOnline[$field] | ConvertFrom-Json
            if ($record.access_account_id -eq $AccessAccountId -or $record.access_username -eq $AccessUsername) {
                $beforeOnlineFields[$field] = $true
            }
        }
        catch {
        }
    }

    $connection = Open-NtripClientForMount $Seed $Mount $NtripHost $NtripPort $Label -AllowRejected
    Close-NtripTcpConnection $connection
    Start-Sleep -Milliseconds 800

    $online = Get-RedisHashMap $onlineKey
    foreach ($field in $online.Keys) {
        try {
            $record = $online[$field] | ConvertFrom-Json
            if (($record.access_account_id -eq $AccessAccountId -or $record.access_username -eq $AccessUsername) -and
                -not $beforeOnlineFields.ContainsKey($field)) {
                Fail "$Label created rejected online session in $onlineKey field=$field"
            }
        }
        catch {
        }
    }
    $after = Count-AccessRuntimeBillingEntries $AccessAccountId
    if ($after -ne $before) {
        Fail "$Label unexpectedly created billing entry after rejection; before=$before after=$after"
    }

    if (-not [string]::IsNullOrWhiteSpace($ExpectedRejectionReason)) {
        if ([string]::IsNullOrWhiteSpace($Period)) {
            $Period = Get-E2eRuntimePeriod
        }
        $rejection = Wait-AccessRuntimeRejection $Period $OwnerAccountId $AccessAccountId $AccessUsername $ExpectedRejectionReason $StartupTimeoutSec $Label
        if ($ExpectedLimit -ne [int64]::MinValue -and [int64]$rejection.Record.limit -ne $ExpectedLimit) {
            Fail "$Label rejection limit mismatch: expected=$ExpectedLimit actual=$($rejection.Record.limit) record=$(ConvertTo-CompactJson $rejection.Record)"
        }
        if ($ExpectedCurrentCount -ne [int64]::MinValue -and [int64]$rejection.Record.current_count -ne $ExpectedCurrentCount) {
            Fail "$Label rejection current_count mismatch: expected=$ExpectedCurrentCount actual=$($rejection.Record.current_count) record=$(ConvertTo-CompactJson $rejection.Record)"
        }
        return $rejection
    }
}

function Invoke-AccessRuntimeEnforcementSmoke {
    param(
        [string]$Base,
        [hashtable]$AdminHeaders,
        [string]$NtripHost,
        [int]$NtripPort
    )

    Say "validating access runtime enforcement over AACC-backed NTRIP sessions"
    $prefix = "nc055_$PID"
    $userAccountId = "${prefix}_user_acc"
    $supplierAccountId = "${prefix}_supplier_acc"
    $blockedAccountId = "${prefix}_blocked_acc"
    $lowBalanceAccountId = "${prefix}_runtime_low_acc"
    $subRevokedAccountId = "${prefix}_runtime_sub_rev_acc"
    $subExpiredAccountId = "${prefix}_runtime_sub_exp_acc"
    $userName = "${prefix}_user"
    $supplierName = "${prefix}_supplier"
    $blockedName = "${prefix}_blocked"
    $lowBalanceName = "${prefix}_runtime_low_user"
    $subRevokedName = "${prefix}_runtime_sub_rev_user"
    $subExpiredName = "${prefix}_runtime_sub_exp_user"
    $password = "runtime-pass"
    $groupId = "${prefix}_grp"
    $mount = "${prefix}_MPT"
    $otherMount = "${prefix}_OTHER"
    $userAccessId = "${prefix}_user_aacc"
    $supplierAccessId = "${prefix}_supplier_aacc"
    $blockedAccessId = "${prefix}_blocked_aacc"
    $disableRunningAccessId = "${prefix}_runtime_disable_aacc"
    $lowBalanceAccessId = "${prefix}_runtime_low_aacc"
    $subRevokedAccessId = "${prefix}_runtime_sub_rev_aacc"
    $subExpiredAccessId = "${prefix}_runtime_sub_exp_aacc"
    $userAccessName = "${prefix}_rover"
    $supplierAccessName = "${prefix}_station"
    $blockedAccessName = "${prefix}_blocked_rover"
    $disableRunningAccessName = "${prefix}_runtime_disable_rover"
    $lowBalanceAccessName = "${prefix}_runtime_low_rover"
    $subRevokedAccessName = "${prefix}_runtime_sub_rev_rover"
    $subExpiredAccessName = "${prefix}_runtime_sub_exp_rover"
    $userAccessPassword = "runtime-rover-pass"
    $supplierAccessPassword = "runtime-station-pass"
    $blockedAccessPassword = "runtime-blocked-pass"
    $disableRunningAccessPassword = "runtime-disable-pass"
    $lowBalanceAccessPassword = "runtime-low-pass"
    $subRevokedAccessPassword = "runtime-sub-rev-pass"
    $subExpiredAccessPassword = "runtime-sub-exp-pass"
    $subRevokedSubscriptionId = "${prefix}_runtime_sub_rev"
    $subExpiredSubscriptionId = "${prefix}_runtime_sub_exp"
    $subRevokedInitialBalance = 7000
    $subExpiredInitialBalance = 8000
    $period = Get-E2eRuntimePeriod

    foreach ($body in @(
        @{ account_id = $userAccountId; username = $userName; role = "user"; password = $password; balance_cents = 20000; credit_limit_cents = 0; concurrency_limit = 2 },
        @{ account_id = $supplierAccountId; username = $supplierName; role = "supplier"; password = $password; balance_cents = 0; credit_limit_cents = 0; concurrency_limit = 2 },
        @{ account_id = $blockedAccountId; username = $blockedName; role = "user"; password = $password; balance_cents = 0; credit_limit_cents = 0; concurrency_limit = 1 },
        @{ account_id = $lowBalanceAccountId; username = $lowBalanceName; role = "user"; password = $password; balance_cents = 20000; credit_limit_cents = 0; concurrency_limit = 1 },
        @{ account_id = $subRevokedAccountId; username = $subRevokedName; role = "user"; password = $password; balance_cents = $subRevokedInitialBalance; credit_limit_cents = 0; concurrency_limit = 1 },
        @{ account_id = $subExpiredAccountId; username = $subExpiredName; role = "user"; password = $password; balance_cents = $subExpiredInitialBalance; credit_limit_cents = 0; concurrency_limit = 1 }
    )) {
        Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body ($body | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/accounts" -TimeoutSec 10 | Out-Null
    }

    Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body (@{ group_id = $groupId; name = "NC-055 runtime group"; billing_multiplier = 1.0 } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-point-groups" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{ hourly_price_cents = 3600 } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-points/$mount" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{ hourly_price_cents = 3600 } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-points/$otherMount" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{ mountpoint = $mount } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/mount-point-groups/$groupId/members" -TimeoutSec 10 | Out-Null
    foreach ($accountId in @($userAccountId, $supplierAccountId, $blockedAccountId, $lowBalanceAccountId, $subRevokedAccountId, $subExpiredAccountId)) {
        Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{ group_id = $groupId } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/accounts/$accountId/group-grants" -TimeoutSec 10 | Out-Null
    }

    $subscriptionFutureExpire = ([DateTimeOffset]::UtcNow).ToUnixTimeSeconds() + 3600
    Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        subscription_id = $subRevokedSubscriptionId
        account_id = $subRevokedAccountId
        group_ids = @($groupId)
        status = "active"
        expire_time = $subscriptionFutureExpire
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscriptions" -TimeoutSec 10 | Out-Null
    Invoke-RestMethod -Method Post -Headers $AdminHeaders -ContentType "application/json" -Body (@{
        subscription_id = $subExpiredSubscriptionId
        account_id = $subExpiredAccountId
        group_ids = @($groupId)
        status = "active"
        expire_time = $subscriptionFutureExpire
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscriptions" -TimeoutSec 10 | Out-Null

    $userLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "runtime user" -Username $userName -Password $password
    $supplierLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "runtime supplier" -Username $supplierName -Password $password
    $blockedLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "runtime blocked user" -Username $blockedName -Password $password
    $lowBalanceLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "runtime low balance user" -Username $lowBalanceName -Password $password
    $subRevokedLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "runtime subscription revoked user" -Username $subRevokedName -Password $password
    $subExpiredLogin = Invoke-E2eLoginWithHeaders -Base $Base -Context "runtime subscription expired user" -Username $subExpiredName -Password $password

    Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
        access_account_id = $userAccessId
        username = $userAccessName
        password = $userAccessPassword
        mount_point_group_id = $groupId
        concurrency_limit = 1
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts" -TimeoutSec 10 | Out-Null

    Invoke-RestMethod -Method Post -Headers $supplierLogin.Headers -ContentType "application/json" -Body (@{
        access_account_id = $supplierAccessId
        username = $supplierAccessName
        password = $supplierAccessPassword
        mount_point_group_id = $groupId
        concurrency_limit = 1
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/supplier/access-accounts" -TimeoutSec 10 | Out-Null

    Invoke-RestMethod -Method Post -Headers $blockedLogin.Headers -ContentType "application/json" -Body (@{
        access_account_id = $blockedAccessId
        username = $blockedAccessName
        password = $blockedAccessPassword
        mount_point_group_id = $groupId
        concurrency_limit = 1
    } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts" -TimeoutSec 10 | Out-Null

    $disableRunningSeed = New-AccessRuntimeUserClientSeed `
        -Base $Base `
        -Headers $userLogin.Headers `
        -Prefix $prefix `
        -Mount $mount `
        -AccessAccountId $disableRunningAccessId `
        -AccessUsername $disableRunningAccessName `
        -AccessPassword $disableRunningAccessPassword `
        -GroupId $groupId
    $lowBalanceSeed = New-AccessRuntimeUserClientSeed `
        -Base $Base `
        -Headers $lowBalanceLogin.Headers `
        -Prefix $prefix `
        -Mount $mount `
        -AccessAccountId $lowBalanceAccessId `
        -AccessUsername $lowBalanceAccessName `
        -AccessPassword $lowBalanceAccessPassword `
        -GroupId $groupId
    $subRevokedSeed = New-AccessRuntimeUserClientSeed `
        -Base $Base `
        -Headers $subRevokedLogin.Headers `
        -Prefix $prefix `
        -Mount $mount `
        -AccessAccountId $subRevokedAccessId `
        -AccessUsername $subRevokedAccessName `
        -AccessPassword $subRevokedAccessPassword `
        -GroupId $groupId
    $subExpiredSeed = New-AccessRuntimeUserClientSeed `
        -Base $Base `
        -Headers $subExpiredLogin.Headers `
        -Prefix $prefix `
        -Mount $mount `
        -AccessAccountId $subExpiredAccessId `
        -AccessUsername $subExpiredAccessName `
        -AccessPassword $subExpiredAccessPassword `
        -GroupId $groupId

    $sourceSeed = [pscustomobject]@{
        Prefix = $prefix
        Mount = $mount
        SourceUser = $supplierAccessName
        SourcePassword = $supplierAccessPassword
        Account = $supplierAccessName
        Password = $supplierAccessPassword
        SourceConnectKey = ""
    }
    $clientSeed = [pscustomobject]@{
        Prefix = $prefix
        Mount = $mount
        Account = $userAccessName
        Password = $userAccessPassword
    }
    $blockedSeed = [pscustomobject]@{
        Prefix = $prefix
        Mount = $mount
        Account = $blockedAccessName
        Password = $blockedAccessPassword
    }

    $sourceConnection = $null
    $clientConnection = $null
    try {
        $sourceConnection = Open-NtripSourceForSeed $sourceSeed $NtripHost $NtripPort "runtime-source"
        $sourceOnline = Wait-AccessRuntimeOnlineSession $supplierAccountId $supplierAccessId $supplierAccessName $mount $StartupTimeoutSec "runtime source"

        $clientConnection = Open-NtripClientForSeed $clientSeed $NtripHost $NtripPort "runtime-client"
        $clientOnline = Wait-AccessRuntimeOnlineSession $userAccountId $userAccessId $userAccessName $mount $StartupTimeoutSec "runtime client"
        if ($clientOnline.Record.kind -ne "user_client" -or $sourceOnline.Record.kind -ne "supplier_station") {
            Fail "access runtime online kind mismatch: client=$(ConvertTo-CompactJson $clientOnline.Record) source=$(ConvertTo-CompactJson $sourceOnline.Record)"
        }

        $concurrencyRejection = Assert-AccessRuntimeRejected `
            -Seed $clientSeed `
            -Mount $mount `
            -NtripHost $NtripHost `
            -NtripPort $NtripPort `
            -Label "runtime access concurrency" `
            -OwnerAccountId $userAccountId `
            -AccessAccountId $userAccessId `
            -AccessUsername $userAccessName `
            -ExpectedRejectionReason "access_concurrency_exceeded" `
            -Period $period `
            -ExpectedLimit 1 `
            -ExpectedCurrentCount 1
        $adminRuntimeRejections = Invoke-RestMethod -Method Get -Headers $AdminHeaders -Uri "$Base/api/v1/admin/runtime-rejections?period=$period" -TimeoutSec 10
        if ((ConvertTo-CompactJson $adminRuntimeRejections) -notmatch [regex]::Escape($concurrencyRejection.Field)) {
            Fail "runtime admin rejection API did not expose concurrency rejection $($concurrencyRejection.Field): $(ConvertTo-CompactJson $adminRuntimeRejections)"
        }

        Write-NtripPayload $sourceConnection "NC055_PAYLOAD`r`n" "runtime source"
        Wait-NtripPayload $clientConnection "NC055_PAYLOAD`r`n" 10 "runtime client"
        Start-Sleep -Seconds 2

        Close-NtripTcpConnection $clientConnection
        $clientConnection = $null
        Wait-AccessRuntimeOnlineSessionGone $clientOnline.Key $clientOnline.Field $StartupTimeoutSec "runtime client"
        $billing = Wait-AccessRuntimeBillingEntry $userAccountId $userAccessId $mount $period $StartupTimeoutSec "runtime client"
        if ([int64]$billing.Record.used_seconds -le 0 -or [int64]$billing.Record.actual_debit_cents -le 0) {
            Fail "runtime billing entry did not carry positive usage/debit: $(ConvertTo-CompactJson $billing.Record)"
        }
        $ledger = Wait-AccessRuntimeLedgerEntry $userAccountId $userAccessId $billing.Record.billing_id $period $StartupTimeoutSec "runtime client"
        if ([int64]$ledger.Record.delta_cents -ne -[int64]$billing.Record.actual_debit_cents) {
            Fail "runtime ledger delta mismatch: billing=$(ConvertTo-CompactJson $billing.Record) ledger=$(ConvertTo-CompactJson $ledger.Record)"
        }

        $ownerRecord = Get-RedisJsonHashField "ACC:RECORD" $userAccountId "runtime client owner balance"
        if ([int64]$ownerRecord.balance_cents -ge 20000) {
            Fail "runtime client owner balance was not decremented: $(ConvertTo-CompactJson $ownerRecord)"
        }
        $activeIndex = Get-RedisJsonHashField "AACC:ACTIVE" $userAccessName "runtime client active index balance"
        if ([int64]$activeIndex.balance_cents -ne [int64]$ownerRecord.balance_cents) {
            Fail "runtime AACC active balance snapshot mismatch: owner=$(ConvertTo-CompactJson $ownerRecord) active=$(ConvertTo-CompactJson $activeIndex)"
        }

        Assert-AccessRuntimeForcedDisconnect `
            -Seed $lowBalanceSeed `
            -OwnerAccountId $lowBalanceAccountId `
            -AccessAccountId $lowBalanceAccessId `
            -AccessUsername $lowBalanceAccessName `
            -Mount $mount `
            -Period $period `
            -NtripHost $NtripHost `
            -NtripPort $NtripPort `
            -Mutation {
                Invoke-AccessRuntimeBalanceAdjustment `
                    -Base $Base `
                    -AdminHeaders $AdminHeaders `
                    -AccountId $lowBalanceAccountId `
                    -DeltaCents (-19941) `
                    -LedgerId "${prefix}_runtime_low_force_59" `
                    -Period $period `
                    -Source "e2e_runtime_balance_revalidation" | Out-Null
            } `
            -Label "runtime running balance insufficient" `
            -ExpectedReason "balance_insufficient" `
            -ExpectedBillingMode "payg" `
            -ExpectPositiveDebit $true | Out-Null

        Assert-AccessRuntimeForcedDisconnect `
            -Seed $subRevokedSeed `
            -OwnerAccountId $subRevokedAccountId `
            -AccessAccountId $subRevokedAccessId `
            -AccessUsername $subRevokedAccessName `
            -Mount $mount `
            -Period $period `
            -NtripHost $NtripHost `
            -NtripPort $NtripPort `
            -Mutation {
                Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{
                    status = "disabled"
                } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscriptions/$subRevokedSubscriptionId" -TimeoutSec 10 | Out-Null
            } `
            -Label "runtime running subscription disabled" `
            -ExpectedReason "subscription_revoked" `
            -ExpectedBillingMode "subscription" `
            -ExpectedSubscriptionId $subRevokedSubscriptionId `
            -ExpectZeroDebit $true `
            -ExpectedBalanceCents $subRevokedInitialBalance | Out-Null

        $expiredAt = ([DateTimeOffset]::UtcNow).ToUnixTimeSeconds() - 1
        Assert-AccessRuntimeForcedDisconnect `
            -Seed $subExpiredSeed `
            -OwnerAccountId $subExpiredAccountId `
            -AccessAccountId $subExpiredAccessId `
            -AccessUsername $subExpiredAccessName `
            -Mount $mount `
            -Period $period `
            -NtripHost $NtripHost `
            -NtripPort $NtripPort `
            -Mutation {
                Invoke-RestMethod -Method Put -Headers $AdminHeaders -ContentType "application/json" -Body (@{
                    expire_time = $expiredAt
                } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/subscriptions/$subExpiredSubscriptionId" -TimeoutSec 10 | Out-Null
            } `
            -Label "runtime running subscription expired" `
            -ExpectedReason "subscription_expired" `
            -ExpectedBillingMode "subscription" `
            -ExpectedSubscriptionId $subExpiredSubscriptionId `
            -ExpectZeroDebit $true `
            -ExpectedBalanceCents $subExpiredInitialBalance | Out-Null

        Assert-AccessRuntimeForcedDisconnect `
            -Seed $disableRunningSeed `
            -OwnerAccountId $userAccountId `
            -AccessAccountId $disableRunningAccessId `
            -AccessUsername $disableRunningAccessName `
            -Mount $mount `
            -Period $period `
            -NtripHost $NtripHost `
            -NtripPort $NtripPort `
            -Mutation {
                Invoke-RestMethod -Method Put -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
                    status = "disabled"
                    mount_point_group_id = $groupId
                    concurrency_limit = 1
                } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts/$disableRunningAccessId" -TimeoutSec 10 | Out-Null
            } `
            -Label "runtime running access disabled" `
            -ExpectedReason "access_account_disabled" `
            -ExpectedBillingMode "payg" `
            -ExpectPositiveDebit $true | Out-Null

        Assert-AccessRuntimeRejected $clientSeed $otherMount $NtripHost $NtripPort "runtime unauthorized mount" $userAccountId $userAccessId $userAccessName

        Invoke-RestMethod -Method Put -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
            status = "disabled"
            mount_point_group_id = $groupId
            concurrency_limit = 1
        } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/me/access-accounts/$userAccessId" -TimeoutSec 10 | Out-Null
        Assert-AccessRuntimeRejected $clientSeed $mount $NtripHost $NtripPort "runtime disabled access" $userAccountId $userAccessId $userAccessName

        Assert-AccessRuntimeRejected $blockedSeed $mount $NtripHost $NtripPort "runtime balance insufficient" $blockedAccountId $blockedAccessId $blockedAccessName

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        Wait-AccessRuntimeOnlineSessionGone $sourceOnline.Key $sourceOnline.Field $StartupTimeoutSec "runtime source"
        $supply = Wait-AccessRuntimeSupplyUsage $supplierAccountId $supplierAccessId $mount $period $StartupTimeoutSec "runtime source"
        if ([int64]$supply.Record.used_seconds -le 0) {
            Fail "runtime supply usage did not carry positive duration: $(ConvertTo-CompactJson $supply.Record)"
        }
        $station = Wait-AccessRuntimeStationRecord $supplierAccountId $supplierAccessId $mount $StartupTimeoutSec "runtime source"
        if ($station.Record.current_online -ne $false) {
            Fail "runtime station record should be offline after disconnect: $(ConvertTo-CompactJson $station.Record)"
        }
        Wait-AccessRuntimeStationEvents $supplierAccountId $supplierAccessId $mount $StartupTimeoutSec "runtime source"
    }
    finally {
        Close-NtripTcpConnection $clientConnection
        Close-NtripTcpConnection $sourceConnection
    }

    Say "PASS access runtime enforcement smoke"
}

function Get-ClusterNodeByUid {
    param(
        [object]$Cluster,
        [string]$Uid
    )

    foreach ($node in @($Cluster.nodes)) {
        if ($node.uid -eq $Uid) {
            return $node
        }
    }

    return $null
}

function Assert-ClusterNodeRuntime {
    param(
        [object]$Cluster,
        [string]$Uid,
        [int]$ExpectedListenPort,
        [int]$ExpectedHttpPort,
        [int]$ExpectedProcessId,
        [string]$Context
    )

    $node = Get-ClusterNodeByUid $Cluster $Uid
    if (-not $node) {
        $seen = @($Cluster.nodes | ForEach-Object { $_.uid }) -join ","
        Fail "$Context cluster missing node $Uid; seen=[$seen]"
    }

    if ($node.online -ne $true) {
        Fail "$Context cluster node $Uid is not online"
    }
    if ([int]$node.listen_port -ne $ExpectedListenPort) {
        Fail "$Context cluster node $Uid listen_port mismatch: expected=$ExpectedListenPort actual=$($node.listen_port)"
    }
    if ([int]$node.http_port -ne $ExpectedHttpPort) {
        Fail "$Context cluster node $Uid http_port mismatch: expected=$ExpectedHttpPort actual=$($node.http_port)"
    }
    if ([int64]$node.process_id -ne [int64]$ExpectedProcessId) {
        Fail "$Context cluster node $Uid process_id mismatch: expected=$ExpectedProcessId actual=$($node.process_id)"
    }
    if ([string]::IsNullOrWhiteSpace([string]$node.hostname)) {
        Fail "$Context cluster node $Uid missing hostname"
    }
    if ($node.http_enabled -ne $true) {
        Fail "$Context cluster node $Uid should report http_enabled=true"
    }

    return $node
}

function Wait-LocalDualNodeCluster {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [string]$PrimaryNodeId,
        [int]$PrimaryHttpPort,
        [int]$PrimaryNtripPort,
        [int]$PrimaryProcessId,
        [string]$SecondaryBase,
        [hashtable]$SecondaryHeaders,
        [string]$SecondaryNodeId,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort,
        [int]$SecondaryProcessId,
        [int]$TimeoutSec
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $primaryCluster = Invoke-RestMethod -Headers $PrimaryHeaders -Uri "$PrimaryBase/api/monitor/cluster" -TimeoutSec 10
            $secondaryCluster = Invoke-RestMethod -Headers $SecondaryHeaders -Uri "$SecondaryBase/api/monitor/cluster" -TimeoutSec 10

            $primaryA = Assert-ClusterNodeRuntime $primaryCluster $PrimaryNodeId $PrimaryNtripPort $PrimaryHttpPort $PrimaryProcessId "primary HTTP view"
            $primaryB = Assert-ClusterNodeRuntime $primaryCluster $SecondaryNodeId $SecondaryNtripPort $SecondaryHttpPort $SecondaryProcessId "primary HTTP view"
            $secondaryA = Assert-ClusterNodeRuntime $secondaryCluster $PrimaryNodeId $PrimaryNtripPort $PrimaryHttpPort $PrimaryProcessId "secondary HTTP view"
            $secondaryB = Assert-ClusterNodeRuntime $secondaryCluster $SecondaryNodeId $SecondaryNtripPort $SecondaryHttpPort $SecondaryProcessId "secondary HTTP view"

            return [pscustomobject]@{
                PrimaryCluster = $primaryCluster
                SecondaryCluster = $secondaryCluster
                PrimaryNodeFromPrimary = $primaryA
                SecondaryNodeFromPrimary = $primaryB
                PrimaryNodeFromSecondary = $secondaryA
                SecondaryNodeFromSecondary = $secondaryB
            }
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "local dual-node cluster view did not converge before timeout; last=[$last]"
}

function Get-E2eMasterNode {
    $raw = Invoke-RedisCommand GET "CASTER:MASTER"
    if ($null -eq $raw) {
        return ""
    }
    return ([string]$raw).Trim()
}

function Wait-E2eMasterNodeInSet {
    param(
        [string[]]$ExpectedNodeIds,
        [int]$TimeoutSec,
        [string]$Context
    )

    $expected = @($ExpectedNodeIds | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($expected.Count -eq 0) {
        Fail "$Context master wait received no expected node ids"
    }

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $masterNode = Get-E2eMasterNode
            if ($expected -contains $masterNode) {
                return $masterNode
            }
            $last = "master=[$masterNode]"
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $redisMaster = ""
    $redisTtl = ""
    try { $redisMaster = Get-E2eMasterNode } catch { $redisMaster = $_.Exception.Message }
    try { $redisTtl = Invoke-RedisCommand TTL "CASTER:MASTER" } catch { $redisTtl = $_.Exception.Message }
    Fail "$Context master lease did not point to expected nodes before timeout; expected=[$($expected -join ',')] last=[$last] redis_master=[$redisMaster] ttl=[$redisTtl]"
}

function Wait-E2eStatusMasterNode {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$ExpectedMasterNodeId,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $status = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/status" -TimeoutSec 10
            if ($status.master_node -eq $ExpectedMasterNodeId) {
                if ($status.redis_auth_connected -ne $true -or $status.redis_caster_connected -ne $true) {
                    Fail "status reports Redis disconnected: caster=$($status.redis_caster_connected) auth=$($status.redis_auth_connected)"
                }
                return $status
            }
            $last = "status.master_node=[$($status.master_node)] node_id=[$($status.node_id)]"
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 1000
    } while ((Get-Date) -lt $deadline)

    Fail "$Context status endpoint did not converge to master_node=$ExpectedMasterNodeId before timeout; last=[$last]"
}

function Wait-E2eClusterMasterNode {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$ExpectedMasterNodeId,
        [string]$RetiredNodeId,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
            if ($cluster.master_node -ne $ExpectedMasterNodeId) {
                $last = "cluster.master_node=[$($cluster.master_node)]"
            }
            else {
                $expectedNode = Get-ClusterNodeByUid $cluster $ExpectedMasterNodeId
                if (-not $expectedNode) {
                    $seen = @($cluster.nodes | ForEach-Object { $_.uid }) -join ","
                    $last = "missing expected master node; seen=[$seen]"
                }
                elseif ($expectedNode.online -ne $true) {
                    $last = "expected master node is not online"
                }
                elseif ($expectedNode.is_master -ne $true) {
                    $last = "expected master node is not flagged is_master=true"
                }
                else {
                    if (-not [string]::IsNullOrWhiteSpace($RetiredNodeId)) {
                        $retiredNode = Get-ClusterNodeByUid $cluster $RetiredNodeId
                        if ($retiredNode -and $retiredNode.is_master -eq $true) {
                            $last = "retired node $RetiredNodeId is still flagged as master"
                            Start-Sleep -Milliseconds 1000
                            continue
                        }
                    }
                    return $cluster
                }
            }
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 1000
    } while ((Get-Date) -lt $deadline)

    Fail "$Context cluster monitor did not converge to master_node=$ExpectedMasterNodeId before timeout; last=[$last]"
}

function Assert-E2eMasterNodeStable {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$ExpectedMasterNodeId,
        [string]$ForbiddenMasterNodeId,
        [int]$ObserveSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($ObserveSec)
    do {
        $redisMaster = Get-E2eMasterNode
        if ($redisMaster -ne $ExpectedMasterNodeId) {
            Fail "$Context Redis CASTER:MASTER changed: expected=$ExpectedMasterNodeId actual=$redisMaster"
        }

        $status = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/status" -TimeoutSec 10
        if ($status.master_node -ne $ExpectedMasterNodeId) {
            Fail "$Context status.master_node changed: expected=$ExpectedMasterNodeId actual=$($status.master_node)"
        }
        if ($status.redis_auth_connected -ne $true -or $status.redis_caster_connected -ne $true) {
            Fail "$Context status reports Redis disconnected: caster=$($status.redis_caster_connected) auth=$($status.redis_auth_connected)"
        }

        $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
        if ($cluster.master_node -ne $ExpectedMasterNodeId) {
            Fail "$Context cluster.master_node changed: expected=$ExpectedMasterNodeId actual=$($cluster.master_node)"
        }
        $expectedNode = Get-ClusterNodeByUid $cluster $ExpectedMasterNodeId
        if (-not $expectedNode -or $expectedNode.is_master -ne $true) {
            Fail "$Context expected master node is missing or not flagged as master"
        }
        if (-not [string]::IsNullOrWhiteSpace($ForbiddenMasterNodeId)) {
            $forbiddenNode = Get-ClusterNodeByUid $cluster $ForbiddenMasterNodeId
            if ($forbiddenNode -and $forbiddenNode.is_master -eq $true) {
                Fail "$Context forbidden node $ForbiddenMasterNodeId is flagged as master"
            }
        }

        Start-Sleep -Milliseconds 1000
    } while ((Get-Date) -lt $deadline)
}

function Assert-DockerImagePresent {
    param(
        [string]$Image,
        [string]$Context
    )

    $result = Invoke-NativeCommand "docker" @("image", "inspect", $Image)
    if ($result.ExitCode -ne 0) {
        Fail "$Context requires local Docker image '$Image'. Build or load it before running this smoke; output=$($result.Output -join ' ')"
    }
}

function Assert-NavCasterRuntimeImageSpecified {
    param(
        [string]$Context
    )

    if ([string]::IsNullOrWhiteSpace($NavCasterImage)) {
        Fail "$Context requires -NavCasterImage navcaster:team-dev-<short12> or another immutable commit tag. Build it with deploy/scripts/build_runtime_image.sh."
    }
    if ($NavCasterImage -eq "navcaster:latest") {
        Fail "$Context does not accept navcaster:latest as runtime evidence. Pass a commit-tagged image such as navcaster:team-dev-<short12>."
    }
}

function Invoke-DockerBridgeRedisCommand {
    param(
        [string]$ContainerName,
        [Parameter(ValueFromRemainingArguments = $true)][string[]]$CommandArgs
    )

    $baseArgs = @("exec", $ContainerName, "redis-cli", "-h", "127.0.0.1", "-p", "6379", "--raw")
    if ($RedisPassword) {
        $baseArgs += @("--no-auth-warning", "-a", $RedisPassword)
    }
    $result = Invoke-NativeCommand "docker" ($baseArgs + $CommandArgs)
    if ($result.ExitCode -ne 0) {
        throw ($result.Output -join "`n")
    }
    return ($result.Output -join "`n").Trim()
}

function Wait-DockerBridgeRedisReady {
    param(
        [string]$ContainerName,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 500
        try {
            $reply = Invoke-DockerBridgeRedisCommand $ContainerName PING
            if ($reply -eq "PONG") {
                return
            }
            $last = $reply
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context Redis container did not become ready before timeout; last=[$last]"
}

function Get-DockerLogsTail {
    param(
        [string]$ContainerName,
        [int]$Tail = 80
    )

    $result = Invoke-NativeCommand "docker" @("logs", "--tail", ([string]$Tail), $ContainerName)
    if ($result.ExitCode -ne 0) {
        return ($result.Output -join "`n")
    }
    return ($result.Output -join "`n")
}

function Set-YamlValueInSectionOrAppend {
    param(
        [string]$Text,
        [string]$Section,
        [string]$Key,
        [string]$Value
    )

    try {
        return Set-YamlValueInSection $Text $Section $Key $Value
    }
    catch {
        $lines = $Text -split "`r?`n"
        $sectionLine = -1
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match '^\S[^:]*:\s*(?:#.*)?$') {
                $name = ($lines[$i] -split ":", 2)[0].Trim()
                if ($name -eq $Section) {
                    $sectionLine = $i
                    break
                }
            }
        }
        if ($sectionLine -lt 0) {
            throw "section not found: $Section"
        }

        $insertAt = $sectionLine + 1
        while ($insertAt -lt $lines.Count) {
            $line = $lines[$insertAt]
            if ($line -match '^\S[^:]*:\s*(?:#.*)?$') {
                break
            }
            $insertAt++
        }

        $before = @()
        if ($insertAt -gt 0) {
            $before = @($lines[0..($insertAt - 1)])
        }
        $after = @()
        if ($insertAt -lt $lines.Count) {
            $after = @($lines[$insertAt..($lines.Count - 1)])
        }

        return (($before + @("  ${Key}: $Value") + $after) -join "`r`n")
    }
}

function Copy-DockerBridgeImageConfig {
    param(
        [string]$DestinationRoot,
        [string]$ContainerPrefix = $DockerBridgeNetworkName
    )

    $copyContainer = "$ContainerPrefix-conf"
    $createResult = Invoke-NativeCommand "docker" @("create", "--name", $copyContainer, $NavCasterImage)
    if ($createResult.ExitCode -ne 0) {
        Fail "failed to create temporary config copy container: $($createResult.Output -join ' ')"
    }
    try {
        $copyResult = Invoke-NativeCommand "docker" @("cp", "${copyContainer}:/app/conf", $DestinationRoot)
        if ($copyResult.ExitCode -ne 0) {
            Fail "failed to copy /app/conf from runtime image: $($copyResult.Output -join ' ')"
        }
    }
    finally {
        Invoke-NativeCommand "docker" @("rm", "-f", $copyContainer) | Out-Null
    }
}

function New-DockerBridgeCasterConfig {
    param(
        [string]$TemplateConfDir,
        [string]$DestinationConfDir
    )

    Copy-Item -LiteralPath $TemplateConfDir -Destination $DestinationConfDir -Recurse -Force

    $servicePath = Join-Path $DestinationConfDir "Service_Setting.yml"
    $corePath = Join-Path $DestinationConfDir "Caster_Core.yml"
    $authPath = Join-Path $DestinationConfDir "Auth_Verify.yml"

    foreach ($path in @($servicePath, $corePath, $authPath)) {
        if (-not (Test-Path -LiteralPath $path)) {
            Fail "runtime image config is missing required file: $path"
        }
    }

    $serviceText = Get-Content -LiteralPath $servicePath -Raw
    $serviceText = Set-YamlValueInSection $serviceText "Ntrip_Listener_Setting" "Listen_Port" "4202"
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Port" "8080"
    $serviceText = Set-YamlValueInSection $serviceText "HTTP_API_Setting" "Bind_Addr" "`"0.0.0.0`""
    $serviceText = Set-YamlValueInSectionOrAppend $serviceText "HTTP_API_Setting" "Force_Enable" "true"
    $serviceText = Set-YamlValueInSectionOrAppend $serviceText "HTTP_API_Setting" "Admin_User" "`"$AdminUser`""
    $serviceText = Set-YamlValueInSectionOrAppend $serviceText "HTTP_API_Setting" "Admin_Password" "`"$AdminPassword`""
    Write-TextFile $servicePath $serviceText

    $coreText = Get-Content -LiteralPath $corePath -Raw
    $coreText = Set-YamlValueInSectionOrAppend $coreText "Caster_Setting" "Update_Intv" "1"
    $coreText = Set-YamlValueInSectionOrAppend $coreText "Caster_Setting" "Key_Expire_Time" "10"
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "IP" "redis"
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "Port" "6379"
    $coreText = Set-YamlValueInSection $coreText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    Write-TextFile $corePath $coreText

    $authText = Get-Content -LiteralPath $authPath -Raw
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "IP" "redis"
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Port" "6379"
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    Write-TextFile $authPath $authText
}

function Start-DockerBridgeCasterContainer {
    param(
        [string]$ContainerName,
        [string]$Hostname,
        [string]$NetworkName,
        [string]$ConfDir,
        [int]$HostHttpPort,
        [int]$HostNtripPort
    )

    $result = Invoke-NativeCommand "docker" @(
        "run", "-d",
        "--name", $ContainerName,
        "--hostname", $Hostname,
        "--network", $NetworkName,
        "-p", "127.0.0.1:${HostHttpPort}:8080",
        "-p", "127.0.0.1:${HostNtripPort}:4202",
        "-v", "${ConfDir}:/app/conf",
        $NavCasterImage
    )
    if ($result.ExitCode -ne 0) {
        Fail "failed to start NavCaster container ${ContainerName}: $($result.Output -join ' ')"
    }
}

function Wait-DockerBridgeHttpReady {
    param(
        [string]$Base,
        [string]$ContainerName,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 800
        try {
            $state = Invoke-NativeCommand "docker" @("inspect", "-f", "{{.State.Running}} {{.State.ExitCode}}", $ContainerName)
            if ($state.ExitCode -eq 0 -and (($state.Output -join " ") -notmatch '^true\b')) {
                $last = "container exited: $($state.Output -join ' ')"
                break
            }
            $health = Invoke-RestMethod -Uri "$Base/api/status/health" -TimeoutSec 3
            if ($health.status -eq "ok") {
                return
            }
            $last = "health.status=$($health.status)"
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    $logs = Format-DebugText (Get-DockerLogsTail $ContainerName 80)
    Fail "$Context HTTP health did not become ready before timeout; last=[$last], logs=[$logs]"
}

function Assert-DockerBridgeClusterNode {
    param(
        [object]$Cluster,
        [string]$Uid,
        [string]$ExpectedHostname,
        [string]$Context
    )

    $node = Get-ClusterNodeByUid $Cluster $Uid
    if (-not $node) {
        $seen = @($Cluster.nodes | ForEach-Object { $_.uid }) -join ","
        Fail "$Context cluster missing node $Uid; seen=[$seen]"
    }
    if ($node.online -ne $true) {
        Fail "$Context cluster node $Uid is not online"
    }
    if ([int]$node.listen_port -ne 4202) {
        Fail "$Context cluster node $Uid listen_port mismatch: expected=4202 actual=$($node.listen_port)"
    }
    if ([int]$node.http_port -ne 8080) {
        Fail "$Context cluster node $Uid http_port mismatch: expected=8080 actual=$($node.http_port)"
    }
    if ([int64]$node.process_id -le 0) {
        Fail "$Context cluster node $Uid process_id should be positive; actual=$($node.process_id)"
    }
    if ($node.hostname -ne $ExpectedHostname) {
        Fail "$Context cluster node $Uid hostname mismatch: expected=$ExpectedHostname actual=$($node.hostname)"
    }
    if ($node.http_enabled -ne $true) {
        Fail "$Context cluster node $Uid should report http_enabled=true"
    }

    return $node
}

function Wait-DockerBridgeClusterConverged {
    param(
        [string]$NodeABase,
        [hashtable]$NodeAHeaders,
        [string]$NodeAId,
        [string]$NodeAHostname,
        [string]$NodeBBase,
        [hashtable]$NodeBHeaders,
        [string]$NodeBId,
        [string]$NodeBHostname,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $clusterA = Invoke-RestMethod -Headers $NodeAHeaders -Uri "$NodeABase/api/monitor/cluster" -TimeoutSec 10
            $clusterB = Invoke-RestMethod -Headers $NodeBHeaders -Uri "$NodeBBase/api/monitor/cluster" -TimeoutSec 10

            $nodeAFromA = Assert-DockerBridgeClusterNode $clusterA $NodeAId $NodeAHostname "$Context node A HTTP view"
            $nodeBFromA = Assert-DockerBridgeClusterNode $clusterA $NodeBId $NodeBHostname "$Context node A HTTP view"
            $nodeAFromB = Assert-DockerBridgeClusterNode $clusterB $NodeAId $NodeAHostname "$Context node B HTTP view"
            $nodeBFromB = Assert-DockerBridgeClusterNode $clusterB $NodeBId $NodeBHostname "$Context node B HTTP view"

            if ([int]$clusterA.total_nodes -lt 2 -or [int]$clusterA.online_nodes -lt 2) {
                Fail "$Context node A HTTP cluster totals too small: total=$($clusterA.total_nodes) online=$($clusterA.online_nodes)"
            }
            if ([int]$clusterB.total_nodes -lt 2 -or [int]$clusterB.online_nodes -lt 2) {
                Fail "$Context node B HTTP cluster totals too small: total=$($clusterB.total_nodes) online=$($clusterB.online_nodes)"
            }

            return [pscustomobject]@{
                NodeACluster = $clusterA
                NodeBCluster = $clusterB
                NodeAFromA = $nodeAFromA
                NodeBFromA = $nodeBFromA
                NodeAFromB = $nodeAFromB
                NodeBFromB = $nodeBFromB
            }
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context Docker bridge cluster view did not converge before timeout; last=[$last]"
}

function Get-DockerBridgeMasterNode {
    param([string]$RedisContainer)

    $raw = Invoke-DockerBridgeRedisCommand $RedisContainer GET "CASTER:MASTER"
    if ($null -eq $raw) {
        return ""
    }
    return ([string]$raw).Trim()
}

function Wait-DockerBridgeMasterNodeInSet {
    param(
        [string]$RedisContainer,
        [string[]]$ExpectedNodeIds,
        [int]$TimeoutSec,
        [string]$Context
    )

    $expected = @($ExpectedNodeIds | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($expected.Count -eq 0) {
        Fail "$Context master wait received no expected node ids"
    }

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $masterNode = Get-DockerBridgeMasterNode $RedisContainer
            if ($expected -contains $masterNode) {
                return $masterNode
            }
            $last = "master=[$masterNode]"
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $redisMaster = ""
    $redisTtl = ""
    try { $redisMaster = Get-DockerBridgeMasterNode $RedisContainer } catch { $redisMaster = $_.Exception.Message }
    try { $redisTtl = Invoke-DockerBridgeRedisCommand $RedisContainer TTL "CASTER:MASTER" } catch { $redisTtl = $_.Exception.Message }
    Fail "$Context master lease did not point to expected nodes before timeout; expected=[$($expected -join ',')] last=[$last] redis_master=[$redisMaster] ttl=[$redisTtl]"
}

function Wait-DockerBridgeStatusMasterNode {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$ExpectedMasterNodeId,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $status = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/status" -TimeoutSec 10
            if ($status.master_node -eq $ExpectedMasterNodeId) {
                if ($status.redis_auth_connected -ne $true -or $status.redis_caster_connected -ne $true) {
                    Fail "$Context status reports Redis disconnected: caster=$($status.redis_caster_connected) auth=$($status.redis_auth_connected)"
                }
                return $status
            }
            $last = "status.master_node=[$($status.master_node)] node_id=[$($status.node_id)]"
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 1000
    } while ((Get-Date) -lt $deadline)

    Fail "$Context status endpoint did not converge to master_node=$ExpectedMasterNodeId before timeout; last=[$last]"
}

function Wait-DockerBridgeClusterMasterNode {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$ExpectedMasterNodeId,
        [string]$RetiredNodeId,
        [int]$TimeoutSec,
        [string]$Context,
        [switch]$RequireRetiredOffline
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
            if ($cluster.master_node -ne $ExpectedMasterNodeId) {
                $last = "cluster.master_node=[$($cluster.master_node)]"
            }
            else {
                $expectedNode = Get-ClusterNodeByUid $cluster $ExpectedMasterNodeId
                if (-not $expectedNode) {
                    $seen = @($cluster.nodes | ForEach-Object { $_.uid }) -join ","
                    $last = "missing expected master node; seen=[$seen]"
                }
                elseif ($expectedNode.online -ne $true) {
                    $last = "expected master node is not online"
                }
                elseif ($expectedNode.is_master -ne $true) {
                    $last = "expected master node is not flagged is_master=true"
                }
                else {
                    if (-not [string]::IsNullOrWhiteSpace($RetiredNodeId)) {
                        $retiredNode = Get-ClusterNodeByUid $cluster $RetiredNodeId
                        if ($retiredNode -and $retiredNode.is_master -eq $true) {
                            $last = "retired node $RetiredNodeId is still flagged as master"
                            Start-Sleep -Milliseconds 1000
                            continue
                        }
                        if ($RequireRetiredOffline -and $retiredNode -and $retiredNode.online -eq $true) {
                            $last = "retired node $RetiredNodeId is still online"
                            Start-Sleep -Milliseconds 1000
                            continue
                        }
                    }
                    return $cluster
                }
            }
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 1000
    } while ((Get-Date) -lt $deadline)

    Fail "$Context cluster monitor did not converge to master_node=$ExpectedMasterNodeId before timeout; last=[$last]"
}

function Invoke-DockerBridgeClusterSmoke {
    $redisContainer = $RedisContainerName
    $nodeAContainer = "$DockerBridgeNetworkName-node-a"
    $nodeBContainer = "$DockerBridgeNetworkName-node-b"
    $nodeAHostname = "nc031-a-$PID"
    $nodeBHostname = "nc031-b-$PID"
    $nodeABase = "http://127.0.0.1:${HttpPort}"
    $nodeBBase = "http://127.0.0.1:${NtripBroadcastHttpPort}"
    $confRoot = Join-Path $env:TEMP ("navcaster-e2e-nc031-conf-" + [guid]::NewGuid().ToString())
    $templateRoot = Join-Path $confRoot "template"
    $templateConfDir = Join-Path $templateRoot "conf"
    $nodeAConfDir = Join-Path $confRoot "node-a-conf"
    $nodeBConfDir = Join-Path $confRoot "node-b-conf"
    $createdNetwork = $false
    $startedRedis = $false
    $startedNodeA = $false
    $startedNodeB = $false

    try {
        Say "validating Docker bridge cluster images navcaster=$NavCasterImage redis=$RedisImage"
        Assert-DockerImagePresent $NavCasterImage "Docker bridge cluster smoke"
        Assert-DockerImagePresent $RedisImage "Docker bridge cluster smoke"
        New-Item -ItemType Directory -Force -Path $confRoot | Out-Null
        New-Item -ItemType Directory -Force -Path $templateRoot | Out-Null
        Copy-DockerBridgeImageConfig $templateRoot
        New-DockerBridgeCasterConfig $templateConfDir $nodeAConfDir
        New-DockerBridgeCasterConfig $templateConfDir $nodeBConfDir

        foreach ($container in @($redisContainer, $nodeAContainer, $nodeBContainer)) {
            $existing = Invoke-NativeCommand "docker" @("ps", "-a", "--filter", "name=^/$container$", "--format", "{{.Names}}")
            if (($existing.Output | Where-Object { $_ -eq $container } | Select-Object -First 1) -eq $container) {
                Fail "container already exists: $container"
            }
        }
        $existingNetwork = Invoke-NativeCommand "docker" @("network", "ls", "--filter", "name=^$DockerBridgeNetworkName$", "--format", "{{.Name}}")
        if (($existingNetwork.Output | Where-Object { $_ -eq $DockerBridgeNetworkName } | Select-Object -First 1) -eq $DockerBridgeNetworkName) {
            Fail "Docker network already exists: $DockerBridgeNetworkName"
        }

        Say "creating Docker bridge network $DockerBridgeNetworkName"
        $networkResult = Invoke-NativeCommand "docker" @("network", "create", "--driver", "bridge", $DockerBridgeNetworkName)
        if ($networkResult.ExitCode -ne 0) {
            Fail "failed to create Docker bridge network: $($networkResult.Output -join ' ')"
        }
        $createdNetwork = $true

        Say "starting bridge Redis container $redisContainer"
        $redisRun = Invoke-NativeCommand "docker" @(
            "run", "-d",
            "--name", $redisContainer,
            "--network", $DockerBridgeNetworkName,
            "--network-alias", "redis",
            $RedisImage,
            "redis-server", "--requirepass", $RedisPassword, "--save", "", "--appendonly", "no"
        )
        if ($redisRun.ExitCode -ne 0) {
            Fail "failed to start bridge Redis container: $($redisRun.Output -join ' ')"
        }
        $startedRedis = $true
        Wait-DockerBridgeRedisReady $redisContainer $StartupTimeoutSec "Docker bridge cluster"

        if (-not $SkipRedisCompat) {
            Say "running Redis compatibility check through bridge Redis container"
            $compatResult = Invoke-NativeCommand "powershell" @(
                "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $compatScript,
                "-DockerContainer", $redisContainer,
                "-HostName", "127.0.0.1",
                "-Port", "6379",
                "-Password", $RedisPassword
            )
            $compatResult.Output | ForEach-Object { Write-Host $_ }
            if ($compatResult.ExitCode -ne 0) {
                Fail "Redis compatibility check failed"
            }
        }

        Say "starting Docker bridge NavCaster node A container=$nodeAContainer host_http=$HttpPort"
        Start-DockerBridgeCasterContainer $nodeAContainer $nodeAHostname $DockerBridgeNetworkName $nodeAConfDir $HttpPort $NtripPort
        $startedNodeA = $true
        Wait-DockerBridgeHttpReady $nodeABase $nodeAContainer $StartupTimeoutSec "Docker bridge node A"

        Say "starting Docker bridge NavCaster node B container=$nodeBContainer host_http=$NtripBroadcastHttpPort"
        Start-DockerBridgeCasterContainer $nodeBContainer $nodeBHostname $DockerBridgeNetworkName $nodeBConfDir $NtripBroadcastHttpPort $NtripBroadcastNtripPort
        $startedNodeB = $true
        Wait-DockerBridgeHttpReady $nodeBBase $nodeBContainer $StartupTimeoutSec "Docker bridge node B"

        $nodeASession = Invoke-E2eLogin $nodeABase
        $nodeBSession = Invoke-E2eLogin $nodeBBase
        $nodeAStatus = Assert-E2eStatusAndCluster $nodeABase $nodeASession.Headers "Docker bridge node A" -RequireRedisConnected
        $nodeBStatus = Assert-E2eStatusAndCluster $nodeBBase $nodeBSession.Headers "Docker bridge node B" -RequireRedisConnected
        Assert-E2eNodeIdFormat $nodeAStatus.node_id "Docker bridge node A"
        Assert-E2eNodeIdFormat $nodeBStatus.node_id "Docker bridge node B"
        if ($nodeAStatus.node_id -eq $nodeBStatus.node_id) {
            Fail "Docker bridge nodes reported the same node_id: $($nodeAStatus.node_id)"
        }

        Wait-DockerBridgeClusterConverged `
            -NodeABase $nodeABase `
            -NodeAHeaders $nodeASession.Headers `
            -NodeAId $nodeAStatus.node_id `
            -NodeAHostname $nodeAHostname `
            -NodeBBase $nodeBBase `
            -NodeBHeaders $nodeBSession.Headers `
            -NodeBId $nodeBStatus.node_id `
            -NodeBHostname $nodeBHostname `
            -TimeoutSec $StartupTimeoutSec `
            -Context "Docker bridge initial" | Out-Null

        $initialMaster = Wait-DockerBridgeMasterNodeInSet $redisContainer @($nodeAStatus.node_id, $nodeBStatus.node_id) ($StartupTimeoutSec + 5) "Docker bridge initial"
        Say "Docker bridge initial master=$initialMaster"
        Wait-DockerBridgeStatusMasterNode $nodeABase $nodeASession.Headers $initialMaster $StartupTimeoutSec "Docker bridge node A initial master" | Out-Null
        Wait-DockerBridgeStatusMasterNode $nodeBBase $nodeBSession.Headers $initialMaster $StartupTimeoutSec "Docker bridge node B initial master" | Out-Null
        Wait-DockerBridgeClusterMasterNode $nodeABase $nodeASession.Headers $initialMaster "" $StartupTimeoutSec "Docker bridge node A initial master" | Out-Null
        Wait-DockerBridgeClusterMasterNode $nodeBBase $nodeBSession.Headers $initialMaster "" $StartupTimeoutSec "Docker bridge node B initial master" | Out-Null

        if ($initialMaster -eq $nodeAStatus.node_id) {
            $masterContainer = $nodeAContainer
            $survivorContainer = $nodeBContainer
            $survivorBase = $nodeBBase
            $survivorHeaders = $nodeBSession.Headers
            $survivorNodeId = $nodeBStatus.node_id
            $retiredNodeId = $nodeAStatus.node_id
        }
        else {
            $masterContainer = $nodeBContainer
            $survivorContainer = $nodeAContainer
            $survivorBase = $nodeABase
            $survivorHeaders = $nodeASession.Headers
            $survivorNodeId = $nodeAStatus.node_id
            $retiredNodeId = $nodeBStatus.node_id
        }

        Say "stopping Docker bridge master container=$masterContainer node_id=$retiredNodeId"
        $stopMaster = Invoke-NativeCommand "docker" @("stop", "-t", "1", $masterContainer)
        if ($stopMaster.ExitCode -ne 0) {
            Fail "failed to stop Docker bridge master container: $($stopMaster.Output -join ' ')"
        }
        if ($masterContainer -eq $nodeAContainer) { $startedNodeA = $false } else { $startedNodeB = $false }

        Wait-DockerBridgeMasterNodeInSet $redisContainer @($survivorNodeId) ($StartupTimeoutSec + 20) "Docker bridge master stop" | Out-Null
        Wait-DockerBridgeStatusMasterNode $survivorBase $survivorHeaders $survivorNodeId $StartupTimeoutSec "Docker bridge master stop" | Out-Null
        Wait-DockerBridgeClusterMasterNode $survivorBase $survivorHeaders $survivorNodeId $retiredNodeId ($StartupTimeoutSec + 10) "Docker bridge master stop" -RequireRetiredOffline | Out-Null

        $survivorInspect = Invoke-NativeCommand "docker" @("inspect", "-f", "{{.State.Running}}", $survivorContainer)
        if ($survivorInspect.ExitCode -ne 0 -or (($survivorInspect.Output -join " ") -notmatch '^true$')) {
            Fail "Docker bridge survivor container is not running: $($survivorInspect.Output -join ' ')"
        }

        Say "PASS Docker bridge cluster smoke"
    }
    finally {
        foreach ($container in @($nodeAContainer, $nodeBContainer, $redisContainer)) {
            try {
                Invoke-NativeCommand "docker" @("rm", "-f", $container) | Out-Null
            }
            catch {
                Write-Warning "failed to remove Docker bridge container ${container}: $($_.Exception.Message)"
            }
        }
        if ($createdNetwork) {
            try {
                Invoke-NativeCommand "docker" @("network", "rm", $DockerBridgeNetworkName) | Out-Null
            }
            catch {
                Write-Warning "failed to remove Docker bridge network ${DockerBridgeNetworkName}: $($_.Exception.Message)"
            }
        }
        if ($confRoot -and (Test-Path -LiteralPath $confRoot)) {
            try {
                Remove-Item -LiteralPath $confRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove Docker bridge temporary config ${confRoot}: $($_.Exception.Message)"
            }
        }
    }
}

function Get-HttpStatusCodeFromError {
    param([object]$ErrorRecord)

    $response = $ErrorRecord.Exception.Response
    if ($null -eq $response) {
        return $null
    }

    try {
        if ($null -ne $response.StatusCode) {
            return [int]$response.StatusCode
        }
    }
    catch {
    }
    try {
        if ($null -ne $response.StatusCode.value__) {
            return [int]$response.StatusCode.value__
        }
    }
    catch {
    }
    try {
        return [int]$response.StatusCode.GetHashCode()
    }
    catch {
    }
    return $null
}

function Invoke-HttpStatusCode {
    param(
        [string]$Uri,
        [hashtable]$Headers = @{},
        [string]$Method = "Get",
        [string]$Body = $null,
        [string]$ContentType = "application/json",
        [int]$TimeoutSec = 10,
        [switch]$DisableKeepAlive
    )

    $parameters = @{
        Uri = $Uri
        Method = $Method
        Headers = $Headers
        TimeoutSec = $TimeoutSec
        UseBasicParsing = $true
    }
    if ($DisableKeepAlive) {
        $parameters["DisableKeepAlive"] = $true
    }
    if (-not [string]::IsNullOrEmpty($Body)) {
        $parameters["Body"] = $Body
        $parameters["ContentType"] = $ContentType
    }

    try {
        $response = Invoke-WebRequest @parameters
        return [int]$response.StatusCode
    }
    catch {
        $code = Get-HttpStatusCodeFromError $_
        if ($null -eq $code) {
            throw
        }
        return $code
    }
}

function Assert-HttpIngressUnauthorized {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Context
    )

    $code = Invoke-HttpStatusCode -Uri "$Base/api/status" -Headers $Headers -TimeoutSec 10
    if ($code -ne 401 -and $code -ne 403) {
        Fail "$Context expected cross-node token rejection, got HTTP $code"
    }
}

function New-HttpIngressNginxConfig {
    param(
        [string]$NodeAContainer,
        [string]$NodeBContainer
    )

    $template = @'
upstream navcaster_sticky {
    hash $http_x_navcaster_sticky consistent;
    server __NODE_A__:8080;
    server __NODE_B__:8080;
}

upstream navcaster_round_robin {
    server __NODE_A__:8080;
    server __NODE_B__:8080;
}

server {
    listen 18080;

    location / {
        proxy_pass http://navcaster_sticky;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}

server {
    listen 18081;

    location / {
        proxy_pass http://navcaster_round_robin;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
'@

    return $template.Replace("__NODE_A__", $NodeAContainer).Replace("__NODE_B__", $NodeBContainer)
}

function Start-DockerBridgeNginxContainer {
    param(
        [string]$ContainerName,
        [string]$NetworkName,
        [string]$ConfPath,
        [int]$StickyPort,
        [int]$RoundRobinPort
    )

    $result = Invoke-NativeCommand "docker" @(
        "run", "-d",
        "--name", $ContainerName,
        "--network", $NetworkName,
        "-p", "127.0.0.1:${StickyPort}:18080",
        "-p", "127.0.0.1:${RoundRobinPort}:18081",
        "-v", "${ConfPath}:/etc/nginx/conf.d/default.conf:ro",
        $NginxImage
    )
    if ($result.ExitCode -ne 0) {
        Fail "failed to start nginx ingress container ${ContainerName}: $($result.Output -join ' ')"
    }
}

function Assert-HttpIngressRoundRobinTokenBoundary {
    param([string]$Base)

    $session = Invoke-E2eLoginWithHeaders -Base $Base -Context "round-robin ingress"
    $codes = @()
    for ($i = 0; $i -lt 8; $i++) {
        $codes += Invoke-HttpStatusCode -Uri "$Base/api/status" -Headers $session.Headers -TimeoutSec 10 -DisableKeepAlive
        Start-Sleep -Milliseconds 150
    }

    $unauthorized = @($codes | Where-Object { $_ -eq 401 -or $_ -eq 403 })
    if ($unauthorized.Count -eq 0) {
        Write-Warning "round-robin ingress did not expose process-local token boundary in this sample; status_codes=[$($codes -join ',')]. Direct cross-node token checks remain the authoritative boundary assertion."
        return
    }
    Say "round-robin ingress token boundary observed status_codes=[$($codes -join ',')]"
}

function New-HttpIngressAccount {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Account
    )

    $body = @{
        account = $Account
        password = "nc032-secret"
        group = "default"
        enabled = $true
        state = 1
        active = 1
    } | ConvertTo-Json -Compress
    $created = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body $body -Uri "$Base/api/accounts" -TimeoutSec 10
    if ($created.ok -ne $true -or $created.account -ne $Account) {
        Fail "HTTP ingress account create returned unexpected payload: $(ConvertTo-CompactJson $created)"
    }
}

function Remove-HttpIngressAccount {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Account
    )

    if ([string]::IsNullOrWhiteSpace($Account)) {
        return
    }
    try {
        Invoke-RestMethod -Method Delete -Headers $Headers -Uri "$Base/api/accounts/$Account" -TimeoutSec 10 | Out-Null
    }
    catch {
    }
}

function Wait-HttpIngressAccountExists {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Account,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $record = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/accounts/$Account" -TimeoutSec 10
            $recordAccount = [string](Get-JsonProperty $record "account")
            $recordUid = [string](Get-JsonProperty $record "uid")
            if ($recordAccount -eq $Account -or $recordUid -eq $Account) {
                return $record
            }
            $last = ConvertTo-CompactJson $record
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    Fail "$Context did not observe account $Account before timeout; last=[$last]"
}

function Wait-HttpIngressAccountMissing {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Account,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        try {
            $code = Invoke-HttpStatusCode -Uri "$Base/api/accounts/$Account" -Headers $Headers -TimeoutSec 10
            if ($code -eq 404) {
                return
            }
            $last = "HTTP $code"
        }
        catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    Fail "$Context still observed account $Account before timeout; last=[$last]"
}

function Invoke-HttpIngressStrategySmoke {
    $networkName = $HttpIngressNetworkName
    $redisContainer = "$networkName-redis"
    $nodeAContainer = "$networkName-node-a"
    $nodeBContainer = "$networkName-node-b"
    $nginxContainer = "$networkName-nginx"
    $nodeAHostname = "nc032-a-$PID"
    $nodeBHostname = "nc032-b-$PID"
    $nodeABase = "http://127.0.0.1:${HttpPort}"
    $nodeBBase = "http://127.0.0.1:${NtripBroadcastHttpPort}"
    $stickyBase = "http://127.0.0.1:${HttpIngressStickyPort}"
    $roundRobinBase = "http://127.0.0.1:${HttpIngressRoundRobinPort}"
    $confRoot = Join-Path $env:TEMP ("navcaster-e2e-nc032-conf-" + [guid]::NewGuid().ToString())
    $templateRoot = Join-Path $confRoot "template"
    $templateConfDir = Join-Path $templateRoot "conf"
    $nodeAConfDir = Join-Path $confRoot "node-a-conf"
    $nodeBConfDir = Join-Path $confRoot "node-b-conf"
    $nginxConfPath = Join-Path $confRoot "nginx-default.conf"
    $createdNetwork = $false
    $createdAccount = $false
    $account = "nc032_ingress_${PID}_$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
    $stickySession = $null

    try {
        Say "validating HTTP ingress images navcaster=$NavCasterImage redis=$RedisImage nginx=$NginxImage"
        Assert-DockerImagePresent $NavCasterImage "HTTP ingress strategy smoke"
        Assert-DockerImagePresent $RedisImage "HTTP ingress strategy smoke"
        Assert-DockerImagePresent $NginxImage "HTTP ingress strategy smoke"
        New-Item -ItemType Directory -Force -Path $confRoot | Out-Null
        New-Item -ItemType Directory -Force -Path $templateRoot | Out-Null
        Copy-DockerBridgeImageConfig $templateRoot $networkName
        New-DockerBridgeCasterConfig $templateConfDir $nodeAConfDir
        New-DockerBridgeCasterConfig $templateConfDir $nodeBConfDir
        Write-TextFile $nginxConfPath (New-HttpIngressNginxConfig $nodeAContainer $nodeBContainer)

        foreach ($container in @($redisContainer, $nodeAContainer, $nodeBContainer, $nginxContainer)) {
            $existing = Invoke-NativeCommand "docker" @("ps", "-a", "--filter", "name=^/$container$", "--format", "{{.Names}}")
            if (($existing.Output | Where-Object { $_ -eq $container } | Select-Object -First 1) -eq $container) {
                Fail "container already exists: $container"
            }
        }
        $existingNetwork = Invoke-NativeCommand "docker" @("network", "ls", "--filter", "name=^$networkName$", "--format", "{{.Name}}")
        if (($existingNetwork.Output | Where-Object { $_ -eq $networkName } | Select-Object -First 1) -eq $networkName) {
            Fail "Docker network already exists: $networkName"
        }

        Say "creating HTTP ingress Docker bridge network $networkName"
        $networkResult = Invoke-NativeCommand "docker" @("network", "create", "--driver", "bridge", $networkName)
        if ($networkResult.ExitCode -ne 0) {
            Fail "failed to create HTTP ingress Docker bridge network: $($networkResult.Output -join ' ')"
        }
        $createdNetwork = $true

        Say "starting HTTP ingress Redis container $redisContainer"
        $redisRun = Invoke-NativeCommand "docker" @(
            "run", "-d",
            "--name", $redisContainer,
            "--network", $networkName,
            "--network-alias", "redis",
            $RedisImage,
            "redis-server", "--requirepass", $RedisPassword, "--save", "", "--appendonly", "no"
        )
        if ($redisRun.ExitCode -ne 0) {
            Fail "failed to start HTTP ingress Redis container: $($redisRun.Output -join ' ')"
        }
        Wait-DockerBridgeRedisReady $redisContainer $StartupTimeoutSec "HTTP ingress strategy"

        if (-not $SkipRedisCompat) {
            Say "running Redis compatibility check through HTTP ingress bridge Redis container"
            $compatResult = Invoke-NativeCommand "powershell" @(
                "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $compatScript,
                "-DockerContainer", $redisContainer,
                "-HostName", "127.0.0.1",
                "-Port", "6379",
                "-Password", $RedisPassword
            )
            $compatResult.Output | ForEach-Object { Write-Host $_ }
            if ($compatResult.ExitCode -ne 0) {
                Fail "Redis compatibility check failed"
            }
        }

        Say "starting HTTP ingress NavCaster node A container=$nodeAContainer host_http=$HttpPort"
        Start-DockerBridgeCasterContainer $nodeAContainer $nodeAHostname $networkName $nodeAConfDir $HttpPort $NtripPort
        Wait-DockerBridgeHttpReady $nodeABase $nodeAContainer $StartupTimeoutSec "HTTP ingress node A"

        Say "starting HTTP ingress NavCaster node B container=$nodeBContainer host_http=$NtripBroadcastHttpPort"
        Start-DockerBridgeCasterContainer $nodeBContainer $nodeBHostname $networkName $nodeBConfDir $NtripBroadcastHttpPort $NtripBroadcastNtripPort
        Wait-DockerBridgeHttpReady $nodeBBase $nodeBContainer $StartupTimeoutSec "HTTP ingress node B"

        $nodeASession = Invoke-E2eLogin $nodeABase
        $nodeBSession = Invoke-E2eLogin $nodeBBase
        $nodeAStatus = Assert-E2eStatusAndCluster $nodeABase $nodeASession.Headers "HTTP ingress node A" -RequireRedisConnected
        $nodeBStatus = Assert-E2eStatusAndCluster $nodeBBase $nodeBSession.Headers "HTTP ingress node B" -RequireRedisConnected
        Assert-E2eNodeIdFormat $nodeAStatus.node_id "HTTP ingress node A"
        Assert-E2eNodeIdFormat $nodeBStatus.node_id "HTTP ingress node B"
        if ($nodeAStatus.node_id -eq $nodeBStatus.node_id) {
            Fail "HTTP ingress nodes reported the same node_id: $($nodeAStatus.node_id)"
        }

        Wait-DockerBridgeClusterConverged `
            -NodeABase $nodeABase `
            -NodeAHeaders $nodeASession.Headers `
            -NodeAId $nodeAStatus.node_id `
            -NodeAHostname $nodeAHostname `
            -NodeBBase $nodeBBase `
            -NodeBHeaders $nodeBSession.Headers `
            -NodeBId $nodeBStatus.node_id `
            -NodeBHostname $nodeBHostname `
            -TimeoutSec $StartupTimeoutSec `
            -Context "HTTP ingress initial" | Out-Null

        $initialMaster = Wait-DockerBridgeMasterNodeInSet $redisContainer @($nodeAStatus.node_id, $nodeBStatus.node_id) ($StartupTimeoutSec + 5) "HTTP ingress initial"
        Say "HTTP ingress cluster master=$initialMaster"

        Say "starting nginx ingress container=$nginxContainer sticky_port=$HttpIngressStickyPort round_robin_port=$HttpIngressRoundRobinPort"
        Start-DockerBridgeNginxContainer $nginxContainer $networkName $nginxConfPath $HttpIngressStickyPort $HttpIngressRoundRobinPort
        Wait-DockerBridgeHttpReady $stickyBase $nginxContainer $StartupTimeoutSec "HTTP ingress sticky proxy"
        Wait-DockerBridgeHttpReady $roundRobinBase $nginxContainer $StartupTimeoutSec "HTTP ingress round-robin proxy"

        Assert-HttpIngressUnauthorized $nodeBBase $nodeASession.Headers "node A token on node B direct entry"
        Assert-HttpIngressUnauthorized $nodeABase $nodeBSession.Headers "node B token on node A direct entry"
        Assert-HttpIngressRoundRobinTokenBoundary $roundRobinBase

        $stickyHeaders = @{ "X-NavCaster-Sticky" = "nc032-$PID" }
        $stickySession = Invoke-E2eLoginWithHeaders -Base $stickyBase -ExtraHeaders $stickyHeaders -Context "sticky ingress"
        $stickyStatus = Assert-E2eStatusAndCluster $stickyBase $stickySession.Headers "HTTP ingress sticky" -RequireRedisConnected
        for ($i = 0; $i -lt 5; $i++) {
            $nextStickyStatus = Assert-E2eStatusAndCluster $stickyBase $stickySession.Headers "HTTP ingress sticky repeat" -RequireRedisConnected
            if ($nextStickyStatus.node_id -ne $stickyStatus.node_id) {
                Fail "sticky ingress routed session to different node: first=$($stickyStatus.node_id) next=$($nextStickyStatus.node_id)"
            }
        }
        Say "sticky ingress pinned authenticated session to node_id=$($stickyStatus.node_id)"

        Say "creating shared account through sticky management ingress account=$account"
        New-HttpIngressAccount $stickyBase $stickySession.Headers $account
        $createdAccount = $true
        Wait-HttpIngressAccountExists $nodeABase $nodeASession.Headers $account $StartupTimeoutSec "direct node A read after sticky create" | Out-Null
        Wait-HttpIngressAccountExists $nodeBBase $nodeBSession.Headers $account $StartupTimeoutSec "direct node B read after sticky create" | Out-Null

        Say "deleting shared account through sticky management ingress account=$account"
        Remove-HttpIngressAccount $stickyBase $stickySession.Headers $account
        $createdAccount = $false
        Wait-HttpIngressAccountMissing $nodeABase $nodeASession.Headers $account $StartupTimeoutSec "direct node A read after sticky delete"
        Wait-HttpIngressAccountMissing $nodeBBase $nodeBSession.Headers $account $StartupTimeoutSec "direct node B read after sticky delete"

        Say "PASS HTTP ingress/sticky session/write routing strategy smoke"
    }
    finally {
        if ($createdAccount -and $stickySession) {
            try {
                Remove-HttpIngressAccount $stickyBase $stickySession.Headers $account
            }
            catch {
                Write-Warning "failed to remove HTTP ingress smoke account ${account}: $($_.Exception.Message)"
            }
        }
        foreach ($container in @($nginxContainer, $nodeAContainer, $nodeBContainer, $redisContainer)) {
            try {
                Invoke-NativeCommand "docker" @("rm", "-f", $container) | Out-Null
            }
            catch {
                Write-Warning "failed to remove HTTP ingress container ${container}: $($_.Exception.Message)"
            }
        }
        if ($createdNetwork) {
            try {
                Invoke-NativeCommand "docker" @("network", "rm", $networkName) | Out-Null
            }
            catch {
                Write-Warning "failed to remove HTTP ingress Docker network ${networkName}: $($_.Exception.Message)"
            }
        }
        if ($confRoot -and (Test-Path -LiteralPath $confRoot)) {
            try {
                Remove-Item -LiteralPath $confRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove HTTP ingress temporary config ${confRoot}: $($_.Exception.Message)"
            }
        }
    }
}

function Get-PullRelayStatusFromHttp {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid
    )

    $states = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/relays/pull/status" -TimeoutSec 10
    return Get-JsonProperty $states $Uid
}

function Get-PullRelayRecordFromHttp {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid
    )

    return Invoke-RestMethod -Headers $Headers -Uri "$Base/api/relays/pull/$Uid" -TimeoutSec 10
}

function Get-PullRelayStatusFromRedis {
    param([string]$Uid)

    $raw = Invoke-RedisCommand HGET "PULL:STAT" $Uid
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }

    try {
        return $raw | ConvertFrom-Json
    }
    catch {
        Fail "PULL:STAT $Uid is not valid JSON: $(Format-DebugText $raw)"
    }
}

function Wait-PullRelayRunning {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid,
        [string]$ExpectedNodeId,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $status = Get-PullRelayStatusFromHttp $Base $Headers $Uid
            if ($null -eq $status) {
                $status = Get-PullRelayStatusFromRedis $Uid
            }
            if ($null -ne $status -and [int]$status.state -eq 1) {
                if ([string]::IsNullOrWhiteSpace([string]$status.connect_key)) {
                    Fail "$Context PULL:STAT $Uid running without connect_key"
                }
                if ($status.node_uid -ne $ExpectedNodeId) {
                    Fail "$Context PULL:STAT $Uid node_uid mismatch: expected=$ExpectedNodeId actual=$($status.node_uid)"
                }
                return $status
            }
            $last = ConvertTo-CompactJson $status
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL "PULL:STAT")
    Fail "$Context pull relay $Uid did not become running before timeout; last=[$last], pull_stat=[$raw]"
}

function Wait-PullRelayNotRunningOnNode {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid,
        [string]$ForbiddenNodeId,
        [int]$TimeoutSec,
        [string]$Context
    )

    if ([string]::IsNullOrWhiteSpace($ForbiddenNodeId)) {
        return
    }

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        $status = $null
        try {
            $status = Get-PullRelayStatusFromHttp $Base $Headers $Uid
        }
        catch {
            $last = $_.Exception.Message
        }
        if ($null -eq $status) {
            try {
                $status = Get-PullRelayStatusFromRedis $Uid
            }
            catch {
                $last = $_.Exception.Message
                continue
            }
        }
        if ($null -eq $status -or [int]$status.state -ne 1 -or $status.node_uid -ne $ForbiddenNodeId) {
            return
        }
        $last = ConvertTo-CompactJson $status
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL "PULL:STAT")
    Fail "$Context pull relay $Uid remained running on retired node $ForbiddenNodeId before timeout; last=[$last], pull_stat=[$raw]"
}

function Wait-PullRelayNotRunning {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        $httpStatus = $null
        $redisStatus = $null
        $httpError = $null
        $redisError = $null
        try {
            $httpStatus = Get-PullRelayStatusFromHttp $Base $Headers $Uid
        }
        catch {
            $httpError = $_.Exception.Message
        }
        try {
            $redisStatus = Get-PullRelayStatusFromRedis $Uid
        }
        catch {
            $redisError = $_.Exception.Message
        }

        if (-not $redisError) {
            $httpRunning = $null -ne $httpStatus -and [int]$httpStatus.state -eq 1
            $redisRunning = $null -ne $redisStatus -and [int]$redisStatus.state -eq 1
            if (-not $httpRunning -and -not $redisRunning) {
                return
            }
        }

        $last = "http=$(ConvertTo-CompactJson $httpStatus); redis=$(ConvertTo-CompactJson $redisStatus); http_error=$httpError; redis_error=$redisError"
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL "PULL:STAT")
    Fail "$Context pull relay $Uid remained running before timeout; last=[$last], pull_stat=[$raw]"
}

function Wait-ClusterPullCountAtLeast {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NodeId,
        [int]$ExpectedPullCount,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
            $node = Get-ClusterNodeByUid $cluster $NodeId
            if ($node -and [int]$node.pull -ge $ExpectedPullCount) {
                return $node
            }
            $last = if ($node) { "pull=$($node.pull)" } else { "missing node $NodeId" }
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context cluster pull count did not reach $ExpectedPullCount for node $NodeId; last=[$last]"
}

function Wait-ClusterPullCountAtMost {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NodeId,
        [int]$ExpectedPullCount,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
            $node = Get-ClusterNodeByUid $cluster $NodeId
            if ($node -and [int]$node.pull -le $ExpectedPullCount) {
                return $node
            }
            $last = if ($node) { "pull=$($node.pull)" } else { "missing node $NodeId" }
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context cluster pull count did not return to <= $ExpectedPullCount for node $NodeId; last=[$last]"
}

function Remove-PullRelayRecord {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid
    )

    if ([string]::IsNullOrWhiteSpace($Uid)) {
        return
    }

    try {
        Invoke-RestMethod -Method Delete -Headers $Headers -Uri "$Base/api/relays/pull/$Uid" -TimeoutSec 10 | Out-Null
    }
    catch {
    }
    try {
        Invoke-RedisCommand HDEL "PULL:RECORD" $Uid | Out-Null
        Invoke-RedisCommand HDEL "PULL:STAT" $Uid | Out-Null
    }
    catch {
    }
}

function Get-PushRelayStatusFromHttp {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid
    )

    $states = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/relays/push/status" -TimeoutSec 10
    return Get-JsonProperty $states $Uid
}

function Get-PushRelayRecordFromHttp {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid
    )

    return Invoke-RestMethod -Headers $Headers -Uri "$Base/api/relays/push/$Uid" -TimeoutSec 10
}

function Get-PushRelayStatusFromRedis {
    param([string]$Uid)

    $raw = Invoke-RedisCommand HGET "PUSH:STAT" $Uid
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }

    try {
        return $raw | ConvertFrom-Json
    }
    catch {
        Fail "PUSH:STAT $Uid is not valid JSON: $(Format-DebugText $raw)"
    }
}

function Wait-PushRelayRunning {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid,
        [string]$ExpectedNodeId,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $status = Get-PushRelayStatusFromHttp $Base $Headers $Uid
            if ($null -eq $status) {
                $status = Get-PushRelayStatusFromRedis $Uid
            }
            if ($null -ne $status -and [int]$status.state -eq 1) {
                if ([string]::IsNullOrWhiteSpace([string]$status.connect_key)) {
                    Fail "$Context PUSH:STAT $Uid running without connect_key"
                }
                if ($status.node_uid -ne $ExpectedNodeId) {
                    Fail "$Context PUSH:STAT $Uid node_uid mismatch: expected=$ExpectedNodeId actual=$($status.node_uid)"
                }
                return $status
            }
            $last = ConvertTo-CompactJson $status
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL "PUSH:STAT")
    Fail "$Context push relay $Uid did not become running before timeout; last=[$last], push_stat=[$raw]"
}

function Wait-PushRelayNotRunning {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        $httpStatus = $null
        $redisStatus = $null
        $httpError = $null
        $redisError = $null
        try {
            $httpStatus = Get-PushRelayStatusFromHttp $Base $Headers $Uid
        }
        catch {
            $httpError = $_.Exception.Message
        }
        try {
            $redisStatus = Get-PushRelayStatusFromRedis $Uid
        }
        catch {
            $redisError = $_.Exception.Message
        }

        if (-not $redisError) {
            $httpRunning = $null -ne $httpStatus -and [int]$httpStatus.state -eq 1
            $redisRunning = $null -ne $redisStatus -and [int]$redisStatus.state -eq 1
            if (-not $httpRunning -and -not $redisRunning) {
                return
            }
        }

        $last = "http=$(ConvertTo-CompactJson $httpStatus); redis=$(ConvertTo-CompactJson $redisStatus); http_error=$httpError; redis_error=$redisError"
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL "PUSH:STAT")
    Fail "$Context push relay $Uid remained running before timeout; last=[$last], push_stat=[$raw]"
}

function Wait-PushRelayNotRunningOnNode {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid,
        [string]$ForbiddenNodeId,
        [int]$TimeoutSec,
        [string]$Context
    )

    if ([string]::IsNullOrWhiteSpace($ForbiddenNodeId)) {
        return
    }

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        $status = $null
        try {
            $status = Get-PushRelayStatusFromHttp $Base $Headers $Uid
        }
        catch {
            $last = $_.Exception.Message
        }
        if ($null -eq $status) {
            try {
                $status = Get-PushRelayStatusFromRedis $Uid
            }
            catch {
                $last = $_.Exception.Message
                continue
            }
        }
        if ($null -eq $status -or [int]$status.state -ne 1 -or $status.node_uid -ne $ForbiddenNodeId) {
            return
        }
        $last = ConvertTo-CompactJson $status
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGETALL "PUSH:STAT")
    Fail "$Context push relay $Uid remained running on retired node $ForbiddenNodeId before timeout; last=[$last], push_stat=[$raw]"
}

function Wait-ClusterPushCountAtLeast {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NodeId,
        [int]$ExpectedPushCount,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
            $node = Get-ClusterNodeByUid $cluster $NodeId
            if ($node -and [int]$node.push -ge $ExpectedPushCount) {
                return $node
            }
            $last = if ($node) { "push=$($node.push)" } else { "missing node $NodeId" }
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context cluster push count did not reach $ExpectedPushCount for node $NodeId; last=[$last]"
}

function Wait-ClusterPushCountAtMost {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$NodeId,
        [int]$ExpectedPushCount,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $cluster = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/monitor/cluster" -TimeoutSec 10
            $node = Get-ClusterNodeByUid $cluster $NodeId
            if ($node -and [int]$node.push -le $ExpectedPushCount) {
                return $node
            }
            $last = if ($node) { "push=$($node.push)" } else { "missing node $NodeId" }
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context cluster push count did not return to <= $ExpectedPushCount for node $NodeId; last=[$last]"
}

function Wait-PushedSourceActive {
    param(
        [pscustomobject]$Seed,
        [string]$Mount,
        [string[]]$ExcludedConnectKeys = @(),
        [int]$TimeoutSec,
        [string]$Context
    )

    $excluded = @($ExcludedConnectKeys | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $online = Get-RedisHashMap "MPT:LIST"
        $records = Get-RedisHashMap "MPT:REC:$Mount"
        if ($online.ContainsKey($Mount) -and $records.Count -gt 0) {
            $connectKey = @($records.Keys | Where-Object { $_ -notin $excluded } | Select-Object -First 1)[0]
            if ([string]::IsNullOrWhiteSpace($connectKey)) {
                Start-Sleep -Milliseconds 500
                continue
            }
            $sourceStatusRaw = Invoke-RedisCommand HGET "MPT:STAT" $connectKey
            if (-not [string]::IsNullOrWhiteSpace($sourceStatusRaw)) {
                return [pscustomobject]@{
                    Mount = $Mount
                    ConnectKey = $connectKey
                    Record = $records[$connectKey]
                    SourceStatusRaw = $sourceStatusRaw
                }
            }
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $onlineRaw = Format-DebugText (Invoke-RedisCommand HGETALL "MPT:LIST")
    $recRaw = Format-DebugText (Invoke-RedisCommand HGETALL "MPT:REC:$Mount")
    $statRaw = Format-DebugText (Invoke-RedisCommand HGETALL "MPT:STAT")
    Fail "$Context pushed source did not become active on mount=$Mount; excluded=[$($excluded -join ',')], mpt_list=[$onlineRaw], mpt_rec=[$recRaw], mpt_stat=[$statRaw]"
}

function Wait-PushedSourceGone {
    param(
        [pscustomobject]$Seed,
        [string]$Mount,
        [string]$ConnectKey,
        [int]$TimeoutSec,
        [string]$Context
    )

    if ([string]::IsNullOrWhiteSpace($ConnectKey)) {
        return
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $records = Get-RedisHashMap "MPT:REC:$Mount"
        if (-not $records.ContainsKey($ConnectKey)) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    $recRaw = Format-DebugText (Invoke-RedisCommand HGETALL "MPT:REC:$Mount")
    $statRaw = Format-DebugText (Invoke-RedisCommand HGET "MPT:STAT" $ConnectKey)
    Fail "$Context pushed source remained active; mount=$Mount, connect_key=$ConnectKey, mpt_rec=[$recRaw], mpt_stat=[$statRaw]"
}

function Remove-PushRelayRecord {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$Uid
    )

    if ([string]::IsNullOrWhiteSpace($Uid)) {
        return
    }

    try {
        Invoke-RestMethod -Method Delete -Headers $Headers -Uri "$Base/api/relays/push/$Uid" -TimeoutSec 10 | Out-Null
    }
    catch {
    }
    try {
        Invoke-RedisCommand HDEL "PUSH:RECORD" $Uid | Out-Null
        Invoke-RedisCommand HDEL "PUSH:STAT" $Uid | Out-Null
    }
    catch {
    }
}

function Get-DataPushPeriod {
    return (Get-Date).ToUniversalTime().ToString("yyyyMM")
}

function Wait-DataPushJobRuntimeStatus {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [string]$JobId,
        [string]$Period,
        [string]$ExpectedRelayStatus,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $job = Invoke-RestMethod -Method Post -Headers $Headers -ContentType "application/json" -Body (@{
                period = $Period
                operator_note = "$Context reconcile"
            } | ConvertTo-Json -Compress) -Uri "$Base/api/v1/admin/data-push-jobs/$JobId/reconcile" -TimeoutSec 10
            if ($job.relay_status -eq $ExpectedRelayStatus) {
                return $job
            }
            $last = ConvertTo-CompactJson $job
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    $raw = Format-DebugText (Invoke-RedisCommand HGET "DATA:PUSH:JOB:$Period" $JobId)
    Fail "$Context DataPushJob $JobId did not reach relay_status=$ExpectedRelayStatus before timeout; last=[$last], raw=[$raw]"
}

function Assert-DataPushUsageAndLedger {
    param(
        [string]$AccountId,
        [string]$JobId,
        [string]$UsageId,
        [string]$Period,
        [int]$ExpectedDebitCents,
        [string]$Context
    )

    $usage = Get-RedisJsonHashField "DATA:PUSH:$Period" $UsageId $Context
    if ($usage.account_id -ne $AccountId -or $usage.job_id -ne $JobId -or $usage.actual_debit_cents -ne $ExpectedDebitCents) {
        Fail "$Context DataPush usage mismatch: $(ConvertTo-CompactJson $usage)"
    }
    if ([string]::IsNullOrWhiteSpace([string]$usage.ledger_id)) {
        Fail "$Context DataPush usage missing ledger_id: $(ConvertTo-CompactJson $usage)"
    }

    $ledger = Get-RedisJsonHashField "ACC:BALANCE:LEDGER:$Period" $usage.ledger_id $Context
    if ($ledger.account_id -ne $AccountId -or $ledger.usage_id -ne $UsageId -or $ledger.delta_cents -ne (-1 * $ExpectedDebitCents)) {
        Fail "$Context DataPush ledger mismatch: usage=$(ConvertTo-CompactJson $usage) ledger=$(ConvertTo-CompactJson $ledger)"
    }
    return [pscustomobject]@{
        Usage = $usage
        Ledger = $ledger
    }
}

function Invoke-DataPushRelayPushE2ESmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null
    $sourceConnection = $null
    $forwardClientConnection = $null
    $seed = $null
    $relayUid = $null
    $targetSource = $null

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $PrimaryBase $PrimaryHeaders "DataPush relay_push primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "DataPush relay_push primary"

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc073-" + [guid]::NewGuid().ToString())
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
            -RoverOnlineProtection $true `
            -RoverAnonymousLogin $false `
            -BaseAnonymousLogin $false `
            -SourceAnonymousLogin $false

        $secondaryBase = "http://${HttpBindAddr}:${SecondaryHttpPort}"
        $secondaryNode = Start-E2eCasterServiceNode "nc073-data-push-relay-target" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondarySession.Headers "DataPush relay_push secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "DataPush relay_push secondary"
        if ($primaryStatus.node_id -eq $secondaryStatus.node_id) {
            Fail "DataPush relay_push smoke requires distinct local node ids, both were $($primaryStatus.node_id)"
        }

        $baselineNode = Wait-ClusterPushCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id 0 $StartupTimeoutSec "DataPush relay_push baseline"
        $baselinePush = [int]$baselineNode.push

        $seed = New-NtripAuthSessionSeed -Label "nc073_data_push_relay"
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed
        Add-NtripSourceAuthAccountSeed $seed
        $sourceConnection = Open-NtripSourceForSeed $seed $HttpBindAddr $PrimaryNtripPort "data-push-relay-source"
        $script:ntripSourceConnection = $sourceConnection

        $period = Get-DataPushPeriod
        $accountId = "$($seed.Prefix)_user_acc"
        $username = "$($seed.Prefix)_user"
        $password = "data-push-relay-pass"
        $configId = "$($seed.Prefix)_cfg"
        $jobId = "$($seed.Prefix)_job"
        $expectedDebitCents = 100

        Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -ContentType "application/json" -Body (@{
            account_id = $accountId
            username = $username
            role = "user"
            password = $password
            balance_cents = 20000
            concurrency_limit = 2
        } | ConvertTo-Json -Compress) -Uri "$PrimaryBase/api/v1/admin/accounts" -TimeoutSec 10 | Out-Null

        $config = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -ContentType "application/json" -Body (@{
            config_id = $configId
            name = "NC-073 relay_push"
            target_mountpoint = $seed.TargetMount
            fixed_hourly_price_cents = $expectedDebitCents
            execution_mode = "relay_push"
            source_mountpoint = $seed.Mount
            relay_target_host = $HttpBindAddr
            relay_target_port = $SecondaryNtripPort
            relay_target_mountpoint = $seed.TargetMount
            relay_target_account = $seed.SourceUser
            relay_target_password = $seed.SourcePassword
            relay_push_type = 2
        } | ConvertTo-Json -Compress) -Uri "$PrimaryBase/api/v1/admin/data-push-configs" -TimeoutSec 10
        if ($config.config_id -ne $configId -or $config.execution_mode -ne "relay_push") {
            Fail "DataPush relay config create mismatch: $(ConvertTo-CompactJson $config)"
        }

        $userLogin = Invoke-E2eLoginWithHeaders -Base $PrimaryBase -Context "DataPush relay user" -Username $username -Password $password
        $userConfigs = Invoke-RestMethod -Headers $userLogin.Headers -Uri "$PrimaryBase/api/v1/me/data-push/configs" -TimeoutSec 10
        if (-not $userConfigs.PSObject.Properties[$configId]) {
            Fail "DataPush relay self-service config list missing $configId"
        }

        $job = Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
            job_id = $jobId
            config_id = $configId
            used_seconds = 3600
            period = $period
            operator_note = "NC-073 real relay_push"
        } | ConvertTo-Json -Compress) -Uri "$PrimaryBase/api/v1/me/data-push/jobs" -TimeoutSec 10
        $relayUid = "data_push:$jobId"
        if ($job.job_id -ne $jobId -or $job.relay_uid -ne $relayUid -or $job.execution_mode -ne "relay_push" -or $job.status -ne "queued") {
            Fail "DataPush relay job create mismatch: $(ConvertTo-CompactJson $job)"
        }
        if ($job.actual_debit_cents -ne $expectedDebitCents -or [string]::IsNullOrWhiteSpace([string]$job.ledger_id)) {
            Fail "DataPush relay job missing debit/ledger fields: $(ConvertTo-CompactJson $job)"
        }

        $ledgerFacts = Assert-DataPushUsageAndLedger $accountId $jobId $job.usage_id $period $expectedDebitCents "DataPush relay job create"
        if ($ledgerFacts.Usage.relay_uid -ne $relayUid) {
            Fail "DataPush relay usage missing relay_uid: $(ConvertTo-CompactJson $ledgerFacts.Usage)"
        }

        $runningStatus = Wait-PushRelayRunning $PrimaryBase $PrimaryHeaders $relayUid $primaryStatus.node_id ($StartupTimeoutSec + 10) "DataPush relay_push"
        $targetSource = Wait-PushedSourceActive $seed $seed.TargetMount @($seed.SourceConnectKey, $runningStatus.connect_key) $StartupTimeoutSec "DataPush relay target source"
        if ($targetSource.ConnectKey -eq $seed.SourceConnectKey -or $targetSource.ConnectKey -eq $runningStatus.connect_key) {
            Fail "DataPush relay target source connect_key must be distinct from source/relay keys: target=$($targetSource.ConnectKey) source=$($seed.SourceConnectKey) relay=$($runningStatus.connect_key)"
        }
        $seed.TargetSourceConnectKey = $targetSource.ConnectKey
        Wait-ClusterPushCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id ($baselinePush + 1) $StartupTimeoutSec "DataPush relay cluster count" | Out-Null

        $reconciled = Wait-DataPushJobRuntimeStatus $PrimaryBase $PrimaryHeaders $jobId $period "running" $StartupTimeoutSec "DataPush relay runtime"
        if ($reconciled.relay_uid -ne $relayUid -or $reconciled.relay_connect_key -ne $runningStatus.connect_key -or $reconciled.relay_node_uid -ne $primaryStatus.node_id) {
            Fail "DataPush relay reconcile mismatch: job=$(ConvertTo-CompactJson $reconciled) status=$(ConvertTo-CompactJson $runningStatus)"
        }

        $maintenance = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -ContentType "application/json" -Body (@{
            period = $period
            unhealthy_after_seconds = 300
        } | ConvertTo-Json -Compress) -Uri "$PrimaryBase/api/v1/admin/data-push-maintenance?action=run&period=$period" -TimeoutSec 10
        if ($maintenance.updated_count -lt 1 -or -not $maintenance.items.PSObject.Properties[$jobId]) {
            Fail "DataPush relay maintenance did not include $jobId`: $(ConvertTo-CompactJson $maintenance)"
        }

        $forwardClientConnection = Open-NtripClientForMount `
            -Seed $seed `
            -Mount $seed.TargetMount `
            -NtripHost $HttpBindAddr `
            -NtripPort $SecondaryNtripPort `
            -Label "data-push-relay-forward-client"
        $forwardSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $forwardSession.Field
        $payload = "NC073-DATA-PUSH-RELAY:$($seed.Prefix):1234567890"
        Start-Sleep -Milliseconds 500
        Write-NtripPayload $sourceConnection $payload "DataPush relay data forwarding"
        Wait-NtripPayload $forwardClientConnection $payload $StartupTimeoutSec "DataPush relay data forwarding"
        Close-NtripTcpConnection $forwardClientConnection
        $forwardClientConnection = $null
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "DataPush relay client cleanup" | Out-Null

        $cancelled = Invoke-RestMethod -Method Post -Headers $userLogin.Headers -ContentType "application/json" -Body (@{
            action = "cancel"
            period = $period
            operator_note = "NC-073 cleanup"
        } | ConvertTo-Json -Compress) -Uri "$PrimaryBase/api/v1/me/data-push/jobs/$jobId/control?period=$period" -TimeoutSec 10
        if ($cancelled.status -ne "cancelled") {
            Fail "DataPush relay cancel mismatch: $(ConvertTo-CompactJson $cancelled)"
        }
        Wait-PushRelayNotRunning $PrimaryBase $PrimaryHeaders $relayUid $StartupTimeoutSec "DataPush relay cancel"
        Wait-PushedSourceGone $seed $seed.TargetMount $targetSource.ConnectKey $StartupTimeoutSec "DataPush relay cancel target source"
        Wait-ClusterPushCountAtMost $PrimaryBase $PrimaryHeaders $primaryStatus.node_id $baselinePush $StartupTimeoutSec "DataPush relay cancel cluster count" | Out-Null
        $relayUid = $null
        $seed.TargetSourceConnectKey = ""
        $targetSource = $null

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

        Say "PASS DataPush relay_push real e2e"
    }
    finally {
        if ($relayUid) {
            Remove-PushRelayRecord $PrimaryBase $PrimaryHeaders $relayUid
        }
        Close-NtripTcpConnection $forwardClientConnection
        Close-NtripTcpConnection $sourceConnection
        if ($script:ntripSourceConnection -eq $sourceConnection) {
            $script:ntripSourceConnection = $null
        }
        if ($script:ntripAuthSeed -eq $seed) {
            $script:ntripAuthSeed = $null
        }
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove DataPush relay service config: $($_.Exception.Message)"
            }
        }
    }
}

function Wait-E2eRedisStatusConnected {
    param(
        [string]$Base,
        [hashtable]$Headers,
        [int]$TimeoutSec,
        [string]$Context
    )

    $last = $null
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 1000
        try {
            $last = Invoke-RestMethod -Headers $Headers -Uri "$Base/api/status" -TimeoutSec 10
            if ($last.redis_auth_connected -eq $true -and $last.redis_caster_connected -eq $true) {
                return $last
            }
        }
        catch {
            $last = $_.Exception.Message
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context did not report Redis reconnected before timeout; last=[$last]"
}

function Wait-RedisDockerReady {
    param(
        [int]$TimeoutSec,
        [string]$Context
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        Start-Sleep -Milliseconds 500
        try {
            if ((Invoke-RedisInDocker PING) -eq "PONG") {
                return
            }
        }
        catch {
        }
    } while ((Get-Date) -lt $deadline)

    Fail "$Context Redis fixture did not become ready before timeout"
}

function Invoke-RedisReconnectSmoke {
    param(
        [string]$Base,
        [hashtable]$Headers
    )

    if ($RedisMode -ne "Docker") {
        Fail "Redis reconnect smoke requires RedisMode Docker so the fixture container can be stopped and started."
    }

    Say "stopping Redis fixture container $RedisContainerName for reconnect smoke"
    $stopResult = Invoke-NativeCommand "docker" @("stop", "-t", "1", $RedisContainerName)
    if ($stopResult.ExitCode -ne 0) {
        Fail ("failed to stop Redis fixture container: " + ($stopResult.Output -join " "))
    }

    Start-Sleep -Seconds 2
    Assert-E2eHealth $Base "redis reconnect while fixture stopped"

    $left = Get-Process -Id $serviceProcess.Id -ErrorAction SilentlyContinue
    if (-not $left -or $serviceProcess.HasExited) {
        Fail "CasterService exited while Redis fixture was stopped"
    }

    Say "restarting Redis fixture container $RedisContainerName"
    $startResult = Invoke-NativeCommand "docker" @("start", $RedisContainerName)
    if ($startResult.ExitCode -ne 0) {
        Fail ("failed to restart Redis fixture container: " + ($startResult.Output -join " "))
    }

    Wait-RedisDockerReady $StartupTimeoutSec "redis reconnect smoke restart"
    Wait-E2eRedisStatusConnected $Base $Headers $StartupTimeoutSec "redis reconnect smoke" | Out-Null

    $session = Invoke-E2eLogin $Base
    Assert-E2eStatusAndCluster $Base $session.Headers "redis reconnect post-recovery" -RequireRedisConnected | Out-Null
    Say "PASS Redis reconnect smoke"
}

function Invoke-LocalDualNodeIdentitySmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $PrimaryBase $PrimaryHeaders "local dual-node primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "local dual-node primary"

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc025-" + [guid]::NewGuid().ToString())
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
        $secondaryNode = Start-E2eCasterServiceNode "nc025-secondary" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryHeaders = $secondarySession.Headers
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondaryHeaders "local dual-node secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "local dual-node secondary"

        if ($primaryStatus.node_id -eq $secondaryStatus.node_id) {
            Fail "local dual-node instances reported the same node_id: $($primaryStatus.node_id)"
        }

        $cluster = Wait-LocalDualNodeCluster `
            -PrimaryBase $PrimaryBase `
            -PrimaryHeaders $PrimaryHeaders `
            -PrimaryNodeId $primaryStatus.node_id `
            -PrimaryHttpPort $HttpPort `
            -PrimaryNtripPort $PrimaryNtripPort `
            -PrimaryProcessId $serviceProcess.Id `
            -SecondaryBase $secondaryBase `
            -SecondaryHeaders $secondaryHeaders `
            -SecondaryNodeId $secondaryStatus.node_id `
            -SecondaryHttpPort $SecondaryHttpPort `
            -SecondaryNtripPort $SecondaryNtripPort `
            -SecondaryProcessId $secondaryNode.Process.Id `
            -TimeoutSec $StartupTimeoutSec

        if ([int]$cluster.PrimaryCluster.total_nodes -lt 2 -or [int]$cluster.PrimaryCluster.online_nodes -lt 2) {
            Fail "primary HTTP cluster view did not report at least two online nodes: total=$($cluster.PrimaryCluster.total_nodes) online=$($cluster.PrimaryCluster.online_nodes)"
        }
        if ([int]$cluster.SecondaryCluster.total_nodes -lt 2 -or [int]$cluster.SecondaryCluster.online_nodes -lt 2) {
            Fail "secondary HTTP cluster view did not report at least two online nodes: total=$($cluster.SecondaryCluster.total_nodes) online=$($cluster.SecondaryCluster.online_nodes)"
        }
        if ($cluster.PrimaryNodeFromPrimary.hostname -ne $cluster.SecondaryNodeFromPrimary.hostname) {
            Fail "local dual-node instances should report the same hostname: primary=$($cluster.PrimaryNodeFromPrimary.hostname) secondary=$($cluster.SecondaryNodeFromPrimary.hostname)"
        }

        Stop-E2eCasterServiceNode $secondaryNode
        $secondaryNode = $null
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            $secondaryConfRoot = $null
        }

        Say "PASS local dual-node identity/cluster smoke"
    }
    finally {
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove local dual-node service config: $($_.Exception.Message)"
            }
        }
    }
}

function Invoke-MasterLeaseFailoverSmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $PrimaryBase $PrimaryHeaders "master lease failover primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "master lease failover primary"

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc029-" + [guid]::NewGuid().ToString())
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
        $secondaryNode = Start-E2eCasterServiceNode "nc029-secondary" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryHeaders = $secondarySession.Headers
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondaryHeaders "master lease failover secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "master lease failover secondary"

        if ($primaryStatus.node_id -eq $secondaryStatus.node_id) {
            Fail "master lease failover instances reported the same node_id: $($primaryStatus.node_id)"
        }

        Wait-LocalDualNodeCluster `
            -PrimaryBase $PrimaryBase `
            -PrimaryHeaders $PrimaryHeaders `
            -PrimaryNodeId $primaryStatus.node_id `
            -PrimaryHttpPort $HttpPort `
            -PrimaryNtripPort $PrimaryNtripPort `
            -PrimaryProcessId $serviceProcess.Id `
            -SecondaryBase $secondaryBase `
            -SecondaryHeaders $secondaryHeaders `
            -SecondaryNodeId $secondaryStatus.node_id `
            -SecondaryHttpPort $SecondaryHttpPort `
            -SecondaryNtripPort $SecondaryNtripPort `
            -SecondaryProcessId $secondaryNode.Process.Id `
            -TimeoutSec $StartupTimeoutSec | Out-Null

        $initialMaster = Wait-E2eMasterNodeInSet -ExpectedNodeIds @($primaryStatus.node_id, $secondaryStatus.node_id) -TimeoutSec ($StartupTimeoutSec + 5) -Context "master lease failover initial"
        Say "master lease initial holder=$initialMaster"

        $survivorBase = $null
        $survivorHeaders = $null
        $survivorNodeId = $null
        $survivorLabel = $null
        $retiredNodeId = $null

        if ($initialMaster -eq $primaryStatus.node_id) {
            $retiredNodeId = $primaryStatus.node_id
            $survivorNodeId = $secondaryStatus.node_id
            $survivorBase = $secondaryBase
            $survivorHeaders = $secondaryHeaders
            $survivorLabel = "secondary"

            Say "stopping current master primary node_id=$retiredNodeId process_id=$($serviceProcess.Id)"
            if (-not $serviceProcess -or $serviceProcess.HasExited) {
                Fail "primary CasterService process was not running before master failover stop"
            }
            Stop-Process -Id $serviceProcess.Id -Force
            if (-not $serviceProcess.WaitForExit(5000)) {
                Write-Warning "primary CasterService did not exit within 5 seconds"
            }
        }
        elseif ($initialMaster -eq $secondaryStatus.node_id) {
            $retiredNodeId = $secondaryStatus.node_id
            $survivorNodeId = $primaryStatus.node_id
            $survivorBase = $PrimaryBase
            $survivorHeaders = $PrimaryHeaders
            $survivorLabel = "primary"

            Say "stopping current master secondary node_id=$retiredNodeId process_id=$($secondaryNode.Process.Id)"
            Stop-E2eCasterServiceNode $secondaryNode
            $secondaryNode = $null
        }
        else {
            Fail "unexpected initial master node id: $initialMaster"
        }

        $failoverTimeout = $StartupTimeoutSec + 20
        $newMaster = Wait-E2eMasterNodeInSet -ExpectedNodeIds @($survivorNodeId) -TimeoutSec $failoverTimeout -Context "master lease failover after stopping $retiredNodeId"
        if ($newMaster -ne $survivorNodeId) {
            Fail "master lease failover selected unexpected node: expected=$survivorNodeId actual=$newMaster"
        }

        Assert-E2eHealth $survivorBase "master lease failover survivor $survivorLabel"
        $survivorStatus = Wait-E2eStatusMasterNode $survivorBase $survivorHeaders $survivorNodeId $StartupTimeoutSec "master lease failover survivor $survivorLabel"
        if ($survivorStatus.node_id -ne $survivorNodeId) {
            Fail "master lease failover survivor status node_id mismatch: expected=$survivorNodeId actual=$($survivorStatus.node_id)"
        }

        $cluster = Wait-E2eClusterMasterNode $survivorBase $survivorHeaders $survivorNodeId $retiredNodeId $StartupTimeoutSec "master lease failover survivor $survivorLabel"
        $retiredNode = Get-ClusterNodeByUid $cluster $retiredNodeId
        if ($retiredNode -and $retiredNode.is_master -eq $true) {
            Fail "master lease failover retired node $retiredNodeId is still marked as master"
        }
        if ($retiredNode -and $retiredNode.online -eq $true) {
            Say "master lease retired node $retiredNodeId still appears online in cluster TTL grace, but master_node has moved to $survivorNodeId"
        }

        if ($secondaryNode) {
            Stop-E2eCasterServiceNode $secondaryNode
            $secondaryNode = $null
        }
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            $secondaryConfRoot = $null
        }

        Say "PASS master lease failover smoke"
    }
    finally {
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove master lease failover service config: $($_.Exception.Message)"
            }
        }
    }
}

function Invoke-MasterLeaseStabilitySmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null
    $primaryBase = $PrimaryBase
    $primaryHeaders = $PrimaryHeaders
    $primaryNodeId = $null
    $secondaryBase = "http://${HttpBindAddr}:${SecondaryHttpPort}"
    $secondaryHeaders = $null
    $secondaryNodeId = $null

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $primaryBase $primaryHeaders "master lease stability primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "master lease stability primary"
        $primaryNodeId = $primaryStatus.node_id

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc030-" + [guid]::NewGuid().ToString())
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

        $secondaryNode = Start-E2eCasterServiceNode "nc030-secondary" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryHeaders = $secondarySession.Headers
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondaryHeaders "master lease stability secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "master lease stability secondary"
        $secondaryNodeId = $secondaryStatus.node_id

        if ($primaryNodeId -eq $secondaryNodeId) {
            Fail "master lease stability instances reported the same node_id: $primaryNodeId"
        }

        Wait-LocalDualNodeCluster `
            -PrimaryBase $primaryBase `
            -PrimaryHeaders $primaryHeaders `
            -PrimaryNodeId $primaryNodeId `
            -PrimaryHttpPort $HttpPort `
            -PrimaryNtripPort $PrimaryNtripPort `
            -PrimaryProcessId $serviceProcess.Id `
            -SecondaryBase $secondaryBase `
            -SecondaryHeaders $secondaryHeaders `
            -SecondaryNodeId $secondaryNodeId `
            -SecondaryHttpPort $SecondaryHttpPort `
            -SecondaryNtripPort $SecondaryNtripPort `
            -SecondaryProcessId $secondaryNode.Process.Id `
            -TimeoutSec $StartupTimeoutSec | Out-Null

        $initialMaster = Wait-E2eMasterNodeInSet -ExpectedNodeIds @($primaryNodeId, $secondaryNodeId) -TimeoutSec ($StartupTimeoutSec + 5) -Context "master lease stability initial"
        Say "master lease stability initial holder=$initialMaster"

        if ($initialMaster -eq $primaryNodeId) {
            $masterBase = $primaryBase
            $masterHeaders = $primaryHeaders
            $masterNodeId = $primaryNodeId
            $standbyNodeId = $secondaryNodeId
            $standbyLabel = "secondary"
        }
        else {
            $masterBase = $secondaryBase
            $masterHeaders = $secondaryHeaders
            $masterNodeId = $secondaryNodeId
            $standbyNodeId = $primaryNodeId
            $standbyLabel = "primary"
        }

        Say "stopping standby $standbyLabel node_id=$standbyNodeId"
        if ($standbyNodeId -eq $secondaryNodeId) {
            Stop-E2eCasterServiceNode $secondaryNode
            $secondaryNode = $null
        }
        else {
            if (-not $serviceProcess -or $serviceProcess.HasExited) {
                Fail "primary standby CasterService process was not running before stop"
            }
            Stop-Process -Id $serviceProcess.Id -Force
            if (-not $serviceProcess.WaitForExit(5000)) {
                Write-Warning "primary standby CasterService did not exit within 5 seconds"
            }
        }
        Start-Sleep -Seconds 2
        Assert-E2eHealth $masterBase "master lease stability after stopping standby"
        Wait-E2eMasterNodeInSet -ExpectedNodeIds @($masterNodeId) -TimeoutSec $StartupTimeoutSec -Context "master lease stability standby stop" | Out-Null
        Wait-E2eStatusMasterNode $masterBase $masterHeaders $masterNodeId $StartupTimeoutSec "master lease stability standby stop" | Out-Null
        Wait-E2eClusterMasterNode $masterBase $masterHeaders $masterNodeId $standbyNodeId $StartupTimeoutSec "master lease stability standby stop" | Out-Null
        Assert-E2eMasterNodeStable $masterBase $masterHeaders $masterNodeId $standbyNodeId 5 "master lease stability standby stop observation"

        Say "restarting standby $standbyLabel node_id=$standbyNodeId"
        if ($standbyNodeId -eq $secondaryNodeId) {
            $secondaryNode = Start-E2eCasterServiceNode "nc030-secondary-restart" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
            $secondarySession = Invoke-E2eLogin $secondaryBase
            $secondaryHeaders = $secondarySession.Headers
        }
        else {
            $primaryNode = Start-E2eCasterServiceNode "nc030-primary-restart" $serviceExe $releaseDir $confDir $primaryBase $StartupTimeoutSec
            $script:serviceProcess = $primaryNode.Process
            $primarySession = Invoke-E2eLogin $primaryBase
            $primaryHeaders = $primarySession.Headers
        }
        Start-Sleep -Seconds 2
        Wait-E2eMasterNodeInSet -ExpectedNodeIds @($masterNodeId) -TimeoutSec $StartupTimeoutSec -Context "master lease stability standby restart" | Out-Null
        Wait-E2eStatusMasterNode $masterBase $masterHeaders $masterNodeId $StartupTimeoutSec "master lease stability standby restart" | Out-Null
        Wait-E2eClusterMasterNode $masterBase $masterHeaders $masterNodeId "" $StartupTimeoutSec "master lease stability standby restart" | Out-Null
        Assert-E2eMasterNodeStable $masterBase $masterHeaders $masterNodeId "" 5 "master lease stability standby restart observation"

        if ($initialMaster -eq $primaryNodeId) {
            $restoredStandbyHeaders = $secondaryHeaders
            Wait-LocalDualNodeCluster `
                -PrimaryBase $primaryBase `
                -PrimaryHeaders $primaryHeaders `
                -PrimaryNodeId $primaryNodeId `
                -PrimaryHttpPort $HttpPort `
                -PrimaryNtripPort $PrimaryNtripPort `
                -PrimaryProcessId $serviceProcess.Id `
                -SecondaryBase $secondaryBase `
                -SecondaryHeaders $restoredStandbyHeaders `
                -SecondaryNodeId $secondaryNodeId `
                -SecondaryHttpPort $SecondaryHttpPort `
                -SecondaryNtripPort $SecondaryNtripPort `
                -SecondaryProcessId $secondaryNode.Process.Id `
                -TimeoutSec $StartupTimeoutSec | Out-Null

            Say "stopping current master primary node_id=$primaryNodeId process_id=$($serviceProcess.Id)"
            Stop-Process -Id $serviceProcess.Id -Force
            if (-not $serviceProcess.WaitForExit(5000)) {
                Write-Warning "primary master CasterService did not exit within 5 seconds"
            }
            $survivorBase = $secondaryBase
            $survivorHeaders = $secondaryHeaders
            $survivorNodeId = $secondaryNodeId
            $retiredNodeId = $primaryNodeId
        }
        else {
            Wait-LocalDualNodeCluster `
                -PrimaryBase $primaryBase `
                -PrimaryHeaders $primaryHeaders `
                -PrimaryNodeId $primaryNodeId `
                -PrimaryHttpPort $HttpPort `
                -PrimaryNtripPort $PrimaryNtripPort `
                -PrimaryProcessId $serviceProcess.Id `
                -SecondaryBase $secondaryBase `
                -SecondaryHeaders $secondaryHeaders `
                -SecondaryNodeId $secondaryNodeId `
                -SecondaryHttpPort $SecondaryHttpPort `
                -SecondaryNtripPort $SecondaryNtripPort `
                -SecondaryProcessId $secondaryNode.Process.Id `
                -TimeoutSec $StartupTimeoutSec | Out-Null

            Say "stopping current master secondary node_id=$secondaryNodeId process_id=$($secondaryNode.Process.Id)"
            Stop-E2eCasterServiceNode $secondaryNode
            $secondaryNode = $null
            $survivorBase = $primaryBase
            $survivorHeaders = $primaryHeaders
            $survivorNodeId = $primaryNodeId
            $retiredNodeId = $secondaryNodeId
        }

        Wait-E2eMasterNodeInSet -ExpectedNodeIds @($survivorNodeId) -TimeoutSec ($StartupTimeoutSec + 20) -Context "master lease stability master stop" | Out-Null
        Wait-E2eStatusMasterNode $survivorBase $survivorHeaders $survivorNodeId $StartupTimeoutSec "master lease stability master stop" | Out-Null
        Wait-E2eClusterMasterNode $survivorBase $survivorHeaders $survivorNodeId $retiredNodeId $StartupTimeoutSec "master lease stability master stop" | Out-Null
        Assert-E2eMasterNodeStable $survivorBase $survivorHeaders $survivorNodeId $retiredNodeId 5 "master lease stability master stop observation"

        Say "restarting retired master node_id=$retiredNodeId to verify it does not form a second master"
        if ($retiredNodeId -eq $primaryNodeId) {
            $primaryNode = Start-E2eCasterServiceNode "nc030-primary-return" $serviceExe $releaseDir $confDir $primaryBase $StartupTimeoutSec
            $script:serviceProcess = $primaryNode.Process
            $primarySession = Invoke-E2eLogin $primaryBase
            $primaryHeaders = $primarySession.Headers
        }
        else {
            $secondaryNode = Start-E2eCasterServiceNode "nc030-secondary-return" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
            $secondarySession = Invoke-E2eLogin $secondaryBase
            $secondaryHeaders = $secondarySession.Headers
        }

        Start-Sleep -Seconds 2
        Wait-E2eMasterNodeInSet -ExpectedNodeIds @($survivorNodeId) -TimeoutSec $StartupTimeoutSec -Context "master lease stability retired restart" | Out-Null
        Wait-E2eStatusMasterNode $survivorBase $survivorHeaders $survivorNodeId $StartupTimeoutSec "master lease stability retired restart" | Out-Null
        Wait-E2eClusterMasterNode $survivorBase $survivorHeaders $survivorNodeId $retiredNodeId $StartupTimeoutSec "master lease stability retired restart" | Out-Null
        Assert-E2eMasterNodeStable $survivorBase $survivorHeaders $survivorNodeId $retiredNodeId 5 "master lease stability retired restart observation"

        Say "PASS master lease stability smoke"
    }
    finally {
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove master lease stability service config: $($_.Exception.Message)"
            }
        }
    }
}

function Invoke-RelayPullStartStopSmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort,
        [switch]$AssertDataForwarding
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null
    $sourceConnection = $null
    $forwardClientConnection = $null
    $seed = $null
    $pullUid = $null
    $relaySession = $null

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $PrimaryBase $PrimaryHeaders "relay pull primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "relay pull primary"

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc026-" + [guid]::NewGuid().ToString())
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
        $secondaryNode = Start-E2eCasterServiceNode "nc026-relay-target" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondarySession.Headers "relay pull secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "relay pull secondary"

        if ($primaryStatus.node_id -eq $secondaryStatus.node_id) {
            Fail "relay pull smoke requires distinct local node ids, both were $($primaryStatus.node_id)"
        }

        $baselineNode = Wait-ClusterPullCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id 0 $StartupTimeoutSec "relay pull baseline"
        $baselinePull = [int]$baselineNode.pull

        $pullConnectionLimit = if ($AssertDataForwarding) { 2 } else { 1 }
        $pullLabel = if ($AssertDataForwarding) { "nc028_relay_pull_data" } else { "nc026_relay_pull" }
        $seed = New-NtripAuthSessionSeed -Label $pullLabel -ConnectionLimit $pullConnectionLimit
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed
        $sourceConnection = Open-NtripSourceForSeed $seed $HttpBindAddr $SecondaryNtripPort "relay-target-source"
        $script:ntripSourceConnection = $sourceConnection

        $pullUid = "$($seed.Prefix)_pull"
        $pullBody = [ordered]@{
            uid = $pullUid
            login_mpt = $seed.Mount
            type = 2
            target_ip = $HttpBindAddr
            target_port = $SecondaryNtripPort
            target_mpt = $seed.Mount
            target_account = $seed.Account
            target_password = $seed.Password
            enabled = $true
        } | ConvertTo-Json -Compress

        Say "creating pull relay uid=$pullUid target=$HttpBindAddr`:$SecondaryNtripPort mount=$($seed.Mount)"
        $create = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -ContentType "application/json" -Body $pullBody -Uri "$PrimaryBase/api/relays/pull" -TimeoutSec 10
        if ($create.uid -ne $pullUid) {
            Fail "create pull relay response uid mismatch: expected=$pullUid actual=$($create.uid)"
        }

        $running = Wait-PullRelayRunning $PrimaryBase $PrimaryHeaders $pullUid $primaryStatus.node_id $StartupTimeoutSec "relay pull create"
        $relaySession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $relaySession.Field
        Wait-NtripOnlineExactFields $seed @($relaySession.Field) $StartupTimeoutSec "relay pull target named auth" | Out-Null
        $anonymousRecords = @(Get-NtripAnonymousClientRecords $seed)
        if ($anonymousRecords.Count -ne 0) {
            Fail "relay pull target unexpectedly used anonymous rover auth: $(Format-DebugText (Format-NtripAnonymousRecords $seed))"
        }
        Wait-ClusterPullCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id ($baselinePull + 1) $StartupTimeoutSec "relay pull create" | Out-Null

        if ($AssertDataForwarding) {
            $forwardClientConnection = Open-NtripClientForSeed $seed $HttpBindAddr $PrimaryNtripPort "relay-pull-forward-client"
            $payload = "NC028-PULL-DATA:$($seed.Prefix):0123456789"
            Start-Sleep -Milliseconds 500
            Write-NtripPayload $sourceConnection $payload "relay pull data forwarding"
            Wait-NtripPayload $forwardClientConnection $payload $StartupTimeoutSec "relay pull data forwarding"
            $forwardSessionMap = Get-NtripSessionMap $seed
            $forwardFields = @($forwardSessionMap.Records.Keys | Where-Object { $_ -ne $relaySession.Field })
            if ($forwardFields.Count -gt 0) {
                $seed.ExtraClientConnectKeys += @($forwardFields)
            }
            Close-NtripTcpConnection $forwardClientConnection
            $forwardClientConnection = $null
            Wait-NtripOnlineExactFields $seed @($relaySession.Field) $StartupTimeoutSec "relay pull data client cleanup" | Out-Null
        }

        Say "stopping pull relay uid=$pullUid"
        $stop = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -Uri "$PrimaryBase/api/relays/pull/stop/$pullUid" -TimeoutSec 10
        if ($stop.ok -ne $true) {
            Fail "stop pull relay did not return ok"
        }
        $stoppedRecord = Get-PullRelayRecordFromHttp $PrimaryBase $PrimaryHeaders $pullUid
        if ($stoppedRecord.enabled -ne $false) {
            Fail "stop pull relay did not set enabled=false"
        }
        Wait-PullRelayNotRunning $PrimaryBase $PrimaryHeaders $pullUid $StartupTimeoutSec "relay pull stop"
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "relay pull stop named auth cleanup" | Out-Null
        Wait-ClusterPullCountAtMost $PrimaryBase $PrimaryHeaders $primaryStatus.node_id $baselinePull $StartupTimeoutSec "relay pull stop" | Out-Null
        $relaySession = $null

        Say "restarting pull relay uid=$pullUid"
        $start = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -Uri "$PrimaryBase/api/relays/pull/start/$pullUid" -TimeoutSec 10
        if ($start.ok -ne $true) {
            Fail "start pull relay did not return ok"
        }
        $startedRecord = Get-PullRelayRecordFromHttp $PrimaryBase $PrimaryHeaders $pullUid
        if ($startedRecord.enabled -ne $true) {
            Fail "start pull relay did not set enabled=true"
        }
        $restarted = Wait-PullRelayRunning $PrimaryBase $PrimaryHeaders $pullUid $primaryStatus.node_id $StartupTimeoutSec "relay pull restart"
        if ($restarted.connect_key -eq $running.connect_key) {
            Say "relay pull restart reused connect_key=$($restarted.connect_key)"
        }
        $relaySession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $relaySession.Field
        Wait-NtripOnlineExactFields $seed @($relaySession.Field) $StartupTimeoutSec "relay pull restart target named auth" | Out-Null
        Wait-ClusterPullCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id ($baselinePull + 1) $StartupTimeoutSec "relay pull restart" | Out-Null

        Remove-PullRelayRecord $PrimaryBase $PrimaryHeaders $pullUid
        Wait-PullRelayNotRunning $PrimaryBase $PrimaryHeaders $pullUid $StartupTimeoutSec "relay pull cleanup"
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "relay pull cleanup named auth" | Out-Null
        Wait-ClusterPullCountAtMost $PrimaryBase $PrimaryHeaders $primaryStatus.node_id $baselinePull $StartupTimeoutSec "relay pull cleanup" | Out-Null
        $pullUid = $null
        $relaySession = $null

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

        Say "PASS relay pull start/stop smoke"
    }
    finally {
        if ($pullUid) {
            Remove-PullRelayRecord $PrimaryBase $PrimaryHeaders $pullUid
        }
        Close-NtripTcpConnection $forwardClientConnection
        Close-NtripTcpConnection $sourceConnection
        if ($script:ntripSourceConnection -eq $sourceConnection) {
            $script:ntripSourceConnection = $null
        }
        if ($script:ntripAuthSeed -eq $seed) {
            $script:ntripAuthSeed = $null
        }
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove relay pull service config: $($_.Exception.Message)"
            }
        }
    }
}

function Invoke-RelayFailoverSmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null
    $sourceConnection = $null
    $seed = $null
    $pullUid = $null
    $primaryNodeId = $null
    $primaryHeaders = $PrimaryHeaders
    $secondaryHeaders = $null
    $secondaryBase = "http://${HttpBindAddr}:${SecondaryHttpPort}"

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $PrimaryBase $primaryHeaders "relay failover primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "relay failover primary"
        $primaryNodeId = $primaryStatus.node_id

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc033-" + [guid]::NewGuid().ToString())
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

        $secondaryNode = Start-E2eCasterServiceNode "nc033-relay-survivor" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryHeaders = $secondarySession.Headers
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondaryHeaders "relay failover secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "relay failover secondary"
        $secondaryNodeId = $secondaryStatus.node_id

        if ($primaryNodeId -eq $secondaryNodeId) {
            Fail "relay failover smoke requires distinct local node ids, both were $primaryNodeId"
        }

        Wait-LocalDualNodeCluster `
            -PrimaryBase $PrimaryBase `
            -PrimaryHeaders $primaryHeaders `
            -PrimaryNodeId $primaryNodeId `
            -PrimaryHttpPort $HttpPort `
            -PrimaryNtripPort $PrimaryNtripPort `
            -PrimaryProcessId $serviceProcess.Id `
            -SecondaryBase $secondaryBase `
            -SecondaryHeaders $secondaryHeaders `
            -SecondaryNodeId $secondaryNodeId `
            -SecondaryHttpPort $SecondaryHttpPort `
            -SecondaryNtripPort $SecondaryNtripPort `
            -SecondaryProcessId $secondaryNode.Process.Id `
            -TimeoutSec $StartupTimeoutSec | Out-Null

        $initialMaster = Wait-E2eMasterNodeInSet -ExpectedNodeIds @($primaryNodeId, $secondaryNodeId) -TimeoutSec ($StartupTimeoutSec + 5) -Context "relay failover initial"
        Say "relay failover initial master=$initialMaster"

        if ($initialMaster -eq $primaryNodeId) {
            $masterBase = $PrimaryBase
            $masterHeaders = $primaryHeaders
            $masterNodeId = $primaryNodeId
            $survivorBase = $secondaryBase
            $survivorHeaders = $secondaryHeaders
            $survivorNodeId = $secondaryNodeId
            $survivorNtripPort = $SecondaryNtripPort
        }
        else {
            $masterBase = $secondaryBase
            $masterHeaders = $secondaryHeaders
            $masterNodeId = $secondaryNodeId
            $survivorBase = $PrimaryBase
            $survivorHeaders = $primaryHeaders
            $survivorNodeId = $primaryNodeId
            $survivorNtripPort = $PrimaryNtripPort
        }

        $baselineMasterNode = Wait-ClusterPullCountAtLeast $masterBase $masterHeaders $masterNodeId 0 $StartupTimeoutSec "relay failover master baseline"
        $baselineMasterPull = [int]$baselineMasterNode.pull
        $baselineSurvivorNode = Wait-ClusterPullCountAtLeast $survivorBase $survivorHeaders $survivorNodeId 0 $StartupTimeoutSec "relay failover survivor baseline"
        $baselineSurvivorPull = [int]$baselineSurvivorNode.pull

        $seed = New-NtripAuthSessionSeed -Label "nc033_relay_failover" -ConnectionLimit 2
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed
        $sourceConnection = Open-NtripSourceForSeed $seed $HttpBindAddr $survivorNtripPort "relay-failover-target-source"
        $script:ntripSourceConnection = $sourceConnection

        $pullUid = "$($seed.Prefix)_pull_failover"
        $pullBody = [ordered]@{
            uid = $pullUid
            login_mpt = $seed.Mount
            type = 2
            target_ip = $HttpBindAddr
            target_port = $survivorNtripPort
            target_mpt = $seed.Mount
            target_account = $seed.Account
            target_password = $seed.Password
            enabled = $true
        } | ConvertTo-Json -Compress

        Say "creating relay failover pull uid=$pullUid via master=$masterNodeId target_port=$survivorNtripPort"
        $create = Invoke-RestMethod -Method Post -Headers $masterHeaders -ContentType "application/json" -Body $pullBody -Uri "$masterBase/api/relays/pull" -TimeoutSec 10
        if ($create.uid -ne $pullUid) {
            Fail "relay failover create pull response uid mismatch: expected=$pullUid actual=$($create.uid)"
        }

        $initialRunning = Wait-PullRelayRunning $masterBase $masterHeaders $pullUid $masterNodeId $StartupTimeoutSec "relay failover initial running"
        $initialRelaySession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $initialRelaySession.Field
        Wait-NtripOnlineExactFields $seed @($initialRelaySession.Field) $StartupTimeoutSec "relay failover initial target auth" | Out-Null
        Wait-ClusterPullCountAtLeast $masterBase $masterHeaders $masterNodeId ($baselineMasterPull + 1) $StartupTimeoutSec "relay failover initial cluster count" | Out-Null

        Say "stopping relay executor/master node=$masterNodeId for failover"
        if ($masterNodeId -eq $primaryNodeId) {
            Stop-Process -Id $serviceProcess.Id -Force
            if (-not $serviceProcess.WaitForExit(5000)) {
                Write-Warning "relay failover primary CasterService did not exit within 5 seconds"
            }
        }
        else {
            Stop-E2eCasterServiceNode $secondaryNode
            $secondaryNode = $null
        }
        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        $script:ntripSourceConnection = $null
        $seed.ClientConnectKey = ""
        Start-Sleep -Seconds 2

        Wait-E2eMasterNodeInSet -ExpectedNodeIds @($survivorNodeId) -TimeoutSec ($StartupTimeoutSec + 20) -Context "relay failover master stop" | Out-Null
        Wait-E2eStatusMasterNode $survivorBase $survivorHeaders $survivorNodeId $StartupTimeoutSec "relay failover survivor master" | Out-Null
        Wait-E2eClusterMasterNode $survivorBase $survivorHeaders $survivorNodeId $masterNodeId $StartupTimeoutSec "relay failover survivor master" | Out-Null

        $sourceConnection = Open-NtripSourceForSeed $seed $HttpBindAddr $survivorNtripPort "relay-failover-target-source-after-stop"
        $script:ntripSourceConnection = $sourceConnection

        Wait-PullRelayNotRunningOnNode $survivorBase $survivorHeaders $pullUid $masterNodeId ($StartupTimeoutSec + 15) "relay failover retired executor"
        $recovered = Wait-PullRelayRunning $survivorBase $survivorHeaders $pullUid $survivorNodeId ($StartupTimeoutSec + 30) "relay failover recovered running"
        if ($recovered.connect_key -eq $initialRunning.connect_key) {
            Fail "relay failover recovered relay reused old connect_key: $($recovered.connect_key)"
        }
        $recoveredRelaySession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $recoveredRelaySession.Field
        Wait-NtripOnlineExactFields $seed @($recoveredRelaySession.Field) $StartupTimeoutSec "relay failover recovered target auth" | Out-Null
        Wait-ClusterPullCountAtLeast $survivorBase $survivorHeaders $survivorNodeId ($baselineSurvivorPull + 1) $StartupTimeoutSec "relay failover survivor cluster count" | Out-Null

        Remove-PullRelayRecord $survivorBase $survivorHeaders $pullUid
        Wait-PullRelayNotRunning $survivorBase $survivorHeaders $pullUid $StartupTimeoutSec "relay failover cleanup"
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "relay failover cleanup target auth" | Out-Null
        Wait-ClusterPullCountAtMost $survivorBase $survivorHeaders $survivorNodeId $baselineSurvivorPull $StartupTimeoutSec "relay failover cleanup cluster count" | Out-Null
        $pullUid = $null

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        $script:ntripSourceConnection = $null
        Remove-NtripAuthSessionSeed $seed
        $seed = $null
        $script:ntripAuthSeed = $null

        Say "PASS relay failover smoke"
    }
    finally {
        if ($pullUid) {
            if ($survivorBase -and $survivorHeaders) {
                Remove-PullRelayRecord $survivorBase $survivorHeaders $pullUid
            }
            elseif ($PrimaryBase -and $primaryHeaders) {
                Remove-PullRelayRecord $PrimaryBase $primaryHeaders $pullUid
            }
        }
        Close-NtripTcpConnection $sourceConnection
        if ($script:ntripSourceConnection -eq $sourceConnection) {
            $script:ntripSourceConnection = $null
        }
        if ($script:ntripAuthSeed -eq $seed) {
            $script:ntripAuthSeed = $null
        }
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove relay failover service config: $($_.Exception.Message)"
            }
        }
    }
}

function Invoke-RelayPushFailoverSmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null
    $sourceConnection = $null
    $forwardClientConnection = $null
    $seed = $null
    $pushUid = $null
    $targetSource = $null
    $primaryNodeId = $null
    $primaryHeaders = $PrimaryHeaders
    $secondaryHeaders = $null
    $secondaryBase = "http://${HttpBindAddr}:${SecondaryHttpPort}"

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $PrimaryBase $primaryHeaders "relay push failover primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "relay push failover primary"
        $primaryNodeId = $primaryStatus.node_id

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc039-" + [guid]::NewGuid().ToString())
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
            -RoverOnlineProtection $true `
            -RoverAnonymousLogin $false `
            -BaseAnonymousLogin $false `
            -SourceAnonymousLogin $false

        $secondaryNode = Start-E2eCasterServiceNode "nc039-relay-push-survivor" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryHeaders = $secondarySession.Headers
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondaryHeaders "relay push failover secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "relay push failover secondary"
        $secondaryNodeId = $secondaryStatus.node_id

        if ($primaryNodeId -eq $secondaryNodeId) {
            Fail "relay push failover smoke requires distinct local node ids, both were $primaryNodeId"
        }

        Wait-LocalDualNodeCluster `
            -PrimaryBase $PrimaryBase `
            -PrimaryHeaders $primaryHeaders `
            -PrimaryNodeId $primaryNodeId `
            -PrimaryHttpPort $HttpPort `
            -PrimaryNtripPort $PrimaryNtripPort `
            -PrimaryProcessId $serviceProcess.Id `
            -SecondaryBase $secondaryBase `
            -SecondaryHeaders $secondaryHeaders `
            -SecondaryNodeId $secondaryNodeId `
            -SecondaryHttpPort $SecondaryHttpPort `
            -SecondaryNtripPort $SecondaryNtripPort `
            -SecondaryProcessId $secondaryNode.Process.Id `
            -TimeoutSec $StartupTimeoutSec | Out-Null

        $initialMaster = Wait-E2eMasterNodeInSet -ExpectedNodeIds @($primaryNodeId, $secondaryNodeId) -TimeoutSec ($StartupTimeoutSec + 5) -Context "relay push failover initial"
        Say "relay push failover initial master=$initialMaster"

        if ($initialMaster -eq $primaryNodeId) {
            $masterBase = $PrimaryBase
            $masterHeaders = $primaryHeaders
            $masterNodeId = $primaryNodeId
            $masterNtripPort = $PrimaryNtripPort
            $survivorBase = $secondaryBase
            $survivorHeaders = $secondaryHeaders
            $survivorNodeId = $secondaryNodeId
            $survivorNtripPort = $SecondaryNtripPort
        }
        else {
            $masterBase = $secondaryBase
            $masterHeaders = $secondaryHeaders
            $masterNodeId = $secondaryNodeId
            $masterNtripPort = $SecondaryNtripPort
            $survivorBase = $PrimaryBase
            $survivorHeaders = $primaryHeaders
            $survivorNodeId = $primaryNodeId
            $survivorNtripPort = $PrimaryNtripPort
        }

        $baselineMasterNode = Wait-ClusterPushCountAtLeast $masterBase $masterHeaders $masterNodeId 0 $StartupTimeoutSec "relay push failover master baseline"
        $baselineMasterPush = [int]$baselineMasterNode.push
        $baselineSurvivorNode = Wait-ClusterPushCountAtLeast $survivorBase $survivorHeaders $survivorNodeId 0 $StartupTimeoutSec "relay push failover survivor baseline"
        $baselineSurvivorPush = [int]$baselineSurvivorNode.push

        $seed = New-NtripAuthSessionSeed -Label "nc039_relay_push_failover" -ConnectionLimit 2
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed
        Add-NtripSourceAuthAccountSeed $seed
        $sourceConnection = Open-NtripSourceForSeed $seed $HttpBindAddr $masterNtripPort "relay-push-failover-source"
        $script:ntripSourceConnection = $sourceConnection

        $pushUid = "$($seed.Prefix)_push_failover"
        $pushBody = [ordered]@{
            uid = $pushUid
            login_mpt = $seed.Mount
            type = 2
            target_ip = $HttpBindAddr
            target_port = $survivorNtripPort
            target_mpt = $seed.TargetMount
            target_account = $seed.SourceUser
            target_password = $seed.SourcePassword
            enabled = $true
        } | ConvertTo-Json -Compress

        Say "creating relay push failover uid=$pushUid via master=$masterNodeId target_port=$survivorNtripPort source_mount=$($seed.Mount) target_mount=$($seed.TargetMount)"
        $create = Invoke-RestMethod -Method Post -Headers $masterHeaders -ContentType "application/json" -Body $pushBody -Uri "$masterBase/api/relays/push" -TimeoutSec 10
        if ($create.uid -ne $pushUid) {
            Fail "relay push failover create response uid mismatch: expected=$pushUid actual=$($create.uid)"
        }

        $initialRunning = Wait-PushRelayRunning $masterBase $masterHeaders $pushUid $masterNodeId $StartupTimeoutSec "relay push failover initial running"
        $targetSource = Wait-PushedSourceActive $seed $seed.TargetMount @($seed.SourceConnectKey, $initialRunning.connect_key) $StartupTimeoutSec "relay push failover initial target source"
        if ($targetSource.ConnectKey -eq $seed.SourceConnectKey -or $targetSource.ConnectKey -eq $initialRunning.connect_key) {
            Fail "relay push failover initial target source connect_key must be distinct from source/relay keys: target=$($targetSource.ConnectKey) source=$($seed.SourceConnectKey) relay=$($initialRunning.connect_key)"
        }
        $seed.TargetSourceConnectKey = $targetSource.ConnectKey
        Wait-ClusterPushCountAtLeast $masterBase $masterHeaders $masterNodeId ($baselineMasterPush + 1) $StartupTimeoutSec "relay push failover initial cluster count" | Out-Null

        $forwardClientConnection = Open-NtripClientForMount `
            -Seed $seed `
            -Mount $seed.TargetMount `
            -NtripHost $HttpBindAddr `
            -NtripPort $survivorNtripPort `
            -Label "relay-push-failover-initial-client"
        $initialForwardSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $initialForwardSession.Field
        $initialPayload = "NC039-PUSH-FAILOVER-BEFORE:$($seed.Prefix):abcdef"
        Start-Sleep -Milliseconds 500
        Write-NtripPayload $sourceConnection $initialPayload "relay push failover initial data"
        Wait-NtripPayload $forwardClientConnection $initialPayload $StartupTimeoutSec "relay push failover initial data"
        Close-NtripTcpConnection $forwardClientConnection
        $forwardClientConnection = $null
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "relay push failover initial client cleanup" | Out-Null

        Say "stopping relay push executor/master node=$masterNodeId for failover"
        if ($masterNodeId -eq $primaryNodeId) {
            Stop-Process -Id $serviceProcess.Id -Force
            if (-not $serviceProcess.WaitForExit(5000)) {
                Write-Warning "relay push failover primary CasterService did not exit within 5 seconds"
            }
        }
        else {
            Stop-E2eCasterServiceNode $secondaryNode
            $secondaryNode = $null
        }
        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        $script:ntripSourceConnection = $null
        $seed.SourceConnectKey = ""
        $seed.TargetSourceConnectKey = ""
        $seed.ClientConnectKey = ""
        Start-Sleep -Seconds 2

        Wait-E2eMasterNodeInSet -ExpectedNodeIds @($survivorNodeId) -TimeoutSec ($StartupTimeoutSec + 20) -Context "relay push failover master stop" | Out-Null
        Wait-E2eStatusMasterNode $survivorBase $survivorHeaders $survivorNodeId $StartupTimeoutSec "relay push failover survivor master" | Out-Null
        Wait-E2eClusterMasterNode $survivorBase $survivorHeaders $survivorNodeId $masterNodeId $StartupTimeoutSec "relay push failover survivor master" | Out-Null
        $retiredTargetSourceConnectKey = $targetSource.ConnectKey
        Wait-PushedSourceGone $seed $seed.TargetMount $retiredTargetSourceConnectKey ($StartupTimeoutSec + 15) "relay push failover retired target source"

        $sourceConnection = Open-NtripSourceForSeed $seed $HttpBindAddr $survivorNtripPort "relay-push-failover-source-after-stop"
        $script:ntripSourceConnection = $sourceConnection

        Wait-PushRelayNotRunningOnNode $survivorBase $survivorHeaders $pushUid $masterNodeId ($StartupTimeoutSec + 15) "relay push failover retired executor"
        $recovered = Wait-PushRelayRunning $survivorBase $survivorHeaders $pushUid $survivorNodeId ($StartupTimeoutSec + 30) "relay push failover recovered running"
        if ($recovered.connect_key -eq $initialRunning.connect_key) {
            Fail "relay push failover recovered relay reused old connect_key: $($recovered.connect_key)"
        }
        $targetSource = Wait-PushedSourceActive $seed $seed.TargetMount @($seed.SourceConnectKey, $recovered.connect_key, $retiredTargetSourceConnectKey) $StartupTimeoutSec "relay push failover recovered target source"
        if ($targetSource.ConnectKey -eq $seed.SourceConnectKey -or $targetSource.ConnectKey -eq $recovered.connect_key) {
            Fail "relay push failover recovered target source connect_key must be distinct from source/relay keys: target=$($targetSource.ConnectKey) source=$($seed.SourceConnectKey) relay=$($recovered.connect_key)"
        }
        $seed.TargetSourceConnectKey = $targetSource.ConnectKey
        Wait-ClusterPushCountAtLeast $survivorBase $survivorHeaders $survivorNodeId ($baselineSurvivorPush + 1) $StartupTimeoutSec "relay push failover survivor cluster count" | Out-Null

        $forwardClientConnection = Open-NtripClientForMount `
            -Seed $seed `
            -Mount $seed.TargetMount `
            -NtripHost $HttpBindAddr `
            -NtripPort $survivorNtripPort `
            -Label "relay-push-failover-recovered-client"
        $recoveredForwardSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
        $seed.ClientConnectKey = $recoveredForwardSession.Field
        $recoveredPayload = "NC039-PUSH-FAILOVER-AFTER:$($seed.Prefix):fedcba"
        Start-Sleep -Milliseconds 500
        Write-NtripPayload $sourceConnection $recoveredPayload "relay push failover recovered data"
        Wait-NtripPayload $forwardClientConnection $recoveredPayload $StartupTimeoutSec "relay push failover recovered data"
        Close-NtripTcpConnection $forwardClientConnection
        $forwardClientConnection = $null
        Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "relay push failover recovered client cleanup" | Out-Null

        Remove-PushRelayRecord $survivorBase $survivorHeaders $pushUid
        Wait-PushRelayNotRunning $survivorBase $survivorHeaders $pushUid $StartupTimeoutSec "relay push failover cleanup"
        Wait-PushedSourceGone $seed $seed.TargetMount $targetSource.ConnectKey $StartupTimeoutSec "relay push failover cleanup target source"
        Wait-ClusterPushCountAtMost $survivorBase $survivorHeaders $survivorNodeId $baselineSurvivorPush $StartupTimeoutSec "relay push failover cleanup cluster count" | Out-Null
        $pushUid = $null
        $seed.TargetSourceConnectKey = ""
        $targetSource = $null

        Close-NtripTcpConnection $sourceConnection
        $sourceConnection = $null
        $script:ntripSourceConnection = $null
        Remove-NtripAuthSessionSeed $seed
        $seed = $null
        $script:ntripAuthSeed = $null

        Say "PASS relay push failover smoke"
    }
    finally {
        if ($pushUid) {
            if ($survivorBase -and $survivorHeaders) {
                Remove-PushRelayRecord $survivorBase $survivorHeaders $pushUid
            }
            elseif ($PrimaryBase -and $primaryHeaders) {
                Remove-PushRelayRecord $PrimaryBase $primaryHeaders $pushUid
            }
        }
        Close-NtripTcpConnection $forwardClientConnection
        Close-NtripTcpConnection $sourceConnection
        if ($script:ntripSourceConnection -eq $sourceConnection) {
            $script:ntripSourceConnection = $null
        }
        if ($script:ntripAuthSeed -eq $seed) {
            $script:ntripAuthSeed = $null
        }
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove relay push failover service config: $($_.Exception.Message)"
            }
        }
    }
}

function Invoke-RelayPushStartStopSmoke {
    param(
        [string]$PrimaryBase,
        [hashtable]$PrimaryHeaders,
        [int]$PrimaryNtripPort,
        [int]$SecondaryHttpPort,
        [int]$SecondaryNtripPort,
        [switch]$AssertDataForwarding
    )

    $secondaryNode = $null
    $secondaryConfRoot = $null
    $sourceConnection = $null
    $forwardClientConnection = $null
    $seed = $null
    $pushUid = $null
    $runningStatus = $null
    $targetSource = $null

    try {
        $primaryStatus = Assert-E2eStatusAndCluster $PrimaryBase $PrimaryHeaders "relay push primary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $primaryStatus.node_id "relay push primary"

        $secondaryConfRoot = Join-Path $env:TEMP ("navcaster-e2e-nc027-" + [guid]::NewGuid().ToString())
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
            -RoverOnlineProtection $true `
            -RoverAnonymousLogin $false `
            -BaseAnonymousLogin $false `
            -SourceAnonymousLogin $false

        $secondaryBase = "http://${HttpBindAddr}:${SecondaryHttpPort}"
        $secondaryNode = Start-E2eCasterServiceNode "nc027-relay-target" $serviceExe $releaseDir $secondaryConfDir $secondaryBase $StartupTimeoutSec
        $secondarySession = Invoke-E2eLogin $secondaryBase
        $secondaryStatus = Assert-E2eStatusAndCluster $secondaryBase $secondarySession.Headers "relay push secondary" -RequireRedisConnected
        Assert-E2eNodeIdFormat $secondaryStatus.node_id "relay push secondary"

        if ($primaryStatus.node_id -eq $secondaryStatus.node_id) {
            Fail "relay push smoke requires distinct local node ids, both were $($primaryStatus.node_id)"
        }

        $baselineNode = Wait-ClusterPushCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id 0 $StartupTimeoutSec "relay push baseline"
        $baselinePush = [int]$baselineNode.push

        $pushLabel = if ($AssertDataForwarding) { "nc028_relay_push_data" } else { "nc027_relay_push" }
        $seed = New-NtripAuthSessionSeed -Label $pushLabel
        $script:ntripAuthSeed = $seed
        Add-NtripAuthAccountSeed $seed
        Add-NtripSourceAuthAccountSeed $seed
        $sourceConnection = Open-NtripSourceForSeed $seed $HttpBindAddr $PrimaryNtripPort "push-local-source"
        $script:ntripSourceConnection = $sourceConnection

        $pushUid = "$($seed.Prefix)_push"
        $pushBody = [ordered]@{
            uid = $pushUid
            login_mpt = $seed.Mount
            type = 2
            target_ip = $HttpBindAddr
            target_port = $SecondaryNtripPort
            target_mpt = $seed.TargetMount
            target_account = $seed.SourceUser
            target_password = $seed.SourcePassword
            enabled = $true
        } | ConvertTo-Json -Compress

        Say "creating push relay uid=$pushUid target=$HttpBindAddr`:$SecondaryNtripPort source_mount=$($seed.Mount) target_mount=$($seed.TargetMount)"
        $create = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -ContentType "application/json" -Body $pushBody -Uri "$PrimaryBase/api/relays/push" -TimeoutSec 10
        if ($create.uid -ne $pushUid) {
            Fail "create push relay response uid mismatch: expected=$pushUid actual=$($create.uid)"
        }

        $runningStatus = Wait-PushRelayRunning $PrimaryBase $PrimaryHeaders $pushUid $primaryStatus.node_id $StartupTimeoutSec "relay push create"
        $targetSource = Wait-PushedSourceActive $seed $seed.TargetMount @($seed.SourceConnectKey, $runningStatus.connect_key) $StartupTimeoutSec "relay push target source"
        if ($targetSource.ConnectKey -eq $seed.SourceConnectKey -or $targetSource.ConnectKey -eq $runningStatus.connect_key) {
            Fail "relay push target source connect_key must be distinct from source/relay keys: target=$($targetSource.ConnectKey) source=$($seed.SourceConnectKey) relay=$($runningStatus.connect_key)"
        }
        $seed.TargetSourceConnectKey = $targetSource.ConnectKey
        Wait-ClusterPushCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id ($baselinePush + 1) $StartupTimeoutSec "relay push create" | Out-Null

        if ($AssertDataForwarding) {
            $forwardClientConnection = Open-NtripClientForMount `
                -Seed $seed `
                -Mount $seed.TargetMount `
                -NtripHost $HttpBindAddr `
                -NtripPort $SecondaryNtripPort `
                -Label "relay-push-forward-client"
            $forwardSession = Wait-NtripActiveSession $seed $StartupTimeoutSec
            $seed.ClientConnectKey = $forwardSession.Field
            $payload = "NC028-PUSH-DATA:$($seed.Prefix):9876543210"
            Start-Sleep -Milliseconds 500
            Write-NtripPayload $sourceConnection $payload "relay push data forwarding"
            Wait-NtripPayload $forwardClientConnection $payload $StartupTimeoutSec "relay push data forwarding"
            Close-NtripTcpConnection $forwardClientConnection
            $forwardClientConnection = $null
            Wait-NtripOnlineExactFields $seed @() $StartupTimeoutSec "relay push data client cleanup" | Out-Null
        }

        Say "stopping push relay uid=$pushUid"
        $stop = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -Uri "$PrimaryBase/api/relays/push/stop/$pushUid" -TimeoutSec 10
        if ($stop.ok -ne $true) {
            Fail "stop push relay did not return ok"
        }
        $stoppedRecord = Get-PushRelayRecordFromHttp $PrimaryBase $PrimaryHeaders $pushUid
        if ($stoppedRecord.enabled -ne $false) {
            Fail "stop push relay did not set enabled=false"
        }
        Wait-PushRelayNotRunning $PrimaryBase $PrimaryHeaders $pushUid $StartupTimeoutSec "relay push stop"
        Wait-PushedSourceGone $seed $seed.TargetMount $targetSource.ConnectKey $StartupTimeoutSec "relay push stop target cleanup"
        Wait-ClusterPushCountAtMost $PrimaryBase $PrimaryHeaders $primaryStatus.node_id $baselinePush $StartupTimeoutSec "relay push stop" | Out-Null
        $seed.TargetSourceConnectKey = ""
        $targetSource = $null

        Say "restarting push relay uid=$pushUid"
        $start = Invoke-RestMethod -Method Post -Headers $PrimaryHeaders -Uri "$PrimaryBase/api/relays/push/start/$pushUid" -TimeoutSec 10
        if ($start.ok -ne $true) {
            Fail "start push relay did not return ok"
        }
        $startedRecord = Get-PushRelayRecordFromHttp $PrimaryBase $PrimaryHeaders $pushUid
        if ($startedRecord.enabled -ne $true) {
            Fail "start push relay did not set enabled=true"
        }
        $restartedStatus = Wait-PushRelayRunning $PrimaryBase $PrimaryHeaders $pushUid $primaryStatus.node_id $StartupTimeoutSec "relay push restart"
        $targetSource = Wait-PushedSourceActive $seed $seed.TargetMount @($seed.SourceConnectKey, $restartedStatus.connect_key) $StartupTimeoutSec "relay push restart target source"
        if ($targetSource.ConnectKey -eq $seed.SourceConnectKey -or $targetSource.ConnectKey -eq $restartedStatus.connect_key) {
            Fail "relay push restarted target source connect_key must be distinct from source/relay keys: target=$($targetSource.ConnectKey) source=$($seed.SourceConnectKey) relay=$($restartedStatus.connect_key)"
        }
        $seed.TargetSourceConnectKey = $targetSource.ConnectKey
        Wait-ClusterPushCountAtLeast $PrimaryBase $PrimaryHeaders $primaryStatus.node_id ($baselinePush + 1) $StartupTimeoutSec "relay push restart" | Out-Null

        Remove-PushRelayRecord $PrimaryBase $PrimaryHeaders $pushUid
        Wait-PushRelayNotRunning $PrimaryBase $PrimaryHeaders $pushUid $StartupTimeoutSec "relay push cleanup"
        Wait-PushedSourceGone $seed $seed.TargetMount $targetSource.ConnectKey $StartupTimeoutSec "relay push cleanup target source"
        Wait-ClusterPushCountAtMost $PrimaryBase $PrimaryHeaders $primaryStatus.node_id $baselinePush $StartupTimeoutSec "relay push cleanup" | Out-Null
        $pushUid = $null
        $seed.TargetSourceConnectKey = ""
        $targetSource = $null

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

        Say "PASS relay push start/stop smoke"
    }
    finally {
        if ($pushUid) {
            Remove-PushRelayRecord $PrimaryBase $PrimaryHeaders $pushUid
        }
        Close-NtripTcpConnection $forwardClientConnection
        Close-NtripTcpConnection $sourceConnection
        if ($script:ntripSourceConnection -eq $sourceConnection) {
            $script:ntripSourceConnection = $null
        }
        if ($script:ntripAuthSeed -eq $seed) {
            $script:ntripAuthSeed = $null
        }
        if ($seed) {
            Remove-NtripAuthSessionSeed $seed
        }
        Stop-E2eCasterServiceNode $secondaryNode
        if ($secondaryConfRoot -and (Test-Path -LiteralPath $secondaryConfRoot)) {
            try {
                Remove-Item -LiteralPath $secondaryConfRoot -Recurse -Force
            }
            catch {
                Write-Warning "failed to remove relay push service config: $($_.Exception.Message)"
            }
        }
    }
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

$requiredPaths = @($compatScript)
if (-not ($IncludeDockerBridgeCluster -or $IncludeHttpIngressStrategy)) {
    $requiredPaths += @($serviceExe, $serviceConfig, $coreConfig, $authConfig)
}
foreach ($path in $requiredPaths) {
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
$ntripNeedsNamedRover = $IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAuthBroadcast -or $IncludeNtripDisabledAccount -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeAccessRuntimeEnforcement -or $IncludeDataPushRelayPushE2E
$ntripNeedsAuthFixture = $ntripNeedsNamedRover -or $IncludeNtripAnonymousAuth
$stdout = Join-Path $env:TEMP ("navcaster-e2e-" + [guid]::NewGuid().ToString() + ".out.log")
$stderr = Join-Path $env:TEMP ("navcaster-e2e-" + [guid]::NewGuid().ToString() + ".err.log")
$scriptFailed = $false

try {
    Say "root=$RootPath configuration=$Configuration redis_mode=$RedisMode"

    if ($IncludeDockerBridgeCluster) {
        $otherIncludes = $IncludeActiveAccounts -or $IncludeActiveAccountSseDelta -or $IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAnonymousAuth -or $IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeHttpIngressStrategy -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeNtripDisabledAccount -or $IncludeRedisReconnect -or $IncludeOperationsApi -or $IncludeSelfServiceApi -or $IncludeAccessRuntimeEnforcement -or $IncludeWebThreeRoleBrowserSmoke -or $IncludeDataPushRelayPushE2E
        if ($otherIncludes) {
            Fail "Docker bridge cluster smoke must run in a separate lifecycle because it creates its own Docker network, Redis, and NavCaster containers."
        }
        if ($RedisMode -ne "Docker") {
            Fail "Docker bridge cluster smoke requires RedisMode Docker because it owns the Redis fixture container inside the bridge network."
        }
        if ($HttpPort -eq $NtripBroadcastHttpPort) {
            Fail "Docker bridge node B HTTP host port must differ from node A HTTP host port."
        }
        if ($NtripPort -eq $NtripBroadcastNtripPort) {
            Fail "Docker bridge node B NTRIP host port must differ from node A NTRIP host port."
        }
        Assert-NavCasterRuntimeImageSpecified "Docker bridge cluster smoke"

        $dockerCmd = Get-Command docker -ErrorAction SilentlyContinue
        if (-not $dockerCmd) {
            Fail "missing docker. Docker bridge cluster smoke requires Docker Engine and local runtime images."
        }
        $dockerInfo = Invoke-NativeCommand "docker" @("info")
        if ($dockerInfo.ExitCode -ne 0) {
            Fail ("docker engine is not available: " + (($dockerInfo.Output | Select-Object -First 6) -join " "))
        }

        Invoke-DockerBridgeClusterSmoke
        Say "PASS health/login/status/cluster smoke"
        return
    }

    if ($IncludeHttpIngressStrategy) {
        $otherIncludes = $IncludeActiveAccounts -or $IncludeActiveAccountSseDelta -or $IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAnonymousAuth -or $IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeDockerBridgeCluster -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeNtripDisabledAccount -or $IncludeRedisReconnect -or $IncludeOperationsApi -or $IncludeSelfServiceApi -or $IncludeAccessRuntimeEnforcement -or $IncludeWebThreeRoleBrowserSmoke -or $IncludeDataPushRelayPushE2E
        if ($otherIncludes) {
            Fail "HTTP ingress strategy smoke must run in a separate lifecycle because it creates its own Docker network, Redis, NavCaster containers, and nginx proxy."
        }
        if ($RedisMode -ne "Docker") {
            Fail "HTTP ingress strategy smoke requires RedisMode Docker because it owns the Redis fixture container inside the bridge network."
        }
        if ($HttpPort -eq $NtripBroadcastHttpPort) {
            Fail "HTTP ingress node B HTTP host port must differ from node A HTTP host port."
        }
        if ($NtripPort -eq $NtripBroadcastNtripPort) {
            Fail "HTTP ingress node B NTRIP host port must differ from node A NTRIP host port."
        }
        $httpPorts = @($HttpPort, $NtripBroadcastHttpPort, $HttpIngressStickyPort, $HttpIngressRoundRobinPort)
        if (($httpPorts | Sort-Object -Unique).Count -ne $httpPorts.Count) {
            Fail "HTTP ingress direct and proxy host ports must be unique: [$($httpPorts -join ',')]"
        }
        Assert-NavCasterRuntimeImageSpecified "HTTP ingress strategy smoke"

        $dockerCmd = Get-Command docker -ErrorAction SilentlyContinue
        if (-not $dockerCmd) {
            Fail "missing docker. HTTP ingress strategy smoke requires Docker Engine and local runtime images."
        }
        $dockerInfo = Invoke-NativeCommand "docker" @("info")
        if ($dockerInfo.ExitCode -ne 0) {
            Fail ("docker engine is not available: " + (($dockerInfo.Output | Select-Object -First 6) -join " "))
        }

        Invoke-HttpIngressStrategySmoke
        Say "PASS health/login/status/cluster smoke"
        return
    }

    if ($IncludeNtripAnonymousAuth -and $ntripNeedsNamedRover) {
        Fail "NTRIP anonymous auth smoke must run in a separate service lifecycle from named-rover NTRIP smokes because Rover_Setting.Anonymous_Login is scenario-specific."
    }
    if ($IncludeNtripAuthBroadcast -and ($IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection)) {
        Fail "NTRIP Auth Broadcast smoke must run in a separate service lifecycle because it starts a second local CasterService instance and requires Online_Protection=false."
    }
    if ($IncludeLocalDualNodeIdentity -and $IncludeNtripAuthBroadcast) {
        Fail "Local dual-node identity smoke must run separately from NTRIP Auth Broadcast because both scenarios start a second local CasterService instance."
    }
    if ($IncludeMasterLeaseFailover -and ($IncludeActiveAccounts -or $IncludeActiveAccountSseDelta -or $IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAnonymousAuth -or $IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeNtripDisabledAccount -or $IncludeRedisReconnect)) {
        Fail "Master lease failover smoke must run in a separate service lifecycle because it stops the current master CasterService instance."
    }
    if ($IncludeMasterLeaseStability -and ($IncludeActiveAccounts -or $IncludeActiveAccountSseDelta -or $IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAnonymousAuth -or $IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeNtripDisabledAccount -or $IncludeRedisReconnect)) {
        Fail "Master lease stability smoke must run in a separate service lifecycle because it repeatedly stops and restarts local CasterService instances."
    }
    if ($IncludeRelayPullStartStop -and ($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover)) {
        Fail "Relay pull start/stop smoke must run separately from other local dual-instance smokes."
    }
    if ($IncludeRelayPushStartStop -and ($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover)) {
        Fail "Relay push start/stop smoke must run separately from other local dual-instance smokes."
    }
    if ($IncludeRelayDataForwarding -and ($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayFailover -or $IncludeRelayPushFailover)) {
        Fail "Relay data forwarding smoke must run separately from other local dual-instance smokes."
    }
    if ($IncludeRelayFailover -and ($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayPushFailover)) {
        Fail "Relay failover smoke must run separately from other local dual-instance smokes because it stops the current relay executor/master node."
    }
    if ($IncludeRelayPushFailover -and ($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover)) {
        Fail "Relay push failover smoke must run separately from other local dual-instance smokes because it stops the current relay executor/master node."
    }
    if ($IncludeDataPushRelayPushE2E -and ($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeNtripDisabledAccount -or $IncludeAccessRuntimeEnforcement -or $IncludeWebThreeRoleBrowserSmoke)) {
        Fail "DataPush relay_push e2e must run in a separate local dual-instance lifecycle."
    }
    if ($IncludeNtripDisabledAccount -and ($IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAuthBroadcast -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover)) {
        Fail "NTRIP disabled account smoke must run in a separate service lifecycle because it mutates account login state through HTTP APIs."
    }
    if ($IncludeAccessRuntimeEnforcement -and ($IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAnonymousAuth -or $IncludeNtripAuthBroadcast -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeNtripDisabledAccount)) {
        Fail "Access runtime enforcement smoke must run in a separate service lifecycle because it creates real role/access accounts and mutates runtime balances."
    }
    if ($IncludeWebThreeRoleBrowserSmoke -and ($IncludeNtripAuthSession -or $IncludeNtripAuthSessionRenewal -or $IncludeNtripOnlineProtection -or $IncludeNtripAnonymousAuth -or $IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeNtripDisabledAccount -or $IncludeRedisReconnect -or $IncludeAccessRuntimeEnforcement)) {
        Fail "Web three-role browser smoke must run in an HTTP-only service lifecycle; combine only with OperationsApi/SelfServiceApi/basic smoke checks."
    }
    if (($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeDataPushRelayPushE2E) -and $HttpPort -eq $NtripBroadcastHttpPort) {
        Fail "secondary HTTP port must differ from primary HTTP port."
    }
    if (($IncludeNtripAuthBroadcast -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeDataPushRelayPushE2E) -and $NtripPort -eq $NtripBroadcastNtripPort) {
        Fail "secondary NTRIP port must differ from primary NTRIP port."
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
    if ($ntripNeedsAuthFixture -or $IncludeLocalDualNodeIdentity -or $IncludeMasterLeaseFailover -or $IncludeMasterLeaseStability -or $IncludeRelayPullStartStop -or $IncludeRelayPushStartStop -or $IncludeRelayDataForwarding -or $IncludeRelayFailover -or $IncludeRelayPushFailover -or $IncludeDataPushRelayPushE2E) {
        $coreText = Set-YamlValueInSection $coreText "Caster_Setting" "Update_Intv" "1"
        $coreText = Set-YamlValueInSection $coreText "Caster_Setting" "Key_Expire_Time" "10"
    }
    if ($ntripNeedsAuthFixture) {
        $coreText = Set-YamlValueInSection $coreText "Rover_Setting" "Enable_Mult" "true"
        $coreText = Set-YamlValueInSection $coreText "Rover_Setting" "Keep_Early" "false"
    }
    Write-TextFile $coreConfig $coreText

    $authText = $originals[$authConfig]
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "IP" $RedisHost
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Port" ([string]$RedisPort)
    $authText = Set-YamlValueInSection $authText "Reids_Connect_Setting" "Requirepass" $RedisPassword
    if ($IncludeAccessRuntimeEnforcement) {
        $authText = Set-YamlValueInSection $authText "Base_Setting" "Anonymous_Login" "false"
        $authText = Set-YamlValueInSection $authText "Rover_Setting" "Anonymous_Login" "false"
        $authText = Set-YamlValueInSection $authText "Rover_Setting" "Online_Protection" "true"
        $authText = Set-YamlValueInSection $authText "Source_Setting" "Anonymous_Login" "false"
    }
    elseif ($ntripNeedsAuthFixture) {
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

    $session = Invoke-E2eLogin $base
    $headers = $session.Headers
    Assert-E2eStatusAndCluster $base $headers "initial smoke" -RequireRedisConnected | Out-Null

    if ($IncludeActiveAccounts) {
        Invoke-ActiveAccountSmoke $base $headers $session.Token
    }

    if ($IncludeActiveAccountSseDelta) {
        Invoke-ActiveAccountSseDeltaSmoke $base $headers $session.Token
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

    if ($IncludeLocalDualNodeIdentity) {
        Invoke-LocalDualNodeIdentitySmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeMasterLeaseFailover) {
        Invoke-MasterLeaseFailoverSmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeMasterLeaseStability) {
        Invoke-MasterLeaseStabilitySmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeRelayPullStartStop) {
        Invoke-RelayPullStartStopSmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeRelayPushStartStop) {
        Invoke-RelayPushStartStopSmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeRelayDataForwarding) {
        Invoke-RelayPullStartStopSmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort -AssertDataForwarding
        Invoke-RelayPushStartStopSmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort -AssertDataForwarding
    }

    if ($IncludeRelayFailover) {
        Invoke-RelayFailoverSmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeRelayPushFailover) {
        Invoke-RelayPushFailoverSmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeDataPushRelayPushE2E) {
        Invoke-DataPushRelayPushE2ESmoke $base $headers $NtripPort $NtripBroadcastHttpPort $NtripBroadcastNtripPort
    }

    if ($IncludeNtripDisabledAccount) {
        Invoke-NtripDisabledAccountSmoke $base $headers $HttpBindAddr $NtripPort
    }

    if ($IncludeRedisReconnect) {
        Invoke-RedisReconnectSmoke $base $headers
    }

    if ($IncludeOperationsApi) {
        Invoke-OperationsApiSmoke $base $headers
    }

    if ($IncludeSelfServiceApi) {
        Invoke-SelfServiceApiSmoke $base $headers
    }

    if ($IncludeWebThreeRoleBrowserSmoke) {
        Invoke-WebThreeRoleBrowserSmoke $base $headers
    }

    if ($IncludeAccessRuntimeEnforcement) {
        Invoke-AccessRuntimeEnforcementSmoke $base $headers $HttpBindAddr $NtripPort
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
            if (-not $serviceProcess.WaitForExit(5000)) {
                Write-Warning "CasterService did not exit within 5 seconds"
            }
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
