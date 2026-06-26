#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DIST_ROOT="${DIST_ROOT:-${ROOT_DIR}/dist}"
PACKAGE_VERSION="${PACKAGE_VERSION:-}"
PACKAGE_PLATFORM="${PACKAGE_PLATFORM:-}"
REDIS_VERSION="${REDIS_VERSION:-8.6.3}"
SKIP_NPM_CI=0
SKIP_WEB_BUILD=0
SKIP_CONTRACT_CHECK=0
SKIP_CTEST=0
ARCHIVE=1
NO_INSTALL=0
APT_UPDATED=0
CMAKE_MIN_VERSION="3.21.0"
CMAKE_BOOTSTRAP_VERSION="${CMAKE_BOOTSTRAP_VERSION:-3.29.8}"
NODE_MIN_MAJOR="${NODE_MIN_MAJOR:-20}"
NODE_BOOTSTRAP_VERSION="${NODE_BOOTSTRAP_VERSION:-20.19.5}"

usage() {
	cat <<'EOF'
Usage: deploy/scripts/package_linux.sh [options]

Build a Linux NavCaster package into dist/ for CI and manual releases.

Options:
  --build-type <Release|Debug>      CMake build type. Defaults to Release.
  --dist-root <path>                Output root. Defaults to <repo>/dist.
  --package-version <version>       Version string used in package name. Defaults to latest git tag plus commit count.
  --package-platform <platform>     Platform suffix. Defaults to detected distro/architecture.
  --redis-version <version>         Redis version to package. Defaults to 8.6.3.
  --skip-npm-ci                     Reuse existing web/node_modules.
  --skip-web-build                  Reuse existing web/dist.
  --skip-contract-check             Skip API contract check.
  --skip-ctest                      Skip schema_smoke CTest after package build.
  --no-archive                      Do not create dist/<package>.tar.gz.
  --no-install                      Check prerequisites but do not install missing tools.
  -h, --help                        Show this help.

Output:
  dist/<package-name>/
  dist/<package-name>.tar.gz
EOF
}

sanitize_tag_part() {
	printf '%s' "$1" | tr '/:@ ' '----' | tr -cd 'A-Za-z0-9_.-'
}

project_default_version() {
	local major="0"
	local minor="0"
	local patch="0"
	local extra="0"
	local line

	if [[ -f "${ROOT_DIR}/CMakeLists.txt" ]]; then
		while IFS= read -r line; do
			case "${line}" in
				*"set(VERSION_MAJOR "*)
					major="$(printf '%s' "${line}" | sed -nE 's/^[[:space:]]*set\(VERSION_MAJOR[[:space:]]+([0-9]+)\).*/\1/p')"
					;;
				*"set(VERSION_MINOR "*)
					minor="$(printf '%s' "${line}" | sed -nE 's/^[[:space:]]*set\(VERSION_MINOR[[:space:]]+([0-9]+)\).*/\1/p')"
					;;
				*"set(VERSION_PATCH "*)
					patch="$(printf '%s' "${line}" | sed -nE 's/^[[:space:]]*set\(VERSION_PATCH[[:space:]]+([0-9]+)\).*/\1/p')"
					;;
				*"set(VERSION_EXTRA "*)
					extra="$(printf '%s' "${line}" | sed -nE 's/^[[:space:]]*set\(VERSION_EXTRA[[:space:]]+([0-9]+)\).*/\1/p')"
					;;
			esac
		done < "${ROOT_DIR}/CMakeLists.txt"
	fi

	printf '%s.%s.%s.%s' "${major:-0}" "${minor:-0}" "${patch:-0}" "${extra:-0}"
}

