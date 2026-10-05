#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"
. ./tools/release-common.sh
parse_release_arguments "$@"
[ "$check_only" = false ] || fail '--check-only is only used by publish-release.sh.'
load_release_info

pwsh -NoProfile -File ./tools/build.ps1 -Channel "$channel" -BuildDate "$build_date"
pwsh -NoProfile -File ./tools/test-guards.ps1 -Channel "$channel" -BuildDate "$build_date"
pwsh -NoProfile -File ./tools/package.ps1 -Channel "$channel" -BuildDate "$build_date"
pwsh -NoProfile -File ./tests/test-pipeline.ps1 -Channel "$channel" -BuildDate "$build_date"
