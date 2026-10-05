#!/bin/sh

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

parse_release_arguments() {
    channel=Release
    build_date=
    check_only=false
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --channel)
                [ "$#" -ge 2 ] || fail '--channel requires Release or Nightly.'
                channel=$2
                shift 2
                ;;
            --date)
                [ "$#" -ge 2 ] || fail '--date requires YYYYMMDD.'
                build_date=$2
                shift 2
                ;;
            --check-only) check_only=true; shift ;;
            *) fail "Unknown argument: $1" ;;
        esac
    done
    case "$channel" in Release|Nightly) ;; *) fail 'Unknown release channel.' ;; esac
}

load_release_info() {
    mkdir -p build
    pwsh -NoProfile -File ./tools/release-info.ps1 -Channel "$channel" -BuildDate "$build_date" > build/release-info.raw
    tr -d '\r' < build/release-info.raw > build/release-info.txt
    IFS='|' read -r version channel tag source_commit build_date player_package < build/release-info.txt
    [ -n "$player_package" ] || fail 'Release metadata is incomplete.'
}
