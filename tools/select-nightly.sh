#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"
: "${GITHUB_OUTPUT:?This script runs in GitHub Actions.}"

if [ -n "${REQUESTED_COMMIT:-}" ]; then
    case "$REQUESTED_COMMIT" in
        *[!0-9a-fA-F]*) printf '%s\n' 'Use a hexadecimal commit SHA from develop.' >&2; exit 1 ;;
    esac
    if [ "${#REQUESTED_COMMIT}" -lt 7 ] || [ "${#REQUESTED_COMMIT}" -gt 40 ]; then
        printf '%s\n' 'Use a 7-to-40-character commit SHA from develop.' >&2
        exit 1
    fi
    commit=$(git rev-parse --verify --end-of-options "$REQUESTED_COMMIT^{commit}")
    git merge-base --is-ancestor "$commit" origin/develop
    git checkout --detach "$commit"
fi
commit=$(git rev-parse HEAD)
build_date=$(TZ=CST-8 date +%Y%m%d)
printf 'date=%s\ncommit=%s\n' "$build_date" "$commit" >> "$GITHUB_OUTPUT"