resolve_default_version() {
	local latest_tag=""
	if latest_tag="$(git -C "${ROOT_DIR}" describe --tags --abbrev=0 2>/dev/null)" && [[ -n "${latest_tag}" ]]; then
		local commit_count=""
		if commit_count="$(git -C "${ROOT_DIR}" rev-list "${latest_tag}..HEAD" --count 2>/dev/null)" && [[ "${commit_count}" =~ ^[0-9]+$ ]]; then
			if [[ "${commit_count}" == "0" ]]; then
				printf '%s' "${latest_tag}"
			else
				printf '%s-%s' "${latest_tag}" "${commit_count}"
			fi
			return
		fi
	fi

	if [[ "${GITHUB_REF_TYPE:-}" == "tag" && -n "${GITHUB_REF_NAME:-}" ]]; then
		printf '%s' "${GITHUB_REF_NAME}"
		return
	fi

	local base_version
	base_version="$(project_default_version)"

	local short_ref=""
	if short_ref="$(git -C "${ROOT_DIR}" rev-parse --short=12 HEAD 2>/dev/null)" && [[ -n "${short_ref}" ]]; then
		printf '%s-%s' "${base_version}" "${short_ref}"
		return
	fi

	if [[ -n "${GITHUB_SHA:-}" ]]; then
		printf '%s-%s' "${base_version}" "${GITHUB_SHA:0:12}"
		return
	fi

	printf '%s-%s' "${base_version}" "$(date -u '+%Y%m%d%H%M%S')"
}

detect_default_platform() {
	local distro="linux"
	local version_id=""
	local arch

	if [[ -r /etc/os-release ]]; then
		# shellcheck disable=SC1091
		. /etc/os-release
		distro="${ID:-linux}"
		version_id="${VERSION_ID:-}"
	fi

	case "$(uname -m)" in
		x86_64) arch="amd64" ;;
		aarch64) arch="arm64" ;;
		*) arch="$(sanitize_tag_part "$(uname -m)")" ;;
	esac

	if [[ -n "${version_id}" ]]; then
		printf '%s-%s-%s' "${distro}" "${version_id}" "${arch}"
	else
		printf '%s-%s' "${distro}" "${arch}"
	fi
}

write_package_metadata() {
	local archive_file=""
	local metadata_path="${DIST_ROOT}/package-metadata.env"
	if [[ "${ARCHIVE}" == "1" ]]; then
		archive_file="$(basename "${ARCHIVE_PATH}")"
	fi

	{
		printf 'PACKAGE_NAME=%s\n' "${PACKAGE_NAME}"
		printf 'PACKAGE_VERSION=%s\n' "${PACKAGE_VERSION}"
		printf 'PACKAGE_PLATFORM=%s\n' "${PACKAGE_PLATFORM}"
		printf 'BUILD_TYPE=%s\n' "${BUILD_TYPE}"
		printf 'ARCHIVE_FILE=%s\n' "${archive_file}"
	} > "${metadata_path}"

	if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
		{
			printf 'package_name=%s\n' "${PACKAGE_NAME}"
			printf 'package_version=%s\n' "${PACKAGE_VERSION}"
			printf 'package_platform=%s\n' "${PACKAGE_PLATFORM}"
			printf 'archive_file=%s\n' "${archive_file}"
		} >> "${GITHUB_OUTPUT}"
	fi
}

command_exists() {
	command -v "$1" >/dev/null 2>&1
}

