param(
	[ValidateSet('Release', 'Debug')]
	[string]$BuildType = 'Release',
	[string[]]$Target = @(),
	[int]$Jobs = 0,
	[switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Preset = "ninja-$BuildType".ToLowerInvariant()
if ($Jobs -le 0) {
	$Jobs = if ($env:CMAKE_BUILD_PARALLEL_LEVEL) { [int]$env:CMAKE_BUILD_PARALLEL_LEVEL } elseif ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 2 }
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

		Write-Host "[build] loading MSVC environment: $($vsDevCmd.FullName)"
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

function Invoke-NativeCommand {
	param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Command)
	& $Command[0] @($Command | Select-Object -Skip 1)
	if ($LASTEXITCODE -ne 0) {
		throw "command failed with exit code ${LASTEXITCODE}: $($Command -join ' ')"
	}
}

Import-MsvcDeveloperEnvironment
$NinjaExe = Resolve-NinjaExecutable
Write-Host "[build] preset : $Preset"
Write-Host "[build] ninja  : $NinjaExe"
Write-Host "[build] jobs   : $Jobs"

Push-Location $RootDir
try {
	Invoke-NativeCommand cmake --preset $Preset "-DCMAKE_C_COMPILER=cl" "-DCMAKE_CXX_COMPILER=cl" "-DCMAKE_MAKE_PROGRAM=$NinjaExe"
	if ($ConfigureOnly) {
		return
	}

	$buildArgs = @('--build', '--preset', $Preset, '--parallel', "$Jobs")
	if ($Target.Count -gt 0) {
		$buildArgs += '--target'
		$buildArgs += $Target
	}

	Invoke-NativeCommand cmake @buildArgs
} finally {
	Pop-Location
}
