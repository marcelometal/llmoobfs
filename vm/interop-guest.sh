#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Interoperability test: common xattr-aware tools through a layer mount over ext4
# (getfattr/setfattr, fselect, cshatag, Recoll, KDE KFileMetaData). Runs inside a virtme-ng
# guest booted by .github/workflows/interop.yml; the runner's root filesystem is the guest's,
# so the tools installed on the runner are found here (journal 0140).
#
# Every command is printed with "+", then its output and "[exit N]". Deterministic facts are
# checked: each prints "INTEROP_OK: <step>" or "INTEROP_CHECK_FAIL: <step>". What generic
# tools show of the typed encoding (the type byte, raw integers) is recorded on
# "INTEROP_OBSERVED:" lines and never judged. The last line is INTEROP_PASS, or
# "INTEROP_FAIL: <steps>" naming every failed step ("setup (...)" when the test could not
# start). fselect, cshatag and Recoll are required; the KFileMetaData helper is optional
# ("INTEROP_SKIP: kfmd" when it was not built).
# Usage (inside the guest, as root): sh vm/interop-guest.sh
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/build/interop
BIN=$OUT/bin   # moobfs-attr, moobfs_layer.ko, fselect, cshatag and kfmd when it was built
LOWER=/tmp/moob-lower LAYER=/tmp/moob-layer
# the lower image: virtio-blk with virtme-ng 1.41 (CI), virtio-scsi with older releases
DEV=${DEV:-/dev/vda}
[ -b "$DEV" ] || DEV=/dev/sda
PATH=$BIN:$PATH
HOME=/tmp/home
LC_ALL=C
export PATH HOME LC_ALL
mkdir -p "$HOME"
. "$REPO/vm/kdiag.sh"

FAILED=
section() { echo; echo "=== INTEROP: $* ==="; }
run() { printf '+'; printf ' %s' "$@"; echo; "$@" 2>&1; echo "[exit $?]"; }
have() { command -v "$1" > /dev/null 2>&1; }
ok() { echo "INTEROP_OK: $1"; }
bad() { echo "INTEROP_CHECK_FAIL: $1${2:+ ($2)}"; FAILED="$FAILED $1"; }
# check STEP CMD...: the step passes when CMD exits 0
check() { s=$1; shift; if "$@"; then ok "$s"; else bad "$s"; fi; }
setup_fail() {
    echo "INTEROP_SETUP_FAIL: $*"
    dmesg | tail -40
    echo "INTEROP_FAIL: setup ($*)"
    exit 1
}
# xval FILE NAME: the attribute's value as getfattr prints it in hex ("0x..."), empty if absent
xval() { getfattr --absolute-names -n "$2" -e hex "$1" 2> /dev/null | sed -n "s/^$2=//p"; }
# xdump FILE: every attribute of FILE in hex, without the "# file:" header
xdump() { getfattr --absolute-names -d -m - -e hex "$1" 2> /dev/null | sed '/^# file:/d'; }
# same_hex FILE...: each file has the same attributes and bytes through the layer and below
same_hex() {
    for f in "$@"; do
        xdump "$D/$f" > /tmp/x.layer
        xdump "$L/$f" > /tmp/x.lower
        if ! [ -s /tmp/x.lower ] || ! cmp -s /tmp/x.layer /tmp/x.lower; then
            echo "$f differs (< layer, > lower):"
            diff /tmp/x.layer /tmp/x.lower
            return 1
        fi
    done
}
# einval CMD...: CMD fails with "Invalid argument" (setfattr prints strerror)
einval() { o=$("$@" 2>&1); r=$?; echo "$o"; [ "$r" -ne 0 ] && printf '%s\n' "$o" | grep -q 'Invalid argument'; }
# rows CMD...: CMD's output lines, sorted, on one line separated by spaces
rows() { "$@" 2> /dev/null | sort | tr '\n' ' ' | sed 's/ $//'; }
# hex TEXT: TEXT as getfattr -e hex prints it
hex() { printf '%s' "$1" | od -An -tx1 | tr -d ' \n' | sed 's/^/0x/'; }
# bytes CMD...: CMD's output as hex bytes on one line
bytes() { "$@" 2> /dev/null | od -An -tx1 | tr -s ' \n' ' '; }

section setup
uname -r
mkdir -p "$LOWER" "$LAYER"
mount -t ext4 "$DEV" "$LOWER" || setup_fail "lower mount"
insmod "$BIN/moobfs_layer.ko" || setup_fail insmod
mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || setup_fail "layer mount"
grep -E 'moobfs_layer| /tmp/moob-lower ' /proc/mounts
for t in getfattr setfattr moobfs-attr fselect cshatag recollindex recollq; do
    have "$t" || setup_fail "missing $t"
done
D=$LAYER/notes L=$LOWER/notes
mkdir "$D" || setup_fail mkdir
printf 'Decision: keep ext4 as the lower filesystem.\n' > "$D/a.txt"
printf '# Meeting note\nThe tags of this note come from Dolphin.\n' > "$D/b.md"
printf 'Draft text with plain tags only.\n' > "$D/c.txt"

section versions
run getfattr --version
run fselect --version
run sh -c 'cshatag -h 2>&1 | head -3'
run sh -c 'dpkg-query -W -f "\${Package} \${Version}\n" attr recollcmd libkf5filemetadata-dev e2fsprogs 2>/dev/null'

section "typed attributes written with moobfs-attr (a.txt, b.md)"
for a in "kind string decision" "score int64 42" "weight double 0.75" "pinned bool true" \
    "created time 1760000000" "blob raw deadbeef"; do
    # shellcheck disable=SC2086 # the words of $a are the name, type and value
    moobfs-attr set "$D/a.txt" $a || setup_fail "moobfs-attr set $a"
done
moobfs-attr set "$D/b.md" kind string note || setup_fail "moobfs-attr set kind on b.md"
run moobfs-attr list "$D/a.txt"

section "desktop attributes written with setfattr (b.md, c.txt)"
setfattr -n user.xdg.tags -v "project,moobfs" "$D/b.md" &&
    setfattr -n user.xdg.comment -v "reviewed on Monday" "$D/b.md" &&
    setfattr -n user.baloo.rating -v 8 "$D/b.md" &&
    setfattr -n user.xdg.tags -v draft "$D/c.txt" || setup_fail "setfattr user.xdg.*"

section "getfattr: text and hex, through the layer and on the lower"
run getfattr -d -m - "$D/a.txt" "$D/b.md" "$D/c.txt"
run getfattr -d -m - -e hex "$D/a.txt" "$D/b.md"
run getfattr -d -m - -e hex "$L/a.txt" "$L/b.md"
check hex-layer-equals-lower same_hex a.txt b.md c.txt
echo "INTEROP_OBSERVED: getfattr hex user.moob.kind of a.txt = $(xval "$D/a.txt" user.moob.kind) (type byte 01, then \"decision\")"
echo "INTEROP_OBSERVED: getfattr hex user.moob.score of a.txt = $(xval "$D/a.txt" user.moob.score) (type byte 02, then 42 as 8 little-endian bytes)"
echo "INTEROP_OBSERVED: getfattr text user.moob.kind of b.md: $(getfattr --absolute-names -n user.moob.kind "$D/b.md" 2>&1 | sed -n 's/^user.moob.kind=//p')"

section "setfattr: a plain-text value under user.moob.* is refused with EINVAL"
e=0
einval setfattr -n user.moob.x -v hello "$D/c.txt" || e=1
einval setfattr -n user.moob.x -v "" "$D/c.txt" || e=1
[ -z "$(xval "$L/c.txt" user.moob.x)" ] || e=1
if [ "$e" -eq 0 ]; then ok einval-plain-moob; else bad einval-plain-moob; fi
echo "a value written by setfattr in hex with the string type byte first is accepted:"
run setfattr -n user.moob.x -v 0x0168656c6c6f "$D/c.txt"
o=$(moobfs-attr get "$D/c.txt" x 2>&1)
echo "$o"
if [ "$o" = "$(printf 'x\tstring\thello')" ] && [ "$(xval "$L/c.txt" user.moob.x)" = 0x0168656c6c6f ]; then
    ok hex-typed-write
else
    bad hex-typed-write
fi
setfattr -x user.moob.x "$D/c.txt"

