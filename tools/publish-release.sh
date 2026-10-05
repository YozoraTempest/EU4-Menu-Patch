#!/bin/sh
set -eu

repo_root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"
. ./tools/release-common.sh
parse_release_arguments "$@"
load_release_info
repo=${GITHUB_REPOSITORY:-YozoraTempest/EU4-Menu-Patch}
changes=$(git status --porcelain)
[ -z "$changes" ] || fail 'Commit source changes before publishing.'
pwsh -NoProfile -File ./tools/release-info.ps1 -Channel "$channel" -BuildDate "$build_date" -WriteNotes

github_value() {
    if gh api "repos/$repo/$1" --jq "$2" > build/github-value.txt 2> build/github-error.txt; then
        tr -d '\r' < build/github-value.txt
    elif grep -q '(HTTP 404)' build/github-error.txt; then
        return 0
    else
        cat build/github-error.txt >&2
        exit 1
    fi
}

tag_object=$(github_value "git/ref/tags/$tag" '.object.type + "|" + .object.sha')
tag_commit=
if [ -n "$tag_object" ]; then
    object_type=$(printf '%s' "$tag_object" | cut -d '|' -f 1)
    object_sha=$(printf '%s' "$tag_object" | cut -d '|' -f 2)
    while [ "$object_type" = tag ]; do
        tag_object=$(github_value "git/tags/$object_sha" '.object.type + "|" + .object.sha')
        object_type=$(printf '%s' "$tag_object" | cut -d '|' -f 1)
        object_sha=$(printf '%s' "$tag_object" | cut -d '|' -f 2)
    done
    [ "$object_type" = commit ] && [ "$object_sha" = "$source_commit" ] ||
        fail 'Existing tag points to a different source commit.'
    tag_commit=$object_sha
fi

find_release_id() {
    gh api --paginate "repos/$repo/releases?per_page=100" \
        --jq ".[] | select(.tag_name == \"$tag\") | .id"
}

release_id=$(find_release_id)
release_state=
if [ -n "$release_id" ]; then
    release_state=$(github_value "releases/$release_id" '[.draft, .prerelease, (.assets | map(.name) | sort | join(",")), .target_commitish] | map(tostring) | join("|")')
fi
expected_assets="$player_package,SHA256SUMS.txt"
if [ -n "$release_state" ]; then
    release_draft=$(printf '%s' "$release_state" | cut -d '|' -f 1)
    release_prerelease=$(printf '%s' "$release_state" | cut -d '|' -f 2)
    release_assets=$(printf '%s' "$release_state" | cut -d '|' -f 3)
    release_target=$(printf '%s' "$release_state" | cut -d '|' -f 4)
    if [ "$release_draft" = true ] && [ -z "$tag_commit" ]; then
        [ "$release_target" = "$source_commit" ] || fail 'Draft release targets a different source commit.'
    fi
    if [ "$release_draft" = false ]; then
        [ "$tag_commit" = "$source_commit" ] && [ "$release_prerelease" = true ] &&
            [ "$release_assets" = "$expected_assets" ] || fail 'Published release has a different channel or asset list.'
        if [ -n "${GITHUB_OUTPUT:-}" ]; then printf 'skip=true\n' >> "$GITHUB_OUTPUT"; fi
        printf 'Already published: https://github.com/%s/releases/tag/%s\n' "$repo" "$tag"
        exit 0
    fi
fi

if [ "$channel" = Release ]; then
    gh api --paginate "repos/$repo/releases?per_page=100" \
        --jq '.[] | select(.draft == false) | .tag_name | select(test("^v[0-9]+\\.[0-9]+\\.[0-9]+-experimental$"))' \
        > build/published-tags.raw
    tr -d '\r' < build/published-tags.raw > build/published-tags.txt
    while IFS= read -r previous_tag; do
        [ -n "$previous_tag" ] || continue
        previous_version=${previous_tag#v}
        previous_version=${previous_version%-experimental}
        if ! awk -v current="$version" -v previous="$previous_version" 'BEGIN {
            split(current, c, "."); split(previous, p, ".");
            for (i = 1; i <= 3; i++) {
                if (c[i] + 0 > p[i] + 0) exit 0;
                if (c[i] + 0 < p[i] + 0) exit 1;
            }
            exit 1;
        }'; then
            fail "Release version must increase beyond $previous_tag."
        fi
    done < build/published-tags.txt
fi
if [ -n "${GITHUB_OUTPUT:-}" ]; then printf 'skip=false\n' >> "$GITHUB_OUTPUT"; fi
[ "$check_only" = false ] || exit 0

pwsh -NoProfile -File ./tools/test-package.ps1 -Channel "$channel" -BuildDate "$build_date"
if [ -z "$release_state" ]; then
    release_id=$(gh api --method POST "repos/$repo/releases" \
        -f "tag_name=$tag" -f "target_commitish=$source_commit" \
        -F draft=true -F prerelease=true -f make_latest=false \
        -f "name=EU4 Menu Patch $tag" -F body=@build/release-notes.md --jq '.id')
    : > build/existing-assets.txt
else
    github_value "releases/$release_id" '.assets[] | .name + "|" + (.id | tostring)' > build/existing-assets.txt
    while IFS='|' read -r asset_name asset_id; do
        case "$asset_name" in
            "$player_package"|SHA256SUMS.txt) ;;
            *) fail "Unexpected draft asset: $asset_name" ;;
        esac
    done < build/existing-assets.txt
    gh api --method PATCH "repos/$repo/releases/$release_id" \
        -F prerelease=true -f make_latest=false -f "name=EU4 Menu Patch $tag" \
        -F body=@build/release-notes.md --silent
fi

while IFS='|' read -r asset_name asset_id; do
    gh api --method DELETE "repos/$repo/releases/assets/$asset_id" --silent
done < build/existing-assets.txt
: > build/uploaded-assets.txt
for asset_name in "$player_package" SHA256SUMS.txt; do
    case "$asset_name" in *.zip) content_type=application/zip ;; *) content_type=text/plain ;; esac
    gh api --method POST "https://uploads.github.com/repos/$repo/releases/$release_id/assets?name=$asset_name" \
        -H "Content-Type: $content_type" --input "dist/$asset_name" \
        --jq '.name + "|" + (.digest // "")' > build/uploaded-asset.raw
    tr -d '\r' < build/uploaded-asset.raw > build/uploaded-asset.txt
    IFS='|' read -r uploaded_name uploaded_digest < build/uploaded-asset.txt
    asset_hash=$(sha256sum "dist/$asset_name")
    asset_hash=${asset_hash%% *}
    [ "$uploaded_name" = "$asset_name" ] && [ "$uploaded_digest" = "sha256:$asset_hash" ] ||
        fail "Uploaded asset digest mismatch: $asset_name"
    cat build/uploaded-asset.txt >> build/uploaded-assets.txt
done

gh api --method PATCH "repos/$repo/releases/$release_id" \
    -F draft=false -F prerelease=true -f make_latest=false --silent
url="https://github.com/$repo/releases/tag/$tag"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    printf '[%s](%s)\n' "$tag" "$url" >> "$GITHUB_STEP_SUMMARY"
fi
printf '%s\n' "$url"
