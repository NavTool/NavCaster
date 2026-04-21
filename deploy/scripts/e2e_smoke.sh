#!/usr/bin/env bash
# NavCaster V3 smoke test:
# 依次访问关键 API，任何一步失败立即退出非零。
# 依赖 curl + jq。可通过 BASE / USER / PASS 覆盖默认值。
set -euo pipefail

BASE="${BASE:-http://127.0.0.1:8080}"
USER="${USER:-admin}"
PASS="${PASS:-admin}"

say() { printf "\033[36m[smoke]\033[0m %s\n" "$*"; }
fail() { printf "\033[31m[smoke FAIL]\033[0m %s\n" "$*" >&2; exit 1; }

require() { command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"; }
require curl
require jq

say "BASE=$BASE USER=$USER"

say "1) /api/status/health"
curl -fsS "$BASE/api/status/health" >/dev/null || fail "health check failed"

say "2) /api/auth/login"
TOKEN=$(curl -fsS -X POST -H 'Content-Type: application/json' \
    -d "$(jq -nc --arg u "$USER" --arg p "$PASS" '{username:$u, password:$p}')" \
    "$BASE/api/auth/login" | jq -r '.token')
[[ -n "$TOKEN" && "$TOKEN" != "null" ]] || fail "login did not return token"

AUTH=(-H "Authorization: Bearer $TOKEN")

say "3) /api/status"
curl -fsS "${AUTH[@]}" "$BASE/api/status" | jq -e '.cpu_percent != null and .node_id != null' >/dev/null \
    || fail "status response missing fields"

say "4) /api/monitor/cluster"
curl -fsS "${AUTH[@]}" "$BASE/api/monitor/cluster" | jq -e '.nodes | length >= 0' >/dev/null \
    || fail "cluster monitor failed"

say "5) /api/audit?limit=1"
curl -fsS "${AUTH[@]}" "$BASE/api/audit?limit=1" | jq -e '.items != null' >/dev/null \
    || fail "audit failed"

say "6) /api/logs/ring?n=1"
curl -fsS "${AUTH[@]}" "$BASE/api/logs/ring?n=1" | jq -e '.items != null' >/dev/null \
    || fail "ring log failed"

say "7) /api/monitor/redis/history?range=1h"
curl -fsS "${AUTH[@]}" "$BASE/api/monitor/redis/history?range=1h" | jq -e '.items != null' >/dev/null \
    || fail "redis history failed"

say "8) /api/system/events?limit=1"
curl -fsS "${AUTH[@]}" "$BASE/api/system/events?limit=1" | jq -e '.items != null' >/dev/null \
    || fail "system events failed"

printf "\033[32m[smoke OK]\033[0m all V3 endpoints reachable\n"
