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
$SchemaSmoke = Join-Path $RootDir "bin\$BuildType\schema_smoke.exe"

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

Push-Location $RootDir
try {
    Invoke-AdmissionStep `
        -Name 'API contract check' `
        -Gate 'BLOCKING' `
        -Command 'node tools\contract_check\check_api_contracts.mjs' `
        -Script { Invoke-Native 'node' @('tools\contract_check\check_api_contracts.mjs') }

    Invoke-AdmissionStep `
        -Name 'Ninja configure' `
        -Gate 'BLOCKING' `
        -Command ".\deploy\scripts\build_ninja.ps1 -BuildType $BuildType -ConfigureOnly" `
        -Script { & (Join-Path $RootDir 'deploy\scripts\build_ninja.ps1') -BuildType $BuildType -Jobs $Jobs -ConfigureOnly }

    Invoke-AdmissionStep `
        -Name 'schema_smoke Ninja build' `
        -Gate 'BLOCKING' `
        -Command ".\deploy\scripts\build_ninja.ps1 -BuildType $BuildType -Target schema_smoke" `
        -Script { & (Join-Path $RootDir 'deploy\scripts\build_ninja.ps1') -BuildType $BuildType -Target schema_smoke -Jobs $Jobs }

    Invoke-AdmissionStep `
        -Name 'schema_smoke executable' `
        -Gate 'BLOCKING' `
        -Command ".\bin\$BuildType\schema_smoke.exe" `
        -Script {
            if (-not (Test-Path -LiteralPath $SchemaSmoke)) {
                throw "missing schema_smoke executable: $SchemaSmoke"
            }
            Invoke-Native $SchemaSmoke @()
        }

    Invoke-AdmissionStep `
        -Name 'CTest schema_smoke' `
        -Gate 'BLOCKING' `
        -Command "ctest --test-dir build\ninja-$BuildType --output-on-failure -R schema_smoke" `
        -Script { Invoke-Native 'ctest' @('--test-dir', $BuildDir, '--output-on-failure', '-R', 'schema_smoke') }

    if (-not $SkipNpmCi) {
        Invoke-AdmissionStep `
            -Name 'Web npm ci' `
            -Gate 'BLOCKING' `
            -Command 'npm --prefix web ci' `
            -Script { Invoke-Native 'npm' @('--prefix', 'web', 'ci') }
    } else {
        Add-Result -Name 'Web npm ci' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command 'npm --prefix web ci' -Notes 'Skipped by -SkipNpmCi'
    }

    Invoke-AdmissionStep `
        -Name 'Web production build' `
        -Gate 'BLOCKING' `
        -Command 'npm --prefix web run build' `
        -Script { Invoke-Native 'npm' @('--prefix', 'web', 'run', 'build') }

    if ($IncludeLint) {
        Invoke-AdmissionStep `
            -Name 'Web lint' `
            -Gate 'REPORT' `
            -Command 'npm --prefix web run lint' `
            -Script { Invoke-Native 'npm' @('--prefix', 'web', 'run', 'lint') } `
            -Notes 'Report item until the existing frontend lint baseline is fixed.'
    } else {
        Add-Result -Name 'Web lint' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command 'npm --prefix web run lint' -Notes 'Report item; run with -IncludeLint.'
    }

    if ($IncludeE2eReport) {
        Add-Result -Name 'E2E matrix' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command '.\deploy\scripts\e2e_smoke.ps1 <task-specific flags>' -Notes 'Task-triggered matrix: Docker/Redis/service lifecycle dependent; run explicitly per task risk.'
    } else {
        Add-Result -Name 'E2E matrix' -Gate 'REPORT' -Status 'SKIP' -ExitCode 0 -Command '.\deploy\scripts\e2e_smoke.ps1 <task-specific flags>' -Notes 'Run with -IncludeE2eReport to print the matrix reminder.'
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
