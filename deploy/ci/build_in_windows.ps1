$ErrorActionPreference = 'Stop'

$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildType = if ($env:BUILD_TYPE) { $env:BUILD_TYPE } else { 'Release' }
$BuildDir = Join-Path $RootDir ("build\ci-" + $BuildType)
$RuntimeDir = Join-Path $RootDir ("bin\" + $BuildType)
$PackageRoot = Join-Path $RootDir 'release'
$PackageName = if ($env:PACKAGE_NAME) { $env:PACKAGE_NAME } else { "NavCaster-$BuildType" }
$PackageDir = Join-Path $PackageRoot $PackageName
$WebDistDir = if ($env:WEB_DIST_DIR) { $env:WEB_DIST_DIR } else { Join-Path $RootDir 'web\dist' }
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

if (Test-Path $BuildDir) {
	Remove-Item $BuildDir -Recurse -Force
}

if (Test-Path $PackageDir) {
	Remove-Item $PackageDir -Recurse -Force
}

cmake -S $RootDir -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType -DCMAKE_CXX_COMPILER=cl -DCMAKE_C_COMPILER=cl
cmake --build $BuildDir --config $BuildType --parallel

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