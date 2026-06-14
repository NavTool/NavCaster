#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
CONFIGURE_ONLY=0
TARGETS=()

detect_jobs() {
	if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
		echo "${CMAKE_BUILD_PARALLEL_LEVEL}"
	elif command -v nproc >/dev/null 2>&1; then
		nproc
	elif command -v getconf >/dev/null 2>&1; then
		getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2
	else
		echo 2
	fi
}

JOBS="$(detect_jobs)"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--build-type)
			BUILD_TYPE="$2"
			shift 2
			;;
		--target)
			TARGETS+=("$2")
			shift 2
			;;
		--jobs)
			JOBS="$2"
			shift 2
			;;
		--configure-only)
			CONFIGURE_ONLY=1
			shift
			;;
		*)
			echo "unknown argument: $1" >&2
			exit 2
			;;
	esac
done

case "${BUILD_TYPE}" in
	Release|Debug) ;;
	*)
		echo "unsupported build type: ${BUILD_TYPE}" >&2
		exit 2
		;;
esac

PRESET="ninja-${BUILD_TYPE,,}"

if ! command -v ninja >/dev/null 2>&1; then
	echo "Ninja executable not found. Install ninja-build or set PATH to ninja." >&2
	exit 1
fi

echo "[build] preset : ${PRESET}"
echo "[build] ninja  : $(command -v ninja)"
echo "[build] jobs   : ${JOBS}"

cd "${ROOT_DIR}"
cmake --preset "${PRESET}"

if [[ "${CONFIGURE_ONLY}" == "1" ]]; then
	exit 0
fi

BUILD_ARGS=(--build --preset "${PRESET}" --parallel "${JOBS}")
if [[ "${#TARGETS[@]}" -gt 0 ]]; then
	BUILD_ARGS+=(--target "${TARGETS[@]}")
fi

cmake "${BUILD_ARGS[@]}"