version_ge() {
	[[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n 1)" == "$2" ]]
}

run_as_root() {
	if [[ "${EUID}" -eq 0 ]]; then
		"$@"
	elif command_exists sudo; then
		sudo "$@"
	else
		echo "[package] missing sudo; rerun as root or install prerequisites manually" >&2
		return 1
	fi
}

apt_update_once() {
	if [[ "${APT_UPDATED}" == "1" ]]; then
		return
	fi
	if [[ "${NO_INSTALL}" == "1" ]]; then
		echo "[package] apt update skipped because --no-install was set" >&2
		return 1
	fi
	command_exists apt-get || {
		echo "[package] apt-get not found; install prerequisites manually or use the Docker builder" >&2
		return 1
	}
	run_as_root apt-get update
	APT_UPDATED=1
}

apt_install() {
	local missing=()
	local pkg
	if command_exists dpkg-query; then
		for pkg in "$@"; do
			if ! dpkg-query -W -f='${Status}' "${pkg}" 2>/dev/null | grep -q "install ok installed"; then
				missing+=("${pkg}")
			fi
		done
	else
		missing=("$@")
	fi

	if [[ "${#missing[@]}" -eq 0 ]]; then
		return
	fi

	if [[ "${NO_INSTALL}" == "1" ]]; then
		echo "[package] missing dependency and --no-install was set: ${missing[*]}" >&2
		return 1
	fi
	apt_update_once
	run_as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
}

ensure_command() {
	local command_name="$1"
	shift
	if command_exists "${command_name}"; then
		return
	fi
	apt_install "$@"
	command_exists "${command_name}" || {
		echo "[package] failed to provide command: ${command_name}" >&2
		exit 1
	}
}

host_arch() {
	case "$(uname -m)" in
		x86_64) echo "x86_64" ;;
		aarch64) echo "aarch64" ;;
		*)
			echo "[package] unsupported architecture: $(uname -m)" >&2
			exit 1
			;;
	esac
}

node_arch() {
	case "$(uname -m)" in
		x86_64) echo "x64" ;;
		aarch64) echo "arm64" ;;
		*)
			echo "[package] unsupported Node.js architecture: $(uname -m)" >&2
			exit 1
			;;
	esac
}

install_cmake_binary() {
	if [[ "${NO_INSTALL}" == "1" ]]; then
		echo "[package] CMake ${CMAKE_MIN_VERSION}+ is required and --no-install was set" >&2
		exit 1
	fi
	ensure_command wget wget ca-certificates
	local arch
	arch="$(host_arch)"
	local installer="/tmp/cmake-${CMAKE_BOOTSTRAP_VERSION}-linux-${arch}.sh"
	echo "[package] installing CMake ${CMAKE_BOOTSTRAP_VERSION} for ${arch}"
	wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_BOOTSTRAP_VERSION}/cmake-${CMAKE_BOOTSTRAP_VERSION}-linux-${arch}.sh" -O "${installer}"
	run_as_root sh "${installer}" --skip-license --prefix=/usr/local
	rm -f "${installer}"
	hash -r
}

install_node_binary() {
	if [[ "${NO_INSTALL}" == "1" ]]; then
		echo "[package] Node.js ${NODE_MIN_MAJOR}+ is required and --no-install was set" >&2
		exit 1
	fi
	ensure_command wget wget ca-certificates
	ensure_command tar tar
	apt_install xz-utils
	local arch
	arch="$(node_arch)"
	local archive="/tmp/node-v${NODE_BOOTSTRAP_VERSION}-linux-${arch}.tar.xz"
	echo "[package] installing Node.js ${NODE_BOOTSTRAP_VERSION} for ${arch}"
	wget -q "https://nodejs.org/dist/v${NODE_BOOTSTRAP_VERSION}/node-v${NODE_BOOTSTRAP_VERSION}-linux-${arch}.tar.xz" -O "${archive}"
	run_as_root tar -xJf "${archive}" -C /usr/local --strip-components=1
	rm -f "${archive}"
	hash -r
}

ensure_cmake() {
	if command_exists cmake; then
		local current
		current="$(cmake --version | head -n 1 | awk '{print $3}')"
		if version_ge "${current}" "${CMAKE_MIN_VERSION}"; then
			return
		fi
		echo "[package] CMake ${current} is older than required ${CMAKE_MIN_VERSION}; installing pinned CMake"
	else
		echo "[package] CMake not found; installing pinned CMake"
	fi
	install_cmake_binary
	local installed
	installed="$(cmake --version | head -n 1 | awk '{print $3}')"
	version_ge "${installed}" "${CMAKE_MIN_VERSION}" || {
		echo "[package] CMake ${installed} is still older than ${CMAKE_MIN_VERSION}" >&2
		exit 1
	}
}

