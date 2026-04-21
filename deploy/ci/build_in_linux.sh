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

BINARIES=(
	CasterService
	reg_check
	ntrip_client_sim_0.0.2
	ntrip_server_sim_0.0.2
	strsvr_mult
	rtklib_rnx2rtkp
	rtklib_rtkconv
)

echo "[ci] build type: ${BUILD_TYPE}"
echo "[ci] build dir : ${BUILD_DIR}"
echo "[ci] package dir: ${PACKAGE_DIR}"

rm -rf "${BUILD_DIR}" "${PACKAGE_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

if [[ ! -d "${RUNTIME_DIR}/conf" ]]; then
	echo "[ci] missing runtime config directory: ${RUNTIME_DIR}/conf" >&2
	exit 1
fi

if [[ ! -f "${WEB_DIST_DIR}/index.html" ]]; then
	echo "[ci] missing web build output: ${WEB_DIST_DIR}/index.html" >&2
	echo "[ci] run npm ci && npm run build in web/ before packaging" >&2
	exit 1
fi

mkdir -p "${PACKAGE_DIR}/conf" "${PACKAGE_DIR}/logs" "${PACKAGE_DIR}/web" "${PACKAGE_DIR}/scripts"

for binary in "${BINARIES[@]}"; do
	if [[ -f "${RUNTIME_DIR}/${binary}" ]]; then
		cp "${RUNTIME_DIR}/${binary}" "${PACKAGE_DIR}/"
	fi
done

cp -r "${RUNTIME_DIR}/conf/." "${PACKAGE_DIR}/conf/"
cp -r "${WEB_DIST_DIR}/." "${PACKAGE_DIR}/web/"
cp -r "${ROOT_DIR}/deploy/scripts/." "${PACKAGE_DIR}/scripts/"

rm -f "${PACKAGE_DIR}/scripts/systemd/install_redis_service.sh"
rm -f "${PACKAGE_DIR}/scripts/systemd/uninstall_redis_service.sh"
rm -f "${PACKAGE_DIR}/scripts/supervisor/install_redis_service.sh"
rm -f "${PACKAGE_DIR}/scripts/supervisor/uninstall_redis_service.sh"

sed -i 's#Web_Root: ""#Web_Root: "./web"#' "${PACKAGE_DIR}/conf/Service_Setting.yml"

echo "[ci] package ready: ${PACKAGE_DIR}"
