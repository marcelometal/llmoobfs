#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Keeps the raw benchmark archives in one GitHub release, "bench-evidence", instead of one
# pre-release per campaign. The table in bench/evidence-release.md is the manifest: one row per
# archive with its size and SHA-256 (and, for the first campaigns, the pre-release it came from).
#
#   scripts/bench-evidence-release.sh [--apply] [--target REF] migrate
#       1. downloads every archive of the manifest from its original pre-release and checks
#          size and SHA-256 against the manifest (cached under build/bench-evidence/);
#       2. creates the release as a draft if it does not exist, and uploads missing archives;
#       3. checks size and digest of every uploaded archive through the API, publishes the
#          draft, downloads each archive again from its public URL and checks size and SHA-256;
#       4. only then deletes the original pre-releases. Their tags are kept.
#   scripts/bench-evidence-release.sh [--apply] add FILE...
#       uploads new campaign archives (each must have a row in the manifest), checks them as in
#       step 3 and refreshes the release notes from the manifest.
#
# Without --apply nothing is written to GitHub: the script runs the downloads and checks it can
# run, and prints each write it would make. Needs gh (authenticated), curl and sha256sum or
# shasum. REPO overrides the repository (default studiomoob/llmoobfs).
set -eu
cd "$(dirname "$0")/.."

REPO=${REPO:-studiomoob/llmoobfs}
TAG=bench-evidence
TITLE="Benchmark evidence (raw data, not a software release)"
NOTES=bench/evidence-release.md
WORK=build/bench-evidence
APPLY=0
TARGET=main

die() { echo "bench-evidence: $*" >&2; exit 1; }
say() { echo "bench-evidence: $*"; }
write() {   # write <description> <command...>: runs the command only with --apply
    desc=$1; shift
    if [ "$APPLY" -eq 1 ]; then say "$desc"; "$@"; else say "would $desc: $*"; fi
}
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
size() { wc -c < "$1" | tr -d ' '; }

