#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
IMAGE_REPOSITORY="${IMAGE_REPOSITORY:-navcaster}"
IMAGE_PREFIX="${IMAGE_PREFIX:-team-dev}"
BASE_IMAGE="${BASE_IMAGE:-ubuntu:24.04}"
SOURCE="${SOURCE:-https://github.com/NavTool/NavCaster}"
REDIS_VERSION="${REDIS_VERSION:-8.6.3}"
USE_DOCKER_BUILDER="${USE_DOCKER_BUILDER:-auto}"
SKIP_WEB_BUILD=0
SKIP_PACKAGE_BUILD=0
ALLOW_DIRTY=0
PACKAGE_DIR_OVERRIDE=""
IMAGE_TAG_OVERRIDE=""
BUILDER_IMAGE="${BUILDER_IMAGE:-}"
BUILDER_CONTAINER="${BUILDER_CONTAINER:-}"
EXTRA_TAGS=()

usage() {
	cat <<'EOF'
Usage: deploy/scripts/build_runtime_image.sh [options]

Build a Linux NavCaster package and Docker runtime image with commit provenance.
On Linux hosts the package is built locally. On non-Linux hosts the script uses
the Ubuntu 24.04 Docker builder from deploy/docker/dockerfile.debian.

Options:
  --build-type <Release|Debug>  CMake build type. Defaults to BUILD_TYPE or Release.
  --package-dir <path>          Reuse an existing package directory relative to repo root.
  --image-tag <tag>             Override the primary image tag.
  --extra-tag <tag>             Add an extra Docker tag. May be repeated.
  --use-docker-builder          Build the Linux package inside a Docker builder.
  --no-docker-builder           Build the Linux package on the current host.
  --skip-web-build              Reuse existing web/dist.
  --skip-package-build          Reuse --package-dir or the computed release package.
  --allow-dirty                 Allow tracked working tree changes in provenance.
  -h, --help                    Show this help.

Environment:
  IMAGE_REPOSITORY=navcaster
  IMAGE_PREFIX=team-dev
  BASE_IMAGE=ubuntu:24.04
  SOURCE=https://github.com/NavTool/NavCaster
  REDIS_VERSION=8.6.3
  USE_DOCKER_BUILDER=auto|1|0
EOF
}

sanitize_tag_part() {
	printf '%s' "$1" | tr '/:@ ' '----' | tr -cd 'A-Za-z0-9_.-'
}

fail() {
	echo "[runtime-image] $*" >&2
	exit 1
}

host_path_for_docker_cp() {
	if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* || "$(uname -s)" == CYGWIN* ]]; then
		(cd "$1" && pwd -W)
	else
		(cd "$1" && pwd)
	fi
}

host_file_for_docker() {
	local file="$1"
	local dir
	local base
	dir="$(dirname "$file")"
	base="$(basename "$file")"
	printf '%s/%s' "$(host_path_for_docker_cp "$dir")" "$base"
}

docker_cli() {
	if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* || "$(uname -s)" == CYGWIN* ]]; then
		MSYS_NO_PATHCONV=1 docker "$@"
	else
		docker "$@"
	fi
}

git_repo() {
	git -c "safe.directory=$ROOT_DIR" -C "$ROOT_DIR" "$@"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--build-type)
			BUILD_TYPE="$2"
			shift 2
			;;
		--package-dir)
			PACKAGE_DIR_OVERRIDE="$2"
			SKIP_PACKAGE_BUILD=1
			shift 2
			;;
		--image-tag)
			IMAGE_TAG_OVERRIDE="$2"
			shift 2
			;;
		--extra-tag)
			EXTRA_TAGS+=("$2")
			shift 2
			;;
		--use-docker-builder)
			USE_DOCKER_BUILDER=1
			shift
			;;
		--no-docker-builder)
			USE_DOCKER_BUILDER=0
			shift
			;;
		--skip-web-build)
			SKIP_WEB_BUILD=1
			shift
			;;
		--skip-package-build)
			SKIP_PACKAGE_BUILD=1
			shift
			;;
		--allow-dirty)
			ALLOW_DIRTY=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "[runtime-image] unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

