$ErrorActionPreference = 'Stop'

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildType = if ($env:BUILD_TYPE) { $env:BUILD_TYPE } else { 'Release' }
$BuildDir = Join-Path $RootDir ("build\ci-" + $BuildType)
$RuntimeDir = Join-Path $RootDir ("bin\" + $BuildType)
$PackageRoot = Join-Path $RootDir 'release'
$PackageName = if ($env:PACKAGE_NAME) { $env:PACKAGE_NAME } else { "NavCaster-$BuildType" }
$PackageDir = Join-Path $PackageRoot $PackageName
$WebDistDir = if ($env:WEB_DIST_DIR) { $env:WEB_DIST_DIR } else { Join-Path $RootDir 'web\dist' }
$Jobs = if ($env:CMAKE_BUILD_PARALLEL_LEVEL) { [int]$env:CMAKE_BUILD_PARALLEL_LEVEL } elseif ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 2 }

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

	throw "Ninja executable not found. Install ninja-build, use a Visual Studio developer environment, or set NINJA_EXE."
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
		return
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

		Write-Host "[ci] loading MSVC environment: $($vsDevCmd.FullName)"
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
			return
		}
	}

	throw "MSVC compiler cl.exe not found. Run from a Visual Studio developer shell or install Visual Studio C++ tools."
}

Import-MsvcDeveloperEnvironment
$NinjaExe = Resolve-NinjaExecutable
$Binaries = @(
	'CasterService.exe',
	'reg_check.exe',
	'ntrip_client_sim_0.0.2.exe',
	'ntrip_server_sim_0.0.2.exe',
	'strsvr_mult.exe',
	'rtklib_rnx2rtkp.exe',
	'rtklib_rtkconv.exe'
)

Write-Host "[ci] build type : $BuildType"
Write-Host "[ci] build dir  : $BuildDir"
Write-Host "[ci] package dir: $PackageDir"
Write-Host "[ci] generator  : Ninja"
Write-Host "[ci] ninja      : $NinjaExe"
Write-Host "[ci] jobs       : $Jobs"

if (Test-Path $BuildDir) {
	Remove-Item $BuildDir -Recurse -Force
}

if (Test-Path $PackageDir) {
	Remove-Item $PackageDir -Recurse -Force
}

cmake -S $RootDir -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=$BuildType -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl "-DCMAKE_MAKE_PROGRAM=$NinjaExe"
cmake --build $BuildDir --parallel $Jobs

if (-not (Test-Path (Join-Path $RuntimeDir 'conf'))) {
	throw "missing runtime config directory: $(Join-Path $RuntimeDir 'conf')"
}

if (-not (Test-Path (Join-Path $WebDistDir 'index.html'))) {
	throw "missing web build output: $(Join-Path $WebDistDir 'index.html')"
}

New-Item -Path $PackageDir -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'conf') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'logs') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'web') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'scripts') -ItemType Directory -Force | Out-Null

foreach ($binary in $Binaries) {
	$source = Join-Path $RuntimeDir $binary
	if (Test-Path $source) {
		Copy-Item -Path $source -Destination $PackageDir -Force
	}
}

Get-ChildItem -Path $RuntimeDir -File -Filter '*.dll' | ForEach-Object {
	Copy-Item -Path $_.FullName -Destination $PackageDir -Force
}

Copy-Item -Path (Join-Path $RuntimeDir 'conf\*') -Destination (Join-Path $PackageDir 'conf') -Recurse -Force
Copy-Item -Path (Join-Path $WebDistDir '*') -Destination (Join-Path $PackageDir 'web') -Recurse -Force
Copy-Item -Path (Join-Path $RootDir 'deploy\scripts\*') -Destination (Join-Path $PackageDir 'scripts') -Recurse -Force

$obsoleteScripts = @(
	'scripts\systemd\install_redis_service.sh',
	'scripts\systemd\uninstall_redis_service.sh',
	'scripts\supervisor\install_redis_service.sh',
	'scripts\supervisor\uninstall_redis_service.sh'
)

foreach ($relativePath in $obsoleteScripts) {
	$targetPath = Join-Path $PackageDir $relativePath
	if (Test-Path $targetPath) {
		Remove-Item -Path $targetPath -Force
	}
}

$serviceConfig = Join-Path $PackageDir 'conf\Service_Setting.yml'
(Get-Content -Path $serviceConfig) -replace 'Web_Root: ""', 'Web_Root: "./web"' | Set-Content -Path $serviceConfig -Encoding utf8

Write-Host "[ci] package ready: $PackageDir"
