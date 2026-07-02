$ErrorActionPreference = 'Stop'

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildType = if ($env:BUILD_TYPE) { $env:BUILD_TYPE } else { 'Release' }
$BuildDir = Join-Path $RootDir ("build\ci-" + $BuildType)
$RuntimeDir = Join-Path $RootDir ("bin\" + $BuildType)
$V2BinDir = Join-Path $BuildDir 'v2-bin'
if (-not $env:PACKAGE_ROOT) {
	throw "PACKAGE_ROOT must be set by deploy\scripts\package_windows.ps1"
}
if (-not $env:PACKAGE_NAME) {
	throw "PACKAGE_NAME must be set by deploy\scripts\package_windows.ps1"
}
$PackageRoot = $env:PACKAGE_ROOT
$PackageName = $env:PACKAGE_NAME
$PackageDir = Join-Path $PackageRoot $PackageName
$WebDistDir = if ($env:WEB_DIST_DIR) { $env:WEB_DIST_DIR } else { Join-Path $RootDir 'app\web\dist' }
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

New-Item -Path $V2BinDir -ItemType Directory -Force | Out-Null

Write-Host '[ci] app/admin go test/build'
Push-Location (Join-Path $RootDir 'app\admin')
try {
	go test ./...
	if ($LASTEXITCODE -ne 0) {
		throw "go test failed in app\admin with exit code $LASTEXITCODE"
	}
	go build -o (Join-Path $V2BinDir 'navcaster-admin.exe') .\cmd\navcaster-admin
	if ($LASTEXITCODE -ne 0) {
		throw "go build navcaster-admin failed with exit code $LASTEXITCODE"
	}
} finally {
	Pop-Location
}

Write-Host '[ci] app/agent go test/build'
Push-Location (Join-Path $RootDir 'app\agent')
try {
	go test ./...
	if ($LASTEXITCODE -ne 0) {
		throw "go test failed in app\agent with exit code $LASTEXITCODE"
	}
	go build -o (Join-Path $V2BinDir 'navcaster-agent.exe') .\cmd\navcaster-agent
	if ($LASTEXITCODE -ne 0) {
		throw "go build navcaster-agent failed with exit code $LASTEXITCODE"
	}
} finally {
	Pop-Location
}

cmake -S $RootDir -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$BuildType" -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl "-DCMAKE_MAKE_PROGRAM=$NinjaExe"
cmake --build $BuildDir --target navcaster-caster --parallel $Jobs
if ($LASTEXITCODE -ne 0) {
	throw "cmake build navcaster-caster failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path (Join-Path $WebDistDir 'index.html'))) {
	throw "missing web build output: $(Join-Path $WebDistDir 'index.html')"
}

if (-not (Test-Path (Join-Path $RuntimeDir 'navcaster-caster.exe'))) {
	throw "missing app/caster build output: $(Join-Path $RuntimeDir 'navcaster-caster.exe')"
}

New-Item -Path $PackageDir -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'bin') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'logs') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'web') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'scripts') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'env') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'app\admin') -ItemType Directory -Force | Out-Null
New-Item -Path (Join-Path $PackageDir 'app\agent') -ItemType Directory -Force | Out-Null

Copy-Item -Path (Join-Path $V2BinDir 'navcaster-admin.exe') -Destination (Join-Path $PackageDir 'bin') -Force
Copy-Item -Path (Join-Path $V2BinDir 'navcaster-agent.exe') -Destination (Join-Path $PackageDir 'bin') -Force
Copy-Item -Path (Join-Path $RuntimeDir 'navcaster-caster.exe') -Destination (Join-Path $PackageDir 'bin') -Force

Get-ChildItem -Path $RuntimeDir -File -Filter '*.dll' | ForEach-Object {
	Copy-Item -Path $_.FullName -Destination (Join-Path $PackageDir 'bin') -Force
}

Copy-Item -Path (Join-Path $WebDistDir '*') -Destination (Join-Path $PackageDir 'web') -Recurse -Force
Copy-Item -Path (Join-Path $RootDir 'deploy\scripts\*') -Destination (Join-Path $PackageDir 'scripts') -Recurse -Force
Copy-Item -Path (Join-Path $RootDir 'app\admin\migrations') -Destination (Join-Path $PackageDir 'app\admin') -Recurse -Force
Copy-Item -Path (Join-Path $RootDir 'app\agent\config.example.json') -Destination (Join-Path $PackageDir 'app\agent') -Force

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

$archiveEntries = Get-ChildItem -LiteralPath $PackageDir -Force -Recurse -ErrorAction SilentlyContinue |
	Where-Object { $_.FullName -match '[\\/]\.archive([\\/]|$)' }
if ($archiveEntries) {
	throw "package must not include .archive/v1"
}

Write-Host "[ci] package ready: $PackageDir"