section "Dolphin-style user.xdg.tags round trip through the layer"
e=0
setfattr -n user.xdg.tags -v "alpha,beta" "$D/c.txt" || e=1
[ "$(xval "$D/c.txt" user.xdg.tags)" = "$(hex alpha,beta)" ] || e=1
[ "$(xval "$L/c.txt" user.xdg.tags)" = "$(hex alpha,beta)" ] || e=1
setfattr -n user.xdg.tags -v "alpha,beta,gamma" "$D/c.txt" || e=1
[ "$(xval "$D/c.txt" user.xdg.tags)" = "$(hex alpha,beta,gamma)" ] || e=1
[ "$(xval "$L/c.txt" user.xdg.tags)" = "$(hex alpha,beta,gamma)" ] || e=1
setfattr -x user.xdg.tags "$D/c.txt" || e=1
[ -z "$(xval "$D/c.txt" user.xdg.tags)" ] || e=1
[ -z "$(xval "$L/c.txt" user.xdg.tags)" ] || e=1
if [ "$e" -eq 0 ]; then ok xdg-tags-roundtrip; else bad xdg-tags-roundtrip; fi
setfattr -n user.xdg.tags -v draft "$D/c.txt"

section fselect
run fselect "path, xattr(user.moob.kind) from $D"
run fselect "path, xattr(user.xdg.tags), xattr(user.baloo.rating) from $D"
run fselect "path, has_xattrs, xattr_count from $D"
r1=$(rows fselect "path from $D where has_xattr(user.moob.kind)")
r2=$(rows fselect "path from $D where has_xattr(user.xdg.tags)")
if [ "$r1" = "a.txt b.md" ] && [ "$r2" = "b.md c.txt" ]; then
    ok fselect-has-xattr
else
    bad fselect-has-xattr "user.moob.kind: [$r1], user.xdg.tags: [$r2]"
fi
r3=$(rows fselect "path from $D where xattr(user.xdg.tags) like '%moobfs%'")
if [ "$r3" = b.md ]; then ok fselect-xdg-value; else bad fselect-xdg-value "[$r3]"; fi
echo "INTEROP_OBSERVED: fselect xattr(user.moob.kind) of a.txt, bytes:" \
    "$(bytes fselect "xattr(user.moob.kind) from $D where name = 'a.txt'")"
echo "INTEROP_OBSERVED: fselect xattr(user.moob.score) of a.txt, bytes:" \
    "$(bytes fselect "xattr(user.moob.score) from $D where name = 'a.txt'")"
echo "INTEROP_OBSERVED: fselect where xattr(user.moob.kind) = 'decision':" \
    "[$(rows fselect "path from $D where xattr(user.moob.kind) = 'decision'")]," \
    "like '%decision': [$(rows fselect "path from $D where xattr(user.moob.kind) like '%decision'")]"

section cshatag
run cshatag "$D/a.txt" "$D/b.md"
run getfattr -d -m 'user.shatag' "$L/a.txt"
echo "unchanged files, second run:"
run cshatag "$D/a.txt" "$D/b.md" | tee /tmp/x.out
if grep -q "^<ok> $D/a.txt\$" /tmp/x.out && grep -q "^<ok> $D/b.md\$" /tmp/x.out &&
    grep -q '^\[exit 0\]$' /tmp/x.out; then
    ok cshatag-ok
else
    bad cshatag-ok
fi
echo "content changed through the layer, mtime updated (an ordinary edit):"
printf 'Appended line.\n' >> "$D/b.md"
run cshatag "$D/b.md" | tee /tmp/x.out
check cshatag-outdated grep -q "^<outdated> $D/b.md\$" /tmp/x.out
echo "content changed through the layer, mtime restored (what silent corruption looks like):"
# the reference file is on the lower ext4, outside notes/: the guest's /tmp can be the host's
# root shared over 9p or virtiofs, which may drop the fraction of a second, and cshatag
# compares mtimes to the nanosecond (seen in a local run, journal 0140)
touch -r "$D/a.txt" "$LOWER/a.ref"
printf 'Decision: keep XFS as the lower filesystem.\n' > "$D/a.txt"
touch -r "$LOWER/a.ref" "$D/a.txt"
rm -f "$LOWER/a.ref"
run cshatag "$D/a.txt" | tee /tmp/x.out
# cshatag exits 5 when it found a corrupt file
if grep -q "^<corrupt> $D/a.txt\$" /tmp/x.out && grep -q '^\[exit 5\]$' /tmp/x.out; then
    ok cshatag-corrupt
else
    bad cshatag-corrupt
fi
echo "typed attributes after cshatag wrote its own:"
run moobfs-attr list "$D/a.txt"
o=$(moobfs-attr get "$D/a.txt" kind 2>&1)
if [ "$o" = "$(printf 'kind\tstring\tdecision')" ] && [ -n "$(xval "$L/a.txt" user.shatag.sha256)" ]; then
    ok cshatag-keeps-typed
else
    bad cshatag-keeps-typed "$o"
fi

section recoll
C=/tmp/rcl
mkdir -p "$C"
cat > "$C/recoll.conf" << EOF
topdirs = $D
skippedPaths =
skippedNames = lost+found
noaspell = 1
loglevel = 3
EOF
# xattrtofields: name without "user." -> field; prefixes make a field searchable as
# field:term; stored makes it come back in the results
cat > "$C/fields" << 'EOF'
[prefixes]
moobkind = XYMK
moobscore = XYMS
xdgtags = XYXT
xdgcomment = XYXC
[stored]
moobkind =
moobscore =
xdgtags =
xdgcomment =
[xattrtofields]
moob.kind = moobkind
moob.score = moobscore
xdg.tags = xdgtags
xdg.comment = xdgcomment
EOF
run recollindex -c "$C"
for q in moobkind:decision moobkind:note xdgtags:moobfs xdgtags:draft xdgcomment:reviewed moobscore:42; do
    run recollq -c "$C" -m "$q"
done
# hits Q: the names of the files query Q returns, sorted, on one line
hits() { rows sh -c "recollq -c '$C' -b '$1' | sed -n 's|^file://$D/||p'"; }
if [ "$(hits moobkind:decision)" = a.txt ] && [ "$(hits moobkind:note)" = b.md ]; then
    ok recoll-moob-field
else
    bad recoll-moob-field "decision: [$(hits moobkind:decision)], note: [$(hits moobkind:note)]"
fi
if [ "$(hits xdgtags:moobfs)" = b.md ] && [ "$(hits xdgtags:draft)" = c.txt ] &&
    [ "$(hits xdgcomment:reviewed)" = b.md ]; then
    ok recoll-xdg-fields
else
    bad recoll-xdg-fields "moobfs: [$(hits xdgtags:moobfs)], draft: [$(hits xdgtags:draft)], reviewed: [$(hits xdgcomment:reviewed)]"
fi
echo "INTEROP_OBSERVED: recoll moobscore:42 finds [$(hits moobscore:42)] (integers are stored as raw bytes)"

section "KDE KFileMetaData (UserMetaData, KF5)"
if have kfmd; then
    run kfmd show "$D/a.txt"
    run kfmd show "$D/b.md"
    run kfmd settags "$D/c.txt" "kde,tagged"
    run kfmd setrating "$D/c.txt" 6
    run kfmd setcomment "$D/c.txt" "written by KFileMetaData"
    run kfmd show "$D/c.txt"
    run getfattr -d -m - "$L/c.txt"
    echo "INTEROP_OBSERVED: kfmd $(kfmd show "$D/a.txt" 2> /dev/null | grep '^attribute(moob.kind)')"
else
    echo "INTEROP_SKIP: kfmd (the KFileMetaData helper was not built)"
fi

section "final state, through the layer and on the lower"
run getfattr -R -d -m - -e hex "$D"
check hex-layer-equals-lower-final same_hex a.txt b.md c.txt
umount "$LAYER" && umount "$LOWER" && rmmod moobfs_layer || bad cleanup
dmesg > "$OUT/dmesg.txt"
check kernel-log kdiag "$OUT/dmesg.txt" 'BUG:|Oops:|^WARNING:|Kernel panic'
if [ -z "$FAILED" ]; then
    echo "INTEROP_PASS"
else
    echo "INTEROP_FAIL:$FAILED"
fi
