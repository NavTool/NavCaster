param(
    [ValidateSet('Release', 'Debug')]
    [string]$BuildType = 'Release',
    [int]$Jobs = 0,
    [switch]$SkipNpmCi,
    [switch]$IncludeLint,
    [switch]$IncludeE2eReport
)

$ErrorActionPreference = 'Stop'

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildDir = Join-Path $RootDir "build\ninja-$BuildType"
$AdmissionBinDir = Join-Path $RootDir "build\admission-$BuildType"
$AdminDir = Join-Path $RootDir 'app\admin'
$AgentDir = Join-Path $RootDir 'app\agent'
$CasterExe = Join-Path $RootDir "bin\$BuildType\navcaster-caster.exe"

$results = New-Object System.Collections.Generic.List[object]

function Add-Result {
    param(
        [string]$Name,
        [ValidateSet('BLOCKING', 'REPORT')]
        [string]$Gate,
        [ValidateSet('PASS', 'FAIL', 'SKIP')]
        [string]$Status,
        [int]$ExitCode,
        [string]$Command,
        [string]$Notes = ''
    )

    $results.Add([pscustomobject]@{
        Name = $Name
        Gate = $Gate
        Status = $Status
        ExitCode = $ExitCode
        Command = $Command
        Notes = $Notes
    }) | Out-Null
}

function Invoke-AdmissionStep {
    param(
        [string]$Name,
        [ValidateSet('BLOCKING', 'REPORT')]
        [string]$Gate,
        [string]$Command,
        [scriptblock]$Script,
        [string]$Notes = ''
    )

    Write-Host "[admission] $Gate $Name"
    try {
        & $Script
        Add-Result -Name $Name -Gate $Gate -Status 'PASS' -ExitCode 0 -Command $Command -Notes $Notes
    } catch {
        $message = $_.Exception.Message
        Add-Result -Name $Name -Gate $Gate -Status 'FAIL' -ExitCode 1 -Command $Command -Notes $message
        if ($Gate -eq 'BLOCKING') {
            throw
        }
        Write-Warning "[admission] report item failed: ${Name}: $message"
    }
}

function Invoke-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = $RootDir
    )

    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
        }
    } finally {
        Pop-Location
    }
}

function Assert-AppLayout {
    $required = @(
        'app\admin',
        'app\agent',
        'app\caster',
        'app\web',
        '.archive\v1\src',
        '.archive\v1\web'
    )
    foreach ($relative in $required) {
        $path = Join-Path $RootDir $relative
        if (-not (Test-Path -LiteralPath $path)) {
            throw "missing required app-layout path: $relative"
        }
    }

    $forbidden = @('admin', 'agent', 'caster', 'web', 'src')
    foreach ($relative in $forbidden) {
        $path = Join-Path $RootDir $relative
        if (Test-Path -LiteralPath $path) {
            throw "root production path must not exist in v2 app layout: $relative"
        }
    }

    $legacyWebDefaults = @(
        'app\web\src\pages',
        'app\web\src\layouts',
        'app\web\src\v2\api\mockData.ts'
    )
    foreach ($relative in $legacyWebDefaults) {
        $path = Join-Path $RootDir $relative
        if (Test-Path -LiteralPath $path) {
            throw "app/web must not include legacy or mock-only default path: $relative"
        }
    }
}

