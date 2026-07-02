#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${ROOT_DIR}/build/ci-${BUILD_TYPE}"
RUNTIME_DIR="${ROOT_DIR}/bin/${BUILD_TYPE}"
V2_BIN_DIR="${BUILD_DIR}/v2-bin"
if [[ -z "${PACKAGE_ROOT:-}" ]]; then
	echo "[ci] PACKAGE_ROOT must be set by deploy/scripts/package_linux.sh" >&2
	exit 2
fi
if [[ -z "${PACKAGE_NAME:-}" ]]; then
	echo "[ci] PACKAGE_NAME must be set by deploy/scripts/package_linux.sh" >&2
	exit 2
fi
PACKAGE_DIR="${PACKAGE_ROOT}/${PACKAGE_NAME}"
WEB_DIST_DIR="${WEB_DIST_DIR:-${ROOT_DIR}/app/web/dist}"
REDIS_VERSION="${REDIS_VERSION:-8.6.3}" # NavCaster minimum is 8.4.0; 8.6.3 is the verified package default.
REDIS_REPO_URL="${REDIS_REPO_URL:-https://github.com/redis/redis.git}"
REDIS_SRC_DIR="${BUILD_DIR}/redis-src"
REDIS_PACKAGE_DIR="${PACKAGE_DIR}/env/redis"

# Portable parallel job count detection (nproc may be missing on some distros).
detect_jobs() {
	if command -v nproc >/dev/null 2>&1; then
		nproc
	elif command -v getconf >/dev/null 2>&1; then
		getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2
	else
		echo 2
	fi
}
JOBS="$(detect_jobs)"

clone_redis_source() {
	local candidate_refs=("${REDIS_VERSION}")

	if [[ "${REDIS_VERSION}" == v* ]]; then
		candidate_refs+=("${REDIS_VERSION#v}")
	else
		candidate_refs+=("v${REDIS_VERSION}")
	fi

	for ref in "${candidate_refs[@]}"; do
		rm -rf "${REDIS_SRC_DIR}"
		if git clone --depth 1 --branch "${ref}" "${REDIS_REPO_URL}" "${REDIS_SRC_DIR}"; then
			return 0
		fi
	done

	echo "[ci] failed to fetch redis source for version: ${REDIS_VERSION}" >&2
	return 1
}

build_and_package_redis() {
	echo "[ci] redis version: ${REDIS_VERSION}"

	rm -rf "${REDIS_SRC_DIR}" "${REDIS_PACKAGE_DIR}"
	clone_redis_source

	make -C "${REDIS_SRC_DIR}" BUILD_TLS=no MALLOC=libc -j"${JOBS}"

	if [[ ! -f "${REDIS_SRC_DIR}/src/redis-server" ]]; then
		echo "[ci] missing redis build output: ${REDIS_SRC_DIR}/src/redis-server" >&2
		exit 1
	fi

	mkdir -p "${REDIS_PACKAGE_DIR}"

	local redis_binaries=(
		redis-server
		redis-cli
		redis-benchmark
		redis-check-aof
		redis-check-rdb
		redis-sentinel
	)

	for binary in "${redis_binaries[@]}"; do
		if [[ -f "${REDIS_SRC_DIR}/src/${binary}" ]]; then
			cp "${REDIS_SRC_DIR}/src/${binary}" "${REDIS_PACKAGE_DIR}/"
		fi
	done

	if [[ -f "${REDIS_SRC_DIR}/redis.conf" ]]; then
		cp "${REDIS_SRC_DIR}/redis.conf" "${REDIS_PACKAGE_DIR}/redis.conf"
		cp "${REDIS_SRC_DIR}/redis.conf" "${REDIS_PACKAGE_DIR}/redis.config"
	fi
}

echo "[ci] build type: ${BUILD_TYPE}"
echo "[ci] build dir : ${BUILD_DIR}"
echo "[ci] package dir: ${PACKAGE_DIR}"
echo "[ci] generator : Ninja"
echo "[ci] jobs      : ${JOBS}"

rm -rf "${BUILD_DIR}" "${PACKAGE_DIR}"
mkdir -p "${V2_BIN_DIR}"

echo "[ci] app/admin go test/build"
(
	cd "${ROOT_DIR}/app/admin"
	go test ./...
	go build -o "${V2_BIN_DIR}/navcaster-admin" ./cmd/navcaster-admin
)

echo "[ci] app/agent go test/build"
(
	cd "${ROOT_DIR}/app/agent"
	go test ./...
	go build -o "${V2_BIN_DIR}/navcaster-agent" ./cmd/navcaster-agent
)

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" --target navcaster-caster --parallel "${JOBS}"

if [[ ! -f "${WEB_DIST_DIR}/index.html" ]]; then
	echo "[ci] missing web build output: ${WEB_DIST_DIR}/index.html" >&2
	echo "[ci] run npm ci && npm run build in app/web/ before packaging" >&2
	exit 1
fi

if [[ ! -f "${RUNTIME_DIR}/navcaster-caster" ]]; then
	echo "[ci] missing app/caster build output: ${RUNTIME_DIR}/navcaster-caster" >&2
	exit 1
fi

mkdir -p "${PACKAGE_DIR}/bin" "${PACKAGE_DIR}/logs" "${PACKAGE_DIR}/web" "${PACKAGE_DIR}/scripts" "${PACKAGE_DIR}/env" "${PACKAGE_DIR}/app/admin" "${PACKAGE_DIR}/app/agent"

cp "${V2_BIN_DIR}/navcaster-admin" "${PACKAGE_DIR}/bin/"
cp "${V2_BIN_DIR}/navcaster-agent" "${PACKAGE_DIR}/bin/"
cp "${RUNTIME_DIR}/navcaster-caster" "${PACKAGE_DIR}/bin/"
cp -r "${WEB_DIST_DIR}/." "${PACKAGE_DIR}/web/"
cp -r "${ROOT_DIR}/deploy/scripts/." "${PACKAGE_DIR}/scripts/"
cp -r "${ROOT_DIR}/app/admin/migrations" "${PACKAGE_DIR}/app/admin/"
cp "${ROOT_DIR}/app/agent/config.example.json" "${PACKAGE_DIR}/app/agent/"

build_and_package_redis

if find "${PACKAGE_DIR}" -path '*/.archive' -o -path '*/.archive/*' | grep -q .; then
	echo "[ci] package must not include .archive/v1" >&2
	exit 1
fi

echo "[ci] package ready: ${PACKAGE_DIR}"
