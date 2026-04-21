#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"

"$SCRIPT_DIR/uninstall_caster_service.sh"
