param(
	[ValidateSet('Release', 'Debug')]
	[string]$BuildType = $(if ($env:BUILD_TYPE) { $env:BUILD_TYPE } else { 'Release' }),
	[string]$DistRoot = $(if ($env:DIST_ROOT) { $env:DIST_ROOT } else { '' }),
	[string]$PackageVersion = $(if ($env:PACKAGE_VERSION) { $env:PACKAGE_VERSION } else { '' }),
	[string]$PackagePlatform = $(if ($env:PACKAGE_PLATFORM) { $env:PACKAGE_PLATFORM } else { '' }),
	[switch]$SkipNpmCi,
	[switch]$SkipWebBuild,
	[switch]$SkipContractCheck,
	[switch]$SkipCtest,
	[switch]$NoArchive,
	[switch]$NoInstall
)

$ErrorActionPreference = 'Stop'

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $DistRoot) {
	$DistRoot = Join-Path $RootDir 'dist'
}

function Invoke-Native {
	param(
		[string]$FilePath,
		[string[]]$Arguments = @()
	)

	& $FilePath @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
	}
}

function Test-CommandAvailable {
	param([string]$Name)
	return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-WingetInstall {
	param(
		[string]$Id,
		[string]$Name,
		[string[]]$OverrideArgs = @()
	)

	if ($NoInstall) {
		throw "$Name is required but -NoInstall was set."
	}
	if (-not (Test-CommandAvailable 'winget')) {
		throw "$Name is required and winget is not available. Install it manually or rerun without -NoInstall on a machine with winget."
	}

	Write-Host "[package] installing $Name with winget package $Id"
	$args = @(
		'install',
		'--id', $Id,
		'--exact',
		'--silent',
		'--accept-package-agreements',
		'--accept-source-agreements'
	)
	if ($OverrideArgs.Count -gt 0) {
		$args += @('--override', ($OverrideArgs -join ' '))
	}
	Invoke-Native winget $args
	$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
	$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
	$env:Path = "$machinePath;$userPath;$env:Path"
}

function Ensure-Command {
	param(
		[string]$Command,
		[string]$WingetId,
		[string]$Name
	)

	if (Test-CommandAvailable $Command) {
		return
	}
	Invoke-WingetInstall -Id $WingetId -Name $Name
	if (-not (Test-CommandAvailable $Command)) {
		throw "failed to provide command: $Command"
	}
}

function Test-NinjaExecutable {
	param([string]$Path)
	if (-not $Path -or -not (Test-Path $Path)) {
		return $false
	}

	try {
		& $Path --version | Out-Null
		return $true
	} catch {
		return $false
	}
}

function Resolve-NinjaExecutable {
	if ($env:NINJA_EXE -and (Test-NinjaExecutable $env:NINJA_EXE)) {
		return (Resolve-Path $env:NINJA_EXE).Path
	}

	foreach ($command in (Get-Command ninja -All -ErrorAction SilentlyContinue)) {
		if ($command.Path -and (Test-NinjaExecutable $command.Path)) {
			return $command.Path
		}
	}

	$vsRoots = @(
		(Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
		(Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio')
	) | Where-Object { $_ -and (Test-Path $_) }

	foreach ($root in $vsRoots) {
		$candidate = Get-ChildItem -Path $root -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
			Where-Object { $_.FullName -like '*CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' } |
			Select-Object -First 1

		if ($candidate -and (Test-NinjaExecutable $candidate.FullName)) {
			return $candidate.FullName
		}
	}

	return $null
}

function Test-PathListCommand {
	param(
		[string]$PathList,
		[string]$CommandName
	)

	foreach ($entry in ($PathList -split ';')) {
		if (-not $entry) {
			continue
		}

		$candidate = Join-Path $entry $CommandName
		if (Test-Path $candidate) {
			return $true
		}
	}

	return $false
}

function Import-MsvcDeveloperEnvironment {
	if (Get-Command cl -ErrorAction SilentlyContinue) {
		return $true
	}

	$vsRoots = @(
		(Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
		(Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio')
	) | Where-Object { $_ -and (Test-Path $_) }

	$vsDevCmds = foreach ($root in $vsRoots) {
		Get-ChildItem -Path $root -Recurse -Filter VsDevCmd.bat -ErrorAction SilentlyContinue
	}

	foreach ($vsDevCmd in ($vsDevCmds | Sort-Object FullName -Descending)) {
		if (-not $vsDevCmd) {
			continue
		}

		Write-Host "[package] loading MSVC environment: $($vsDevCmd.FullName)"
		$envDump = cmd.exe /s /c "call `"$($vsDevCmd.FullName)`" -arch=x64 -host_arch=x64 >nul && set"
		if ($LASTEXITCODE -ne 0) {
			continue
		}

		$envVars = @{}
		$pathValue = $null
		foreach ($line in $envDump) {
			$index = $line.IndexOf('=')
			if ($index -gt 0) {
				$name = $line.Substring(0, $index)
				$value = $line.Substring($index + 1)
				$key = $name.ToUpperInvariant()
				if ($key -eq 'PATH') {
					if (Test-PathListCommand $value 'cl.exe') {
						$pathValue = $value
					}
				} else {
					$envVars[$name] = $value
				}
			}
		}

		foreach ($entry in $envVars.GetEnumerator()) {
			[Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
		}
		if ($pathValue) {
			[Environment]::SetEnvironmentVariable('Path', $pathValue, 'Process')
			$env:Path = $pathValue
		}

		if (Get-Command cl -ErrorAction SilentlyContinue) {
			return $true
		}
	}

	return $false
}

function Ensure-Msvc {
	if (Import-MsvcDeveloperEnvironment) {
		return
	}

	Invoke-WingetInstall `
		-Id 'Microsoft.VisualStudio.2022.BuildTools' `
		-Name 'Visual Studio 2022 Build Tools with C++ workload' `
		-OverrideArgs @('--wait', '--quiet', '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended')
	if (-not (Import-MsvcDeveloperEnvironment)) {
		throw 'MSVC compiler cl.exe not found after installing Visual Studio Build Tools. Make sure the C++ workload is installed.'
	}
}

function Test-ThirdPartyReady {
	$required = @(
		'third_party\abseil-cpp\CMakeLists.txt',
		'third_party\protobuf\CMakeLists.txt',
		'third_party\libevent\CMakeLists.txt',
		'third_party\hiredis\CMakeLists.txt',
		'third_party\yaml-cpp\CMakeLists.txt',
		'third_party\rtklib\CMakeLists.txt',
		'third_party\json\include\nlohmann\json.hpp',
		'third_party\spdlog\include\spdlog\spdlog.h'
	)
	foreach ($item in $required) {
		if (-not (Test-Path (Join-Path $RootDir $item))) {
			return $false
		}
	}
	return $true
}

function Ensure-ThirdParty {
	if (Test-ThirdPartyReady) {
		return
	}

	Write-Host '[package] third_party dependencies are incomplete; hydrating...'
	$teamHydrate = 'F:\Projects\NavCaster\_team\scripts\HYDRATE_WORKTREE_SUBMODULES.ps1'
	$sourceRepo = 'F:\Projects\NavCaster\repo'
	if (Test-Path $teamHydrate -PathType Leaf) {
		if ($NoInstall) {
			throw 'third_party dependencies are incomplete and -NoInstall was set.'
		}
		powershell -ExecutionPolicy Bypass -File $teamHydrate -WorktreePath $RootDir -SourceRepo $sourceRepo
		if ($LASTEXITCODE -ne 0) {
			throw "third_party hydration failed with exit code $LASTEXITCODE"
		}
	} else {
		Invoke-Native git @('-C', $RootDir, 'submodule', 'update', '--init', '--recursive')
	}

	if (-not (Test-ThirdPartyReady)) {
		throw 'third_party dependencies are still incomplete after hydration.'
	}
}

function Ensure-Environment {
	Write-Host '[package] checking build environment'
	Ensure-Command -Command 'git' -WingetId 'Git.Git' -Name 'Git'
	Ensure-Command -Command 'node' -WingetId 'OpenJS.NodeJS.LTS' -Name 'Node.js LTS'
	Ensure-Command -Command 'npm' -WingetId 'OpenJS.NodeJS.LTS' -Name 'npm'
	Ensure-Command -Command 'cmake' -WingetId 'Kitware.CMake' -Name 'CMake'

	$ninja = Resolve-NinjaExecutable
	if (-not $ninja) {
		Invoke-WingetInstall -Id 'Ninja-build.Ninja' -Name 'Ninja'
		$ninja = Resolve-NinjaExecutable
	}
	if (-not $ninja) {
		throw 'Ninja executable not found after installation.'
	}
	$env:NINJA_EXE = $ninja

	Ensure-Msvc

	Write-Host "[package] git   : $(& git --version)"
	Write-Host "[package] node  : $(& node --version)"
	Write-Host "[package] npm   : $(& npm --version)"
	Write-Host "[package] cmake : $((& cmake --version | Select-Object -First 1))"
	Write-Host "[package] ninja : $(& $env:NINJA_EXE --version)"
	Write-Host "[package] cl    : $((Get-Command cl).Source)"
	Ensure-ThirdParty
}

function Get-SanitizedTagPart {
	param([string]$Value)
	$normalized = $Value -replace '[/ :@]', '-'
	$normalized = $normalized -replace '[^A-Za-z0-9_.-]', ''
	return $normalized
}

function Get-ProjectDefaultVersion {
	$cmakeFile = Join-Path $RootDir 'CMakeLists.txt'
	$parts = @{
		MAJOR = '0'
		MINOR = '0'
		PATCH = '0'
		EXTRA = '0'
	}

	if (Test-Path $cmakeFile -PathType Leaf) {
		foreach ($line in (Get-Content -LiteralPath $cmakeFile)) {
			if ($line -match '^\s*set\(VERSION_(MAJOR|MINOR|PATCH|EXTRA)\s+([0-9]+)\s*\)') {
				$parts[$matches[1]] = $matches[2]
			}
		}
	}

	return "$($parts['MAJOR']).$($parts['MINOR']).$($parts['PATCH']).$($parts['EXTRA'])"
}

function Get-DefaultPackagePlatform {
	try {
		$arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
	} catch {
		$arch = $env:PROCESSOR_ARCHITECTURE.ToLowerInvariant()
	}

	switch ($arch) {
		'x64' { $archPart = 'amd64' }
		'amd64' { $archPart = 'amd64' }
		'arm64' { $archPart = 'arm64' }
		default { $archPart = Get-SanitizedTagPart $arch }
	}

	return "windows-$archPart"
}

function Get-DefaultPackageVersion {
	$latestTagOutput = (& git -C $RootDir describe --tags --abbrev=0 2>$null)
	if ($LASTEXITCODE -eq 0 -and $latestTagOutput) {
		$latestTag = (($latestTagOutput | Select-Object -First 1).ToString()).Trim()
		if ($latestTag) {
			$commitCountOutput = (& git -C $RootDir rev-list "$latestTag..HEAD" --count 2>$null)
			if ($LASTEXITCODE -eq 0 -and $commitCountOutput) {
				$commitCountText = (($commitCountOutput | Select-Object -First 1).ToString()).Trim()
				[int]$commitCount = 0
				if ([int]::TryParse($commitCountText, [ref]$commitCount)) {
					if ($commitCount -gt 0) {
						return "$latestTag-$commitCount"
					}
					return $latestTag
				}
			}
		}
	}

	if ($env:GITHUB_REF_TYPE -eq 'tag' -and $env:GITHUB_REF_NAME) {
		return $env:GITHUB_REF_NAME
	}

	$baseVersion = Get-ProjectDefaultVersion
	$shortRef = (& git -C $RootDir rev-parse --short=12 HEAD 2>$null)
	if ($LASTEXITCODE -eq 0 -and $shortRef) {
		return "$baseVersion-$($shortRef.Trim())"
	}

	if ($env:GITHUB_SHA) {
		return "$baseVersion-$($env:GITHUB_SHA.Substring(0, [Math]::Min(12, $env:GITHUB_SHA.Length)))"
	}

	return "$baseVersion-$((Get-Date).ToUniversalTime().ToString('yyyyMMddHHmmss'))"
}

function Write-PackageMetadata {
	$archiveFile = if (-not $NoArchive) { Split-Path -Leaf $ArchivePath } else { '' }
	$metadataPath = Join-Path $DistRoot 'package-metadata.env'
	$metadata = @(
		"PACKAGE_NAME=$PackageName",
		"PACKAGE_VERSION=$PackageVersion",
		"PACKAGE_PLATFORM=$PackagePlatform",
		"BUILD_TYPE=$BuildType",
		"ARCHIVE_FILE=$archiveFile"
	)
	Set-Content -LiteralPath $metadataPath -Value $metadata -Encoding ascii

	if ($env:GITHUB_OUTPUT) {
		Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "package_name=$PackageName"
		Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "package_version=$PackageVersion"
		Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "package_platform=$PackagePlatform"
		Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "archive_file=$archiveFile"
	}
}

Ensure-Environment

if (-not $PackageVersion) {
	$PackageVersion = Get-DefaultPackageVersion
}

if (-not $PackagePlatform) {
	$PackagePlatform = Get-DefaultPackagePlatform
}

$PackageVersion = Get-SanitizedTagPart $PackageVersion
$PackagePlatform = Get-SanitizedTagPart $PackagePlatform
if (-not $PackageVersion) {
	$PackageVersion = Get-SanitizedTagPart (Get-DefaultPackageVersion)
}
if (-not $PackagePlatform) {
	$PackagePlatform = Get-DefaultPackagePlatform
}
$PackageName = if ($env:PACKAGE_NAME) { $env:PACKAGE_NAME } else { "NavCaster-$PackageVersion-$PackagePlatform" }

New-Item -Path $DistRoot -ItemType Directory -Force | Out-Null
$DistRoot = (Resolve-Path $DistRoot).Path
$PackageDir = Join-Path $DistRoot $PackageName
$ArchivePath = Join-Path $DistRoot "$PackageName.zip"

Write-Host "[package] root       : $RootDir"
Write-Host "[package] build type : $BuildType"
Write-Host "[package] package    : $PackageName"
Write-Host "[package] dist root  : $DistRoot"

Push-Location $RootDir
try {
	if (-not $SkipContractCheck) {
		Invoke-Native node @('tools\contract_check\check_api_contracts.mjs')
	}

	if (-not $SkipWebBuild) {
		if (-not $SkipNpmCi) {
			Invoke-Native npm @('--prefix', 'app/web', 'ci')
		}
		Invoke-Native npm @('--prefix', 'app/web', 'run', 'build')
	}

	if (Test-Path $PackageDir) {
		Remove-Item $PackageDir -Recurse -Force
	}
	if (Test-Path $ArchivePath) {
		Remove-Item $ArchivePath -Force
	}

	$env:BUILD_TYPE = $BuildType
	$env:PACKAGE_NAME = $PackageName
	$env:PACKAGE_ROOT = $DistRoot
	try {
		& (Join-Path $RootDir 'deploy\ci\build_in_windows.ps1')
		if ($LASTEXITCODE -ne 0) {
			throw "build_in_windows.ps1 failed with exit code $LASTEXITCODE"
		}
	} finally {
		Remove-Item Env:\PACKAGE_ROOT -ErrorAction SilentlyContinue
	}

	if (-not $SkipCtest) {
		Invoke-Native (Join-Path $PackageDir 'bin\navcaster-caster.exe') @('--self-test', '--worker-count', '2', '--self-test-duration-ms', '250')
	}

	$required = @(
		'bin\navcaster-admin.exe',
		'bin\navcaster-agent.exe',
		'bin\navcaster-caster.exe',
		'web\index.html',
		'scripts',
		'app\admin\migrations\0001_v2_adminservice_foundation.sql',
		'app\agent\config.example.json'
	)
	foreach ($item in $required) {
		$path = Join-Path $PackageDir $item
		if (-not (Test-Path $path)) {
			throw "missing package item: $path"
		}
	}

	$archiveEntries = Get-ChildItem -LiteralPath $PackageDir -Force -Recurse -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match '[\\/]\.archive([\\/]|$)' }
	if ($archiveEntries) {
		throw 'package must not include .archive/v1'
	}

	if (-not $NoArchive) {
		Compress-Archive -Path $PackageDir -DestinationPath $ArchivePath -CompressionLevel Optimal
		Write-Host "[package] archive ready: $ArchivePath"
	}

	Write-PackageMetadata
	Write-Host "[package] package ready: $PackageDir"
} finally {
	Pop-Location
}
