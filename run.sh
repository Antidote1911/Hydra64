#!/usr/bin/env bash
# Builds (incrementally) then starts Hydra64.
# Arguments are passed on to the application, for example:
#   ./run.sh tests/data/classics.pgn --page analyse
#   ./run.sh --help
# BUILD_TYPE=Debug variable to start the debug version.
set -euo pipefail
cd "$(dirname "$0")"

type=${BUILD_TYPE:-Release}
dir=build
[[ ${type,,} == debug ]] && dir=build-debug

./build.sh "${type,,}" >/dev/null || ./build.sh "${type,,}"
exec "./$dir/hydra64" "$@"
