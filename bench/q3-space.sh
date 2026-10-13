#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# M4 question 3: does delegated physical sharing (reflink, dedupe) save space on an agent-memory
# load, and how much? Works on a private copy of an existing data set, on btrfs or XFS, with a
# second layer mounted over the copy, so the data set and its mounted layer are left alone.
#
#   sudo sh bench/q3-space.sh <lower fs mount point> <data set directory inside it> <out dir>
#
# Needs xfs_io, sha256sum and the layer module loaded. Prints and stores:
#   - the size profile of the files, and how much content is byte-identical between files;
#   - bytes used after a plain copy, and after whole-file dedupe through the layer;
#   - bytes used by a second copy of the tree made through the layer with reflink, against a
#     plain second copy.
# Bytes used are the file system's own (statfs), read after syncfs on the lower.
set -eu
FS=$1; SRC=$2; OUT=$3
WORK=$FS/q3-work; MNT=$(mktemp -d /tmp/q3mnt.XXXXXX)
mkdir -p "$OUT"
used() { sync -f "$FS"; df -B1 --output=used "$FS" | tail -1 | tr -d ' '; }
cleanup() { mountpoint -q "$MNT" && umount "$MNT"; rm -rf "$WORK"; rmdir "$MNT" 2>/dev/null || true; }
trap cleanup EXIT
rm -rf "$WORK"; mkdir "$WORK"
u0=$(used)
cp -a --reflink=never "$SRC" "$WORK/a"
u1=$(used)
# size profile and identical content
find "$WORK/a" -type f -printf '%s\n' | awk '{n++; t+=$1; if($1<=2048)a++; else if($1<=4096)b++; else c++; blk+=int(($1+4095)/4096)*4096}
    END{printf "files=%d bytes=%d bytes_in_4k_blocks=%d le2048=%d le4096=%d gt4096=%d\n",n,t,blk,a,b,c}' > "$OUT/profile.txt"
find "$WORK/a" -type f -exec sha256sum {} + | awk '{print $1}' | sort | uniq -c |
    awk '{g++; f+=$1; if($1>1){dg++; df+=$1-1}} END{printf "distinct_contents=%d files=%d groups_with_copies=%d redundant_files=%d\n",g,f,dg,df}' >> "$OUT/profile.txt"
mount -t moobfs_layer -o lower="$WORK",domain_depth=0 none "$MNT"
# Whole-file dedupe through the layer: every file whose content equals an earlier file is
# deduplicated against it with FIDEDUPERANGE (xfs_io). duperemove 0.11.2 finds no candidate in
# files this small, on the layer or on the bare lower, so it is not used.
find "$MNT/a" -type f -exec sha256sum {} + | sort > "$OUT/hashes.txt"
awk '{h=$1; sub(/^[^ ]+ +/,""); if (h==prev) print first "\t" $0; else {prev=h; first=$0}}' "$OUT/hashes.txt" > "$OUT/pairs.txt"
ok=0; bad=0; bytes=0
while IFS="$(printf '\t')" read -r first dup; do
    sz=$(stat -c %s "$dup")
    if xfs_io -c "dedupe $first 0 0 $sz" "$dup" 2>/dev/null | grep -q "^deduped $sz/"; then
        ok=$((ok + 1)); bytes=$((bytes + sz))
    else
        bad=$((bad + 1))
    fi
done < "$OUT/pairs.txt"
echo "dedupe_calls_ok=$ok dedupe_calls_refused=$bad bytes_deduped=$bytes" > "$OUT/dedupe.txt"
rm -f "$OUT/hashes.txt" "$OUT/pairs.txt"
u2=$(used)
cp -a --reflink=always "$MNT/a" "$MNT/b-reflink" 2> "$OUT/reflink.err" || echo "reflink copy failed" >> "$OUT/reflink.err"
u3=$(used)
cp -a --reflink=never "$MNT/a" "$MNT/c-plain"
u4=$(used)
umount "$MNT"
{
    echo "fs=$(stat -f -c %T "$FS") kernel=$(uname -r)"
    cat "$OUT/profile.txt"
    cat "$OUT/dedupe.txt"
    echo "used_before=$u0"
    echo "plain_copy=$((u1 - u0))"
    echo "after_dedupe_through_layer=$((u2 - u0)) saved=$((u1 - u2))"
    echo "second_copy_reflink_through_layer=$((u3 - u2))"
    echo "second_copy_plain_through_layer=$((u4 - u3))"
} | tee "$OUT/result.txt"