ensure_node() {
	if command_exists node && command_exists npm; then
		local major
		major="$(node --version | sed -E 's/^v([0-9]+).*/\1/')"
		if [[ "${major}" -ge "${NODE_MIN_MAJOR}" ]]; then
			return
		fi
		echo "[package] Node.js $(node --version) is older than required major ${NODE_MIN_MAJOR}; installing pinned Node.js"
	else
		echo "[package] Node.js/npm not found; installing pinned Node.js"
	fi
	install_node_binary
	command_exists node && command_exists npm || {
		echo "[package] failed to provide node/npm" >&2
		exit 1
	}
}

third_party_ready() {
	local required=(
		"third_party/abseil-cpp/CMakeLists.txt"
		"third_party/protobuf/CMakeLists.txt"
		"third_party/libevent/CMakeLists.txt"
		"third_party/hiredis/CMakeLists.txt"
		"third_party/yaml-cpp/CMakeLists.txt"
		"third_party/rtklib/CMakeLists.txt"
		"third_party/json/include/nlohmann/json.hpp"
		"third_party/spdlog/include/spdlog/spdlog.h"
	)
	local item
	for item in "${required[@]}"; do
		if [[ ! -e "${ROOT_DIR}/${item}" ]]; then
			return 1
		fi
	done
	return 0
}

ensure_third_party() {
	if third_party_ready; then
		return
	fi

	echo "[package] third_party dependencies are incomplete; hydrating..."
	if [[ "${NO_INSTALL}" == "1" ]]; then
		echo "[package] third_party dependencies are incomplete and --no-install was set" >&2
		exit 1
	fi

	local team_hydrate="F:/Projects/NavCaster/_team/scripts/HYDRATE_WORKTREE_SUBMODULES.ps1"
	local source_repo="F:/Projects/NavCaster/repo"
	if [[ -f "${team_hydrate}" ]] && command_exists powershell; then
		powershell -ExecutionPolicy Bypass -File "${team_hydrate}" -WorktreePath "${ROOT_DIR}" -SourceRepo "${source_repo}"
	else
		git -C "${ROOT_DIR}" submodule update --init --recursive
	fi

	if ! third_party_ready; then
		echo "[package] third_party dependencies are still incomplete after hydration" >&2
		exit 1
	fi
}

ensure_environment() {
	echo "[package] checking build environment"
	ensure_command git git
	ensure_command make make
	ensure_command gcc gcc
	ensure_command g++ g++
	ensure_command ninja ninja-build
	ensure_command curl curl
	ensure_command tar tar
	if [[ "${NO_INSTALL}" != "1" ]]; then
		apt_install ca-certificates pkg-config libssl-dev zlib1g-dev
	fi
	ensure_cmake
	ensure_node
	echo "[package] git    : $(git --version)"
	echo "[package] node   : $(node --version)"
	echo "[package] npm    : $(npm --version)"
	echo "[package] cmake  : $(cmake --version | head -n 1)"
	echo "[package] ninja  : $(ninja --version)"
	echo "[package] gcc    : $(gcc --version | head -n 1)"
	ensure_third_party
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--build-type)
			BUILD_TYPE="$2"
			shift 2
			;;
		--dist-root)
			DIST_ROOT="$2"
			shift 2
			;;
		--package-version)
			PACKAGE_VERSION="$2"
			shift 2
			;;
		--package-platform)
			PACKAGE_PLATFORM="$2"
			shift 2
			;;
		--redis-version)
			REDIS_VERSION="$2"
			shift 2
			;;
		--skip-npm-ci)
			SKIP_NPM_CI=1
			shift
			;;
		--skip-web-build)
			SKIP_WEB_BUILD=1
			shift
			;;
		--skip-contract-check)
			SKIP_CONTRACT_CHECK=1
			shift
			;;
		--skip-ctest)
			SKIP_CTEST=1
			shift
			;;
		--no-archive)
			ARCHIVE=0
			shift
			;;
		--no-install)
			NO_INSTALL=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "[package] unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