VCS_REF="$(git_repo rev-parse HEAD)"
SHORT_REF="$(git_repo rev-parse --short=12 HEAD)"
RAW_VERSION="$(git_repo describe --tags --always --dirty=-dirty)"
VERSION="$(sanitize_tag_part "$RAW_VERSION")"
CREATED="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

if ! git_repo diff --quiet || ! git_repo diff --cached --quiet; then
	if [[ "$ALLOW_DIRTY" != "1" ]]; then
		fail "tracked working tree changes would make image provenance ambiguous. Commit/stash changes first, or rerun with --allow-dirty for an explicit local test image."
	fi
	VERSION="$(sanitize_tag_part "${VERSION}-dirty")"
fi

PACKAGE_NAME="${PACKAGE_NAME:-NavCaster-${VERSION}-ubuntu-24.04-amd64}"
PACKAGE_DIR="${PACKAGE_DIR_OVERRIDE:-release/${PACKAGE_NAME}}"
PRIMARY_TAG="${IMAGE_TAG_OVERRIDE:-${IMAGE_REPOSITORY}:${IMAGE_PREFIX}-${SHORT_REF}}"
VERSION_TAG="${IMAGE_REPOSITORY}:${VERSION}-ubuntu-24.04-amd64"
BUILDER_IMAGE="${BUILDER_IMAGE:-navcaster-linux-builder:ubuntu-24.04-amd64}"
BUILDER_CONTAINER="${BUILDER_CONTAINER:-navcaster-linux-builder-${SHORT_REF}-$$}"

if [[ "$USE_DOCKER_BUILDER" == "auto" ]]; then
	if [[ "$(uname -s)" == Linux* ]]; then
		USE_DOCKER_BUILDER=0
	else
		USE_DOCKER_BUILDER=1
	fi
fi

echo "[runtime-image] root       : $ROOT_DIR"
echo "[runtime-image] revision   : $VCS_REF"
echo "[runtime-image] version    : $VERSION"
echo "[runtime-image] build type : $BUILD_TYPE"
echo "[runtime-image] package    : $PACKAGE_DIR"
echo "[runtime-image] image      : $PRIMARY_TAG"

