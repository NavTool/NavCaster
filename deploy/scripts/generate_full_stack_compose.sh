#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEMPLATE_DIR="${ROOT_DIR}/deploy/docker/full-stack"
OUTPUT_DIR="${ROOT_DIR}/dist/docker"
PACKAGE_DIR=""
NO_PACKAGE=0

usage() {
	cat <<'EOF'
Usage: deploy/scripts/generate_full_stack_compose.sh [options]

Generate a self-contained Docker Compose deployment directory at dist/docker.

Options:
  --package-dir <path>  Copy an existing NavCaster package into dist/docker/package.
                        The path may be absolute or relative to the repository root.
  --output-dir <path>   Output directory. Defaults to dist/docker.
  --no-package          Generate compose templates without copying package artifacts.
  -h, --help            Show this help.

Typical flow:
  bash deploy/scripts/package_linux.sh
  bash deploy/scripts/generate_full_stack_compose.sh --package-dir dist/<PackageName>
  cd dist/docker
  cp .env.example .env
  docker compose --env-file .env build
  docker compose --env-file .env up -d
EOF
}

fail() {
	echo "[full-stack-compose] $*" >&2
	exit 1
}

repo_path() {
	case "$1" in
		/*) printf '%s' "$1" ;;
		*) printf '%s/%s' "$ROOT_DIR" "$1" ;;
	esac
}

detect_package_dir() {
	if [[ -f "${ROOT_DIR}/dist/package-metadata.env" ]]; then
		# shellcheck disable=SC1091
		source "${ROOT_DIR}/dist/package-metadata.env"
		if [[ -n "${PACKAGE_NAME:-}" && -d "${ROOT_DIR}/dist/${PACKAGE_NAME}" ]]; then
			printf '%s/dist/%s' "$ROOT_DIR" "$PACKAGE_NAME"
			return
		fi
	fi

	local latest=""
	while IFS= read -r candidate; do
		latest="$candidate"
	done < <(find "${ROOT_DIR}/dist" -maxdepth 1 -mindepth 1 -type d -name 'NavCaster-*' -printf '%T@ %p\n' 2>/dev/null | sort -n | awk '{print $2}')

	if [[ -n "$latest" ]]; then
		printf '%s' "$latest"
	fi
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--package-dir)
			PACKAGE_DIR="$(repo_path "$2")"
			shift 2
			;;
		--output-dir)
			OUTPUT_DIR="$(repo_path "$2")"
			shift 2
			;;
		--no-package)
			NO_PACKAGE=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "[full-stack-compose] unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

[[ -d "$TEMPLATE_DIR" ]] || fail "missing template directory: $TEMPLATE_DIR"

if [[ "$NO_PACKAGE" != "1" && -z "$PACKAGE_DIR" ]]; then
	PACKAGE_DIR="$(detect_package_dir || true)"
fi

mkdir -p "$OUTPUT_DIR"
cp "${TEMPLATE_DIR}/docker-compose.yml" "$OUTPUT_DIR/docker-compose.yml"
cp "${TEMPLATE_DIR}/Dockerfile.navcaster" "$OUTPUT_DIR/Dockerfile.navcaster"
cp "${TEMPLATE_DIR}/Dockerfile.web" "$OUTPUT_DIR/Dockerfile.web"
cp "${TEMPLATE_DIR}/nginx.conf" "$OUTPUT_DIR/nginx.conf"
cp "${TEMPLATE_DIR}/README.md" "$OUTPUT_DIR/README.md"
cp "${TEMPLATE_DIR}/.env.example" "$OUTPUT_DIR/.env.example"
cp "${TEMPLATE_DIR}/.dockerignore" "$OUTPUT_DIR/.dockerignore"

mkdir -p \
	"$OUTPUT_DIR/data/postgres" \
	"$OUTPUT_DIR/data/redis" \
	"$OUTPUT_DIR/data/agents/agent-1" \
	"$OUTPUT_DIR/logs/admin" \
	"$OUTPUT_DIR/logs/web" \
	"$OUTPUT_DIR/logs/agent-1"

if [[ "$NO_PACKAGE" == "1" ]]; then
	echo "[full-stack-compose] generated templates without package: $OUTPUT_DIR"
	echo "[full-stack-compose] rerun with --package-dir dist/<PackageName> before docker compose build"
	exit 0
fi

[[ -n "$PACKAGE_DIR" ]] || fail "package directory was not found. Run deploy/scripts/package_linux.sh first or pass --package-dir."
[[ -d "$PACKAGE_DIR" ]] || fail "package directory does not exist: $PACKAGE_DIR"

for required in \
	"bin/navcaster-admin" \
	"bin/navcaster-agent" \
	"bin/navcaster-caster" \
	"web/index.html" \
	"app/admin/migrations/0001_v2_adminservice_foundation.sql"; do
	if [[ ! -e "${PACKAGE_DIR}/${required}" ]]; then
		fail "package is missing required item: ${required}"
	fi
done

rm -rf "$OUTPUT_DIR/package"
mkdir -p "$OUTPUT_DIR/package"
cp -a "${PACKAGE_DIR}/." "$OUTPUT_DIR/package/"

echo "[full-stack-compose] generated deploy directory: $OUTPUT_DIR"
echo "[full-stack-compose] package copied from: $PACKAGE_DIR"
