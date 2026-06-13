#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${ROOT_DIR}/build/ci-${BUILD_TYPE}"
RUNTIME_DIR="${ROOT_DIR}/bin/${BUILD_TYPE}"
PACKAGE_ROOT="${ROOT_DIR}/release"
PACKAGE_NAME="${PACKAGE_NAME:-NavCaster-${BUILD_TYPE}}"
PACKAGE_DIR="${PACKAGE_ROOT}/${PACKAGE_NAME}"
WEB_DIST_DIR="${WEB_DIST_DIR:-${ROOT_DIR}/web/dist}"
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

BINARIES=(
	CasterService
	reg_check
	ntrip_client_sim_0.0.2
	ntrip_server_sim_0.0.2
	strsvr_mult
	rtklib_rnx2rtkp
	rtklib_rtkconv
)

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

rm -rf "${BUILD_DIR}" "${PACKAGE_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

if [[ ! -d "${RUNTIME_DIR}/conf" ]]; then
	echo "[ci] missing runtime config directory: ${RUNTIME_DIR}/conf" >&2
	exit 1
fi

if [[ ! -f "${WEB_DIST_DIR}/index.html" ]]; then
	echo "[ci] missing web build output: ${WEB_DIST_DIR}/index.html" >&2
	echo "[ci] run npm ci && npm run build in web/ before packaging" >&2
	exit 1
fi

mkdir -p "${PACKAGE_DIR}/conf" "${PACKAGE_DIR}/logs" "${PACKAGE_DIR}/web" "${PACKAGE_DIR}/scripts" "${PACKAGE_DIR}/env"

for binary in "${BINARIES[@]}"; do
	if [[ -f "${RUNTIME_DIR}/${binary}" ]]; then
		cp "${RUNTIME_DIR}/${binary}" "${PACKAGE_DIR}/"
	fi
done

cp -r "${RUNTIME_DIR}/conf/." "${PACKAGE_DIR}/conf/"
cp -r "${WEB_DIST_DIR}/." "${PACKAGE_DIR}/web/"
cp -r "${ROOT_DIR}/deploy/scripts/." "${PACKAGE_DIR}/scripts/"

build_and_package_redis

sed -i 's#Web_Root: ""#Web_Root: "./web"#' "${PACKAGE_DIR}/conf/Service_Setting.yml"

echo "[ci] package ready: ${PACKAGE_DIR}"
