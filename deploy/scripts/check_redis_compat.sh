#!/usr/bin/env bash
# Verify that the target Redis supports NavCaster runtime commands.
set -euo pipefail

REDIS_CLI="${REDIS_CLI:-redis-cli}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"
REDIS_USER="${REDIS_USER:-}"
REDIS_PASSWORD="${REDIS_PASSWORD:-}"
REDIS_DB="${REDIS_DB:-0}"
MIN_REDIS_VERSION="8.4.0"

say() { printf '[redis-compat] %s\n' "$*"; }
fail() { printf '[redis-compat FAIL] %s\n' "$*" >&2; exit 1; }

if ! command -v "${REDIS_CLI}" >/dev/null 2>&1; then
    fail "missing redis-cli; set REDIS_CLI=/path/to/redis-cli"
fi

REDIS_ARGS=(-h "${REDIS_HOST}" -p "${REDIS_PORT}" -n "${REDIS_DB}" --raw)
if [[ -n "${REDIS_USER}" ]]; then
    REDIS_ARGS+=(--user "${REDIS_USER}")
fi
if [[ -n "${REDIS_PASSWORD}" ]]; then
    REDIS_ARGS+=(--no-auth-warning -a "${REDIS_PASSWORD}")
fi

redis() {
    "${REDIS_CLI}" "${REDIS_ARGS[@]}" "$@"
}

version_ge() {
    local left="${1%%-*}"
    local right="${2%%-*}"
    local IFS=.
    local -a a=(${left}) b=(${right})
    local i
    for ((i=${#a[@]}; i<3; i++)); do a[i]=0; done
    for ((i=${#b[@]}; i<3; i++)); do b[i]=0; done
    for i in 0 1 2; do
        local ai="${a[i]//[^0-9]/}"
        local bi="${b[i]//[^0-9]/}"
        ai="${ai:-0}"
        bi="${bi:-0}"
        if ((10#${ai} > 10#${bi})); then return 0; fi
        if ((10#${ai} < 10#${bi})); then return 1; fi
    done
    return 0
}

KEY_SUFFIX="$$-$(date +%s)"
HASH_KEY="NC:COMPAT:${KEY_SUFFIX}:HASH"
LEASE_KEY="NC:COMPAT:${KEY_SUFFIX}:LEASE"

cleanup() {
    redis DEL "${HASH_KEY}" "${LEASE_KEY}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

say "target=${REDIS_HOST}:${REDIS_PORT} db=${REDIS_DB}"

PING="$(redis PING 2>&1)" || fail "PING failed: ${PING}"
[[ "${PING}" == "PONG" ]] || fail "unexpected PING response: ${PING}"

INFO="$(redis INFO server 2>&1)" || fail "INFO server failed: ${INFO}"
VERSION="$(printf '%s\n' "${INFO}" | awk -F: '/^redis_version:/ {gsub(/\r/, "", $2); print $2; exit}')"
[[ -n "${VERSION}" ]] || fail "INFO server did not expose redis_version"
say "redis_version=${VERSION}"

if ! version_ge "${VERSION}" "${MIN_REDIS_VERSION}"; then
    fail "Redis ${VERSION} is below NavCaster minimum ${MIN_REDIS_VERSION}"
fi

OUT="$(redis HSETEX "${HASH_KEY}" EX 30 FIELDS 1 field value 2>&1)" \
    || fail "HSETEX unsupported or failed: ${OUT}"
[[ "${OUT}" == "1" || "${OUT}" == "OK" ]] || fail "unexpected HSETEX response: ${OUT}"

OUT="$(redis HEXPIRE "${HASH_KEY}" 30 FIELDS 1 field 2>&1)" \
    || fail "HEXPIRE unsupported or failed: ${OUT}"
[[ "${OUT}" == "1" || "${OUT}" == *$'\n1' ]] || fail "unexpected HEXPIRE response: ${OUT}"

OUT="$(redis SET "${LEASE_KEY}" node-a NX EX 30 2>&1)" \
    || fail "SET NX EX failed: ${OUT}"
[[ "${OUT}" == "OK" ]] || fail "unexpected SET NX EX response: ${OUT}"

OUT="$(redis SET "${LEASE_KEY}" node-b IFEQ node-a EX 30 2>&1)" \
    || fail "SET IFEQ EX unsupported or failed: ${OUT}"
[[ "${OUT}" == "OK" ]] || fail "unexpected SET IFEQ EX response: ${OUT}"

say "PASS HSETEX, HEXPIRE, SET IFEQ EX"