if [[ "$SKIP_PACKAGE_BUILD" != "1" ]]; then
	if [[ "$SKIP_WEB_BUILD" != "1" ]]; then
		npm --prefix "$ROOT_DIR/web" ci
		npm --prefix "$ROOT_DIR/web" run build
		rm -rf "$ROOT_DIR/web/node_modules"
	fi

	if [[ "$USE_DOCKER_BUILDER" == "1" ]]; then
		command -v docker >/dev/null 2>&1 || fail "docker is required for non-Linux package builds"

		builder_context="$(host_path_for_docker_cp "$ROOT_DIR/deploy/docker")"
		builder_dockerfile="$(host_file_for_docker "$ROOT_DIR/deploy/docker/dockerfile.debian")"
		repo_context="$(host_path_for_docker_cp "$ROOT_DIR")"

		echo "[runtime-image] builder   : $BUILDER_IMAGE"
		docker_cli build \
			-f "$builder_dockerfile" \
			--build-arg "BASE_IMAGE=$BASE_IMAGE" \
			-t "$BUILDER_IMAGE" \
			"$builder_context"

		existing_container="$(docker_cli ps -a --filter "name=^/${BUILDER_CONTAINER}$" --format '{{.Names}}' | head -n 1)"
		if [[ "$existing_container" == "$BUILDER_CONTAINER" ]]; then
			fail "builder container already exists: $BUILDER_CONTAINER"
		fi

		cleanup_builder() {
			docker_cli rm -f "$BUILDER_CONTAINER" >/dev/null 2>&1 || true
		}
		trap cleanup_builder EXIT

		docker_cli run --name "$BUILDER_CONTAINER" -d "$BUILDER_IMAGE" >/dev/null
		docker_cli exec "$BUILDER_CONTAINER" mkdir -p /workspace
		docker_cli cp "${repo_context}/." "${BUILDER_CONTAINER}:/workspace"
		docker_cli exec -w /workspace "$BUILDER_CONTAINER" git config --global --add safe.directory /workspace || true
		docker_cli exec -w /workspace "$BUILDER_CONTAINER" chmod +x deploy/ci/build_in_linux.sh
		docker_cli exec \
			-e "BUILD_TYPE=$BUILD_TYPE" \
			-e "PACKAGE_NAME=$PACKAGE_NAME" \
			-e "REDIS_VERSION=$REDIS_VERSION" \
			-w /workspace \
			"$BUILDER_CONTAINER" \
			bash deploy/ci/build_in_linux.sh

		case "$PACKAGE_DIR" in
			release/*) rm -rf "$ROOT_DIR/$PACKAGE_DIR" ;;
			*) fail "refusing to overwrite package outside release/: $PACKAGE_DIR" ;;
		esac
		mkdir -p "$ROOT_DIR/release"
		release_context="$(host_path_for_docker_cp "$ROOT_DIR/release")"
		docker_cli cp "${BUILDER_CONTAINER}:/workspace/${PACKAGE_DIR}" "$release_context/"
	else
		BUILD_TYPE="$BUILD_TYPE" PACKAGE_NAME="$PACKAGE_NAME" REDIS_VERSION="$REDIS_VERSION" \
			bash "$ROOT_DIR/deploy/ci/build_in_linux.sh"
	fi
else
	if [[ ! -d "$ROOT_DIR/$PACKAGE_DIR" ]]; then
		fail "missing package directory: $ROOT_DIR/$PACKAGE_DIR"
	fi
fi

if [[ ! -d "$ROOT_DIR/$PACKAGE_DIR" ]]; then
	fail "package directory was not produced: $ROOT_DIR/$PACKAGE_DIR"
fi

cat >"$ROOT_DIR/$PACKAGE_DIR/IMAGE_PROVENANCE.txt" <<EOF
image=$PRIMARY_TAG
version=$VERSION
revision=$VCS_REF
source=$SOURCE
created=$CREATED
package=$PACKAGE_DIR
base_image=$BASE_IMAGE
EOF

docker_args=(
	"build"
	"-f" "$(host_file_for_docker "$ROOT_DIR/deploy/docker/Dockerfile.runtime")"
	"--build-arg" "BASE_IMAGE=$BASE_IMAGE"
	"--build-arg" "PACKAGE_DIR=$PACKAGE_DIR"
	"--build-arg" "VCS_REF=$VCS_REF"
	"--build-arg" "VERSION=$VERSION"
	"--build-arg" "SOURCE=$SOURCE"
	"--build-arg" "CREATED=$CREATED"
	"-t" "$PRIMARY_TAG"
	"-t" "$VERSION_TAG"
)

for tag in "${EXTRA_TAGS[@]}"; do
	docker_args+=("-t" "$tag")
done

docker_args+=("$(host_path_for_docker_cp "$ROOT_DIR")")

docker_cli "${docker_args[@]}"

echo "[runtime-image] inspect"
docker_cli image inspect "$PRIMARY_TAG" --format 'image={{.Id}} revision={{index .Config.Labels "org.opencontainers.image.revision"}} version={{index .Config.Labels "org.opencontainers.image.version"}} source={{index .Config.Labels "org.opencontainers.image.source"}} created={{index .Config.Labels "org.opencontainers.image.created"}}'
echo "NAVCASTER_IMAGE=$PRIMARY_TAG"