case "${BUILD_TYPE}" in
	Release|Debug) ;;
	*)
		echo "[package] unsupported build type: ${BUILD_TYPE}" >&2
		exit 2
		;;
esac

ensure_environment

if [[ -z "${PACKAGE_VERSION}" ]]; then
	PACKAGE_VERSION="$(resolve_default_version)"
fi

if [[ -z "${PACKAGE_PLATFORM}" ]]; then
	PACKAGE_PLATFORM="$(detect_default_platform)"
fi

PACKAGE_VERSION="$(sanitize_tag_part "${PACKAGE_VERSION}")"
PACKAGE_PLATFORM="$(sanitize_tag_part "${PACKAGE_PLATFORM}")"
if [[ -z "${PACKAGE_VERSION}" ]]; then
	PACKAGE_VERSION="$(sanitize_tag_part "$(resolve_default_version)")"
fi
if [[ -z "${PACKAGE_PLATFORM}" ]]; then
	PACKAGE_PLATFORM="$(detect_default_platform)"
fi
PACKAGE_NAME="${PACKAGE_NAME:-NavCaster-${PACKAGE_VERSION}-${PACKAGE_PLATFORM}}"
DIST_ROOT="$(cd "${ROOT_DIR}" && mkdir -p "${DIST_ROOT}" && cd "${DIST_ROOT}" && pwd)"
PACKAGE_DIR="${DIST_ROOT}/${PACKAGE_NAME}"
ARCHIVE_PATH="${DIST_ROOT}/${PACKAGE_NAME}.tar.gz"

echo "[package] root       : ${ROOT_DIR}"
echo "[package] build type : ${BUILD_TYPE}"
echo "[package] package    : ${PACKAGE_NAME}"
echo "[package] dist root  : ${DIST_ROOT}"

cd "${ROOT_DIR}"

if [[ "${SKIP_CONTRACT_CHECK}" != "1" ]]; then
	node tools/contract_check/check_api_contracts.mjs
fi

if [[ "${SKIP_WEB_BUILD}" != "1" ]]; then
	if [[ "${SKIP_NPM_CI}" != "1" ]]; then
		npm --prefix web ci
	fi
	npm --prefix web run build
fi

rm -rf "${PACKAGE_DIR}" "${ARCHIVE_PATH}"

BUILD_TYPE="${BUILD_TYPE}" \
PACKAGE_NAME="${PACKAGE_NAME}" \
PACKAGE_ROOT="${DIST_ROOT}" \
REDIS_VERSION="${REDIS_VERSION}" \
bash "${ROOT_DIR}/deploy/ci/build_in_linux.sh"

if [[ "${SKIP_CTEST}" != "1" ]]; then
	ctest --test-dir "${ROOT_DIR}/build/ci-${BUILD_TYPE}" --output-on-failure -R schema_smoke
fi

required=(
	"CasterService"
	"conf/Service_Setting.yml"
	"web/index.html"
	"scripts"
)

for item in "${required[@]}"; do
	if [[ ! -e "${PACKAGE_DIR}/${item}" ]]; then
		echo "[package] missing package item: ${PACKAGE_DIR}/${item}" >&2
		exit 1
	fi
done

if [[ "${ARCHIVE}" == "1" ]]; then
	tar -C "${DIST_ROOT}" -czf "${ARCHIVE_PATH}" "${PACKAGE_NAME}"
	echo "[package] archive ready: ${ARCHIVE_PATH}"
fi

write_package_metadata
echo "[package] package ready: ${PACKAGE_DIR}"