manifest() {   # prints "archive bytes sha256 origin-tag" for each table row; "-" when no tag
    awk -F'|' '/^\| `/ && NF == 8 {
        for (i = 2; i <= 7; i++) { gsub(/[` ]/, "", $i) }
        if ($5 == "" || $5 == "—") $5 = "-"
        print $2, $6, $7, $5
    }' "$NOTES"
}
row() {   # row <archive>: the manifest line of one archive, or nothing
    manifest | awk -v n="$1" '$1 == n'
}
check_file() {   # check_file <path> <bytes> <sha256> <label>
    [ -f "$1" ] || die "$4: $1 is missing"
    got_size=$(size "$1"); got_sha=$(sha256 "$1")
    [ "$got_size" = "$2" ] || die "$4: $1 has $got_size bytes, manifest says $2"
    [ "$got_sha" = "$3" ] || die "$4: $1 has SHA-256 $got_sha, manifest says $3"
    say "$4: $(basename "$1") $2 bytes, SHA-256 matches"
}
release_exists() { gh release view "$TAG" -R "$REPO" --json tagName >/dev/null 2>&1; }
remote_asset() {   # remote_asset <archive>: prints "bytes sha256" as GitHub reports it, or nothing
    gh release view "$TAG" -R "$REPO" --json assets \
        -q ".assets[] | select(.name == \"$1\") | \"\(.size) \(.digest // \"\")\"" 2>/dev/null |
        sed 's/sha256://'
}
upload() {   # upload <path> <bytes> <sha256>: uploads unless an identical asset is there already
    name=$(basename "$1")
    have=$(remote_asset "$name")
    if [ -n "$have" ]; then
        [ "$have" = "$2 $3" ] || die "$name is already in $TAG with a different size or digest ($have); not replacing it"
        say "$name already in $TAG, identical"
        return 0
    fi
    write "upload $name to $TAG" gh release upload "$TAG" "$1" -R "$REPO"
}
verify_api() {   # verify_api <archive> <bytes> <sha256>
    have=$(remote_asset "$1")
    [ "$have" = "$2 $3" ] || die "after upload, GitHub reports '$have' for $1, expected '$2 $3'"
    say "API: $1 $2 bytes, digest matches"
}
verify_public() {   # verify_public <archive> <bytes> <sha256>: download back from the public URL
    mkdir -p "$WORK/verify"
    rm -f "$WORK/verify/$1"
    curl -fsSL -o "$WORK/verify/$1" "https://github.com/$REPO/releases/download/$TAG/$1" ||
        die "cannot download $1 from the public URL"
    check_file "$WORK/verify/$1" "$2" "$3" "public URL"
}
create_release() {
    if release_exists; then say "release $TAG exists"; return 0; fi
    write "create draft release $TAG (target $TARGET)" gh release create "$TAG" -R "$REPO" --draft \
        --prerelease --latest=false --target "$TARGET" --title "$TITLE" --notes-file "$NOTES"
}
publish_release() {
    draft=$(gh release view "$TAG" -R "$REPO" --json isDraft -q .isDraft 2>/dev/null || echo true)
    [ "$draft" = true ] || { say "release $TAG already published"; return 0; }
    write "publish $TAG" gh release edit "$TAG" -R "$REPO" --draft=false --prerelease --latest=false
}

migrate() {
    mkdir -p "$WORK/download"
    rows=$(manifest | awk '$4 != "-"')
    [ -n "$rows" ] || die "no row with an original tag in $NOTES"
    # 1. download from the original pre-releases and check against the manifest
    echo "$rows" | while read -r name bytes sha origin; do
        if [ ! -f "$WORK/download/$name" ] || [ "$(sha256 "$WORK/download/$name")" != "$sha" ]; then
            rm -f "$WORK/download/$name"
            gh release download "$origin" -R "$REPO" -p "$name" -D "$WORK/download" ||
                die "cannot download $name from $origin"
        fi
        check_file "$WORK/download/$name" "$bytes" "$sha" "$origin"
    done
    # 2. create the draft and upload
    create_release
    echo "$rows" | while read -r name bytes sha origin; do
        upload "$WORK/download/$name" "$bytes" "$sha"
    done
    if [ "$APPLY" -eq 0 ]; then
        echo "$rows" | while read -r name bytes sha origin; do
            write "delete the release $origin (the tag stays)" gh release delete "$origin" -R "$REPO" --yes
        done
        say "dry run finished; rerun with --apply to write to GitHub"
        return 0
    fi
    # 3. check through the API, publish, check through the public URL
    echo "$rows" | while read -r name bytes sha origin; do verify_api "$name" "$bytes" "$sha"; done
    publish_release
    echo "$rows" | while read -r name bytes sha origin; do verify_public "$name" "$bytes" "$sha"; done
    # 4. every archive is verified (any failure above exits): delete the old releases, keep tags
    echo "$rows" | while read -r name bytes sha origin; do
        if gh release view "$origin" -R "$REPO" --json tagName >/dev/null 2>&1; then
            write "delete the release $origin (the tag stays)" gh release delete "$origin" -R "$REPO" --yes
        fi
        gh api "repos/$REPO/git/ref/tags/$origin" --jq .ref >/dev/null || die "tag $origin is gone"
        say "tag $origin kept"
    done
    say "done: https://github.com/$REPO/releases/tag/$TAG"
}

expect() {   # expect <path>: sets name, bytes and sha from the manifest row of the archive
    line=$(row "$(basename "$1")")
    [ -n "$line" ] || die "$(basename "$1") has no row in $NOTES; add it to the table first"
    read -r name bytes sha origin <<EOF
$line
EOF
}

add() {
    [ $# -gt 0 ] || die "add needs at least one archive"
    for f in "$@"; do expect "$f"; check_file "$f" "$bytes" "$sha" "local"; done
    if [ "$APPLY" -eq 1 ]; then release_exists || die "release $TAG does not exist; run migrate first"; fi
    for f in "$@"; do expect "$f"; upload "$f" "$bytes" "$sha"; done
    if [ "$APPLY" -eq 0 ]; then
        write "refresh the notes of $TAG from $NOTES" gh release edit "$TAG" -R "$REPO" --notes-file "$NOTES"
        say "dry run finished; rerun with --apply to write to GitHub"
        return 0
    fi
    for f in "$@"; do expect "$f"; verify_api "$name" "$bytes" "$sha"; verify_public "$name" "$bytes" "$sha"; done
    write "refresh the notes of $TAG from $NOTES" gh release edit "$TAG" -R "$REPO" --notes-file "$NOTES"
}

while [ $# -gt 0 ]; do
    case $1 in
        --apply) APPLY=1 ;;
        --dry-run) APPLY=0 ;;
        --target) shift; TARGET=${1:?--target needs a branch or commit} ;;
        migrate) shift; [ $# -eq 0 ] || die "migrate takes no arguments"; MODE=migrate; break ;;
        add) shift; MODE=add; break ;;
        -h|--help) sed -n '3,22p' "$0"; exit 0 ;;
        *) die "unknown argument $1 (see --help)" ;;
    esac
    shift
done
: "${MODE:=migrate}"
command -v gh >/dev/null 2>&1 || die "gh is required"
command -v curl >/dev/null 2>&1 || die "curl is required"
[ -f "$NOTES" ] || die "$NOTES is missing"
if [ "$APPLY" -eq 1 ]; then say "mode: apply (writes to $REPO)"; else say "mode: dry run (no writes)"; fi
if [ "$MODE" = migrate ]; then migrate; else add "$@"; fi