Push-Location $RootDir
try {
    Invoke-AdmissionStep `
        -Name 'v2 app layout guard' `
        -Gate 'BLOCKING' `
        -Command 'Assert-AppLayout' `
        -Script { Assert-AppLayout }

    Invoke-AdmissionStep `
        -Name 'API contract check' `
        -Gate 'BLOCKING' `
        -Command 'node tools\contract_check\check_api_contracts.mjs' `
        -Script { Invoke-Native 'node' @('tools\contract_check\check_api_contracts.mjs') }

    Invoke-AdmissionStep `
        -Name 'AdminService Go tests' `
        -Gate 'BLOCKING' `
        -Command 'cd app\admin; go test ./...' `
        -Script { Invoke-Native 'go' @('test', './...') -WorkingDirectory $AdminDir }

    Invoke-AdmissionStep `
        -Name 'AdminService build' `
        -Gate 'BLOCKING' `
        -Command 'cd app\admin; go build -o ..\..\build\admission-<BuildType>\navcaster-admin.exe .\cmd\navcaster-admin' `
        -Script {
            New-Item -Path $AdmissionBinDir -ItemType Directory -Force | Out-Null
            Invoke-Native 'go' @('build', '-o', (Join-Path $AdmissionBinDir 'navcaster-admin.exe'), '.\cmd\navcaster-admin') -WorkingDirectory $AdminDir
        }

    Invoke-AdmissionStep `
        -Name 'Agent Go tests' `
        -Gate 'BLOCKING' `
        -Command 'cd app\agent; go test ./...' `
        -Script { Invoke-Native 'go' @('test', './...') -WorkingDirectory $AgentDir }

    Invoke-AdmissionStep `
        -Name 'Agent build' `
        -Gate 'BLOCKING' `
        -Command 'cd app\agent; go build -o ..\..\build\admission-<BuildType>\navcaster-agent.exe .\cmd\navcaster-agent' `
        -Script {
            New-Item -Path $AdmissionBinDir -ItemType Directory -Force | Out-Null
            Invoke-Native 'go' @('build', '-o', (Join-Path $AdmissionBinDir 'navcaster-agent.exe'), '.\cmd\navcaster-agent') -WorkingDirectory $AgentDir
        }

    Invoke-AdmissionStep `
        -Name 'Ninja configure' `
        -Gate 'BLOCKING' `
        -Command ".\deploy\scripts\build_ninja.ps1 -BuildType $BuildType -ConfigureOnly" `
        -Script { & (Join-Path $RootDir 'deploy\scripts\build_ninja.ps1') -BuildType $BuildType -Jobs $Jobs -ConfigureOnly }

    Invoke-AdmissionStep `
        -Name 'navcaster-caster Ninja build' `
        -Gate 'BLOCKING' `
        -Command ".\deploy\scripts\build_ninja.ps1 -BuildType $BuildType -Target navcaster-caster" `
        -Script { & (Join-Path $RootDir 'deploy\scripts\build_ninja.ps1') -BuildType $BuildType -Target navcaster-caster -Jobs $Jobs }

    Invoke-AdmissionStep `
        -Name 'navcaster-caster self-test' `
        -Gate 'BLOCKING' `
        -Command ".\bin\$BuildType\navcaster-caster.exe --self-test --worker-count 2 --self-test-duration-ms 250" `
        -Script {
            if (-not (Test-Path -LiteralPath $CasterExe)) {
                throw "missing navcaster-caster executable: $CasterExe"
            }
            Invoke-Native $CasterExe @('--self-test', '--worker-count', '2', '--self-test-duration-ms', '250')
        }

    if (-not $SkipNpmCi) {
        Invoke-AdmissionStep `
            -Name 'Web npm ci' `
            -Gate 'BLOCKING' `
            -Command 'npm --prefix app/web ci' `
            -Script { Invoke-Native 'npm' @('--prefix', 'app/web', 'ci') }
    } else {
        Add-Result -Name 'Web npm ci' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command 'npm --prefix app/web ci' -Notes 'Skipped by -SkipNpmCi'
    }

    Invoke-AdmissionStep `
        -Name 'Web production build' `
        -Gate 'BLOCKING' `
        -Command 'npm --prefix app/web run build' `
        -Script { Invoke-Native 'npm' @('--prefix', 'app/web', 'run', 'build') }

    if ($IncludeLint) {
        Invoke-AdmissionStep `
            -Name 'Web lint' `
            -Gate 'REPORT' `
            -Command 'npm --prefix app/web run lint' `
            -Script { Invoke-Native 'npm' @('--prefix', 'app/web', 'run', 'lint') } `
            -Notes 'Report item until the existing frontend lint baseline is fixed.'
    } else {
        Add-Result -Name 'Web lint' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command 'npm --prefix app/web run lint' -Notes 'Report item; run with -IncludeLint.'
    }

    if ($IncludeE2eReport) {
        Add-Result -Name 'E2E matrix' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command '.\deploy\scripts\v2_admin_control_plane_smoke.ps1 / v2_caster_* smoke scripts' -Notes 'Task-triggered matrix: Docker/PostgreSQL/Redis lifecycle dependent; run explicitly per task risk.'
    } else {
        Add-Result -Name 'E2E matrix' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command '.\deploy\scripts\v2_admin_control_plane_smoke.ps1 / v2_caster_* smoke scripts' -Notes 'Run with -IncludeE2eReport to print the v2 smoke matrix reminder.'
    }
} finally {
    Pop-Location
}

Write-Host ''
Write-Host '[admission] summary'
$results | Format-Table -AutoSize

$blockingFailures = @($results | Where-Object { $_.Gate -eq 'BLOCKING' -and $_.Status -eq 'FAIL' })
if ($blockingFailures.Count -gt 0) {
    Write-Error "[admission] blocking checks failed: $($blockingFailures.Name -join ', ')"
    exit 1
}

Write-Host '[admission] PASS blocking checks'
