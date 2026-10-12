#!/bin/sh
# MoobFS layer test suite, shared by the Debian initramfs harness (vm/init) and the
# KASAN/lockdep harness (vm/kasan-guest.sh). The caller sets:
#   BIN        directory with layer, fsx, query, ipc, moobfs-*, replay-log, moobfs_layer.ko
#   TOOLS      directory with git, sqlite3, dmsetup, mke2fs, e2fsck
#   MODS       optional directory with lower-fs modules and an "order" file
#   DEV_EXT4 DEV_BTRFS DEV_XFS   block devices carrying the prepared lower filesystems
#   DEV_SCRATCH DEV_LOG          blank block devices for dm-log-writes and dm-flakey
# and a fail() function. LOWER and LAYER (mount points) default to /lower and /mnt/layer.
# Prints MOOBFS_LAYER_PASS and MOOBFS_ACCEPTANCE_PASS on success.
set -u
LOWER=${LOWER:-/lower}
LAYER=${LAYER:-/mnt/layer}
mkdir -p "$LOWER" "$LAYER" /tmp
# Phases 2a/2b: memory layer stacked over ext4, btrfs and XFS (virtio disks vda, vdb, vdc).
if [ -f "$MODS/order" ]; then for m in $(cat "$MODS/order"); do insmod "$MODS/$m" || fail "lower module $m"; done; fi
insmod "$BIN/moobfs_layer.ko" || fail layer-module
mount -t moobfs_layer none $LAYER && fail layer-mount-without-lower
mount -t moobfs_layer -o lower=/nonexistent none $LAYER && fail layer-mount-bad-lower
run_lower() {
    fs=$1; dev=$2; reflink=$3
    mount -t "$fs" "$dev" $LOWER || fail "lower-mount-$fs"
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail "layer-mount-$fs"
    grep -q 'moobfs_layer' /proc/mounts || fail "layer-proc-mounts-$fs"
    mount -t moobfs_layer -o lower=$LAYER none /mnt && fail "layer-stack-on-itself-$fs"
    $BIN/layer $LAYER $LOWER "$reflink" links || fail "layer-tests-$fs"
    # typed attribute tools: write through the layer, check the lower tree
    echo memo > $LAYER/typed.md || fail "typed-create-$fs"
    $BIN/moobfs-attr set $LAYER/typed.md kind string decision || fail "attr-set-$fs"
    $BIN/moobfs-attr set $LAYER/typed.md created time 1760000000000000000 || fail "attr-time-$fs"
    $BIN/moobfs-attr set $LAYER/typed.md pinned bool true || fail "attr-bool-$fs"
    $BIN/moobfs-attr set $LAYER/typed.md score double 0.5 || fail "attr-double-$fs"
    [ "$($BIN/moobfs-attr get $LAYER/typed.md kind)" = "$(printf 'kind\tstring\tdecision')" ] || fail "attr-get-$fs"
    [ "$($BIN/moobfs-attr list $LOWER/typed.md | wc -l)" -eq 4 ] || fail "attr-list-lower-$fs"
    $BIN/moobfs-attr set $LAYER/typed.md Bad string x 2>/dev/null && fail "attr-badname-accepted-$fs"
    $BIN/moobfs-check $LOWER > /tmp/check.json || fail "check-clean-$fs"
    grep -q '"problem_count": 0' /tmp/check.json || fail "check-count-$fs"
    # plant an invalid encoding directly on the lower tree: the checker must flag it
    $BIN/moobfs-attr rawset $LOWER/typed.md broken 0402 || fail "rawset-lower-$fs"
    $BIN/moobfs-check $LOWER > /tmp/check.json; [ $? -eq 2 ] || fail "check-detects-$fs"
    grep -q 'payload is not a valid value' /tmp/check.json || fail "check-reason-$fs"
    $BIN/moobfs-attr get $LAYER/typed.md broken >/dev/null; [ $? -eq 2 ] || fail "attr-get-invalid-$fs"
    $BIN/moobfs-attr rm $LAYER/typed.md broken || fail "attr-rm-$fs"
    $BIN/moobfs-check $LOWER > /dev/null || fail "check-clean-again-$fs"
    rm $LAYER/typed.md || fail "typed-rm-$fs"
    # index and QUERY: the kernel answer must equal the user-space oracle on the lower tree
    same_query() {
        $BIN/query $LAYER "$@" > /tmp/k.txt || return 1
        $BIN/moobfs-index $LOWER "$@" > /tmp/o.txt || return 1
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; return 1; }
    }
    mkdir -p $LAYER/q/sub || fail "q-mkdir-$fs"
    for i in 1 2 3 4 5; do
        echo "memory $i" > $LAYER/q/f$i
        $BIN/moobfs-attr set $LAYER/q/f$i score int64 $((i * 10)) || fail "q-score-$fs"
    done
    $BIN/moobfs-attr set $LAYER/q/f1 kind string decision || fail "q-kind-$fs"
    $BIN/moobfs-attr set $LAYER/q/f2 kind string note || fail "q-kind-$fs"
    $BIN/moobfs-attr set $LAYER/q/f3 kind string decided || fail "q-kind-$fs"
    $BIN/moobfs-attr set $LAYER/q/f4 kind string note || fail "q-kind-$fs"
    $BIN/moobfs-attr set $LAYER/q/sub kind string folder || fail "q-kind-dir-$fs"
    $BIN/moobfs-attr set $LAYER/q/f2 pinned bool true || fail "q-pinned-$fs"
    $BIN/moobfs-attr set $LAYER/q/f5 pinned bool false || fail "q-pinned-$fs"
    $BIN/moobfs-attr set $LAYER/q/f3 weight double 0.75 || fail "q-double-$fs"
    $BIN/moobfs-attr set $LAYER/q/f4 weight double -2.5 || fail "q-double-$fs"
    $BIN/moobfs-attr set $LAYER/q/f1 raw raw deadbeef || fail "q-raw-$fs"
    same_query kind eq string decision || fail "query-eq-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q 'q/f1$' /tmp/k.txt || fail "query-eq-content-$fs"
    same_query kind eq string note score ge int64 40 || fail "query-and-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q 'q/f4$' /tmp/k.txt || fail "query-and-content-$fs"
    same_query kind prefix string dec || fail "query-prefix-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 2 ] || fail "query-prefix-count-$fs"
    same_query score gt int64 20 || fail "query-gt-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 3 ] || fail "query-gt-count-$fs"
    same_query score lt int64 0 || fail "query-empty-$fs"
    [ ! -s /tmp/k.txt ] || fail "query-empty-count-$fs"
    # one attribute name holding several types across files: the scan seeks to the first
    # candidate and stops after the last one, so every op must still equal the oracle when
    # entries of other types sit before and after the term's run in the tree
    i=0
    for v in "string b" "string ba" "string bz" "string c" "string a" "int64 5" "int64 -3" "int64 5" "double 1.5" "double -2.25" "bool true" "bool false" "time 100" "time 50"; do
        i=$((i + 1)); : > $LAYER/q/m$i
        # shellcheck disable=SC2086
        $BIN/moobfs-attr set $LAYER/q/m$i mix $v || fail "q-mix-set-$i-$fs"
    done
    for t in "eq string b" "prefix string b" "prefix string bz" "prefix string x" "lt string c" "le string ba" "gt string b" "ge string bz" \
             "eq int64 5" "lt int64 5" "le int64 5" "gt int64 -3" "ge int64 -3" "gt int64 100" "lt int64 -10" \
             "eq double 1.5" "lt double 0" "ge double -2.25" "eq bool true" "lt bool true" "gt time 50" "le time 100" "exists - -"; do
        # shellcheck disable=SC2086
        same_query mix $t || fail "query-mix-$t-$fs"
    done
    [ "$($BIN/query $LAYER mix eq int64 5 | wc -l)" -eq 2 ] || fail "query-mix-eq-count-$fs"
    [ "$($BIN/query $LAYER mix prefix string b | wc -l)" -eq 3 ] || fail "query-mix-prefix-count-$fs"
    [ "$($BIN/query $LAYER mix exists - - | wc -l)" -eq 14 ] || fail "query-mix-exists-count-$fs"
    rm -f $LAYER/q/m* || fail "q-mix-cleanup-$fs"
    same_query pinned exists - - || fail "query-exists-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 2 ] || fail "query-exists-count-$fs"
    same_query pinned eq bool true || fail "query-bool-$fs"
    same_query weight lt double 0 || fail "query-double-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q 'q/f4$' /tmp/k.txt || fail "query-double-content-$fs"
    same_query kind eq string folder || fail "query-dir-$fs"
    grep -q 'q/sub$' /tmp/k.txt || fail "query-dir-content-$fs"
    $BIN/query $LAYER raw exists - - > /tmp/k.txt || fail "query-raw-$fs"
    [ ! -s /tmp/k.txt ] || fail "query-raw-not-indexed-$fs"
    $BIN/query $LAYER invalid || fail "query-invalid-$fs"
    $BIN/query $LAYER info > /tmp/info.txt || fail "query-info-$fs"
    grep -q "lower=$fs" /tmp/info.txt || fail "query-info-lower-$fs"
    grep -q 'entries=1[0-9] inodes=6' /tmp/info.txt || { cat /tmp/info.txt; fail "query-info-counts-$fs"; }
    # updates: rename, directory rename, unlink and removexattr are reflected
    mv $LAYER/q/f1 $LAYER/q/g1 || fail "q-rename-$fs"
    same_query kind eq string decision || fail "query-renamed-$fs"
    grep -q 'q/g1$' /tmp/k.txt || fail "query-renamed-path-$fs"
    mv $LAYER/q $LAYER/r || fail "q-dir-rename-$fs"
    same_query kind eq string decision || fail "query-dir-renamed-$fs"
    grep -q 'r/g1$' /tmp/k.txt || fail "query-dir-renamed-path-$fs"
    rm $LAYER/r/f2 || fail "q-unlink-$fs"
    same_query kind eq string note || fail "query-unlinked-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] || fail "query-unlinked-count-$fs"
    $BIN/moobfs-attr rm $LAYER/r/f4 kind || fail "q-removexattr-$fs"
    same_query kind eq string note || fail "query-removexattr-$fs"
    [ ! -s /tmp/k.txt ] || fail "query-removexattr-count-$fs"
    # remount rebuilds the index from the lower tree
    umount $LAYER || fail "q-umount-$fs"
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail "q-remount-$fs"
    same_query score ge int64 10 || fail "query-after-remount-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 4 ] || fail "query-after-remount-count-$fs"
    same_query kind eq string decision || fail "query-after-remount-eq-$fs"
    grep -q 'r/g1$' /tmp/k.txt || fail "query-after-remount-path-$fs"
    umount $LAYER || fail "q-umount2-$fs"
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0,noindex none $LAYER || fail "q-noindex-mount-$fs"
    $BIN/query $LAYER score ge int64 10 > /tmp/noindex.txt 2>&1 && fail "query-noindex-answered-$fs"
    grep -q 'Operation not supported' /tmp/noindex.txt || { cat /tmp/noindex.txt; fail "query-noindex-errno-$fs"; }
    [ "$($BIN/query $LAYER q2errno kind exists x x)" = "q2 95" ] || fail "query2-noindex-errno-$fs"   # EOPNOTSUPP
    f=$($BIN/query $LAYER info | sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p')
    [ -n "$f" ] && [ $(( f & 2 )) -ne 0 ] || fail "query-noindex-flag-$fs"   # MOOB_FLAG_INDEX_OFF
    umount $LAYER || fail "q-umount3-$fs"
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0,index_budget=1M none $LAYER || fail "q-budget-mount-$fs"
    same_query score ge int64 10 || fail "query-budget-$fs"
    umount $LAYER || fail "q-umount4-$fs"
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail "q-remount-final-$fs"
    rm -rf $LAYER/r || fail "q-cleanup-$fs"
    # isolation (M3 phase 3b): two users, two namespaces; QUERY as each user must equal the
    # oracle run as that user, and must not reveal the other namespace, not even in counts
    mkdir -p $LAYER/ns/a $LAYER/ns/b $LAYER/ns/shared || fail "iso-mkdir-$fs"
    for d in a b shared; do
        for i in 1 2 3; do
            echo "secret $d $i" > $LAYER/ns/$d/m$i.md
            chmod 0600 $LAYER/ns/$d/m$i.md || fail "iso-mode-$fs"
            $BIN/moobfs-attr set $LAYER/ns/$d/m$i.md kind string note || fail "iso-attr-$fs"
            $BIN/moobfs-attr set $LAYER/ns/$d/m$i.md namespace string $d || fail "iso-ns-$fs"
        done
    done
    chown -R 1001:1001 $LAYER/ns/a && chmod 0700 $LAYER/ns/a || fail "iso-chown-a-$fs"
    chown -R 1002:1002 $LAYER/ns/b && chmod 0700 $LAYER/ns/b || fail "iso-chown-b-$fs"
    chmod -R a+rX $LAYER/ns/shared && chmod 0755 $LAYER/ns $LAYER/ns/shared || fail "iso-shared-$fs"
    chmod 0755 $LAYER || fail "iso-root-mode-$fs"   # QUERY needs an open descriptor of the root
    for u in 1001 1002; do
        $BIN/query --uid $u $LAYER kind eq string note > /tmp/k.txt || fail "iso-query-$u-$fs"
        $BIN/moobfs-index --uid $u $LOWER kind eq string note > /tmp/o.txt || fail "iso-oracle-$u-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "iso-mismatch-$u-$fs"; }
        [ "$(wc -l < /tmp/k.txt)" -eq 6 ] || fail "iso-count-$u-$fs"
        other=$([ $u = 1001 ] && echo b || echo a)
        grep -q "ns/$other/" /tmp/k.txt && fail "iso-leak-$u-$fs"
        grep -q "ns/shared/" /tmp/k.txt || fail "iso-shared-missing-$u-$fs"
    done
    $BIN/query $LAYER kind eq string note > /tmp/k.txt || fail "iso-query-root-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 9 ] || fail "iso-root-count-$fs"
    # many candidates, few authorized: results and counts must stay exact beyond one chunk
    mkdir -p $LAYER/ns/bulk && chmod 0700 $LAYER/ns/bulk || fail "iso-bulk-$fs"
    for i in $(seq 1 600); do : > $LAYER/ns/bulk/f$i; done
    $BIN/moobfs-attr set $LAYER/ns/bulk/f1 kind string note >/dev/null || fail "iso-bulk-attr-$fs"
    for i in $(seq 2 600); do setfattr -n user.moob.kind -v "$(printf '\001note')" $LAYER/ns/bulk/f$i 2>/dev/null || $BIN/moobfs-attr set $LAYER/ns/bulk/f$i kind string note || fail "iso-bulk-attr-$fs"; done
    chown -R 1002:1002 $LAYER/ns/bulk || fail "iso-bulk-chown-$fs"
    $BIN/query --uid 1001 $LAYER kind eq string note > /tmp/k.txt || fail "iso-bulk-query-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 6 ] || fail "iso-bulk-count-$fs"
    $BIN/moobfs-index --uid 1001 $LOWER kind eq string note > /tmp/o.txt || fail "iso-bulk-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt || fail "iso-bulk-mismatch-$fs"
    rm -rf $LAYER/ns/bulk || fail "iso-bulk-rm-$fs"
    # changing the attribute never grants access; changing permissions does, immediately
    $BIN/moobfs-attr set $LAYER/ns/b/m1.md namespace string a || fail "iso-reattr-$fs"
    $BIN/query --uid 1001 $LAYER namespace eq string a > /tmp/k.txt || fail "iso-query-reattr-$fs"
    grep -q "ns/b/" /tmp/k.txt && fail "iso-attr-grants-$fs"
    chmod 0755 $LAYER/ns/b && chmod a+r $LAYER/ns/b/m1.md || fail "iso-chmod-$fs"
    $BIN/query --uid 1001 $LAYER namespace eq string a > /tmp/k.txt || fail "iso-query-chmod-$fs"
    grep -q "ns/b/m1.md" /tmp/k.txt || fail "iso-perm-not-seen-$fs"
    grep -q "ns/b/m2.md" /tmp/k.txt && fail "iso-perm-overreach-$fs"
    umount $LAYER || fail "iso-umount-$fs"
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail "iso-remount-$fs"
    $BIN/query --uid 1001 $LAYER kind eq string note > /tmp/k.txt || fail "iso-query-remount-$fs"
    grep -q "ns/b/m2.md" /tmp/k.txt && fail "iso-leak-remount-$fs"
    # ACLs: QUERY must agree with open() under the same uid when ACLs grant or deny
    chmod 0700 $LAYER/ns/b && chmod 0600 $LAYER/ns/b/m1.md || fail "acl-reset-$fs"
    $TOOLS/setfacl -m u:1001:x $LAYER/ns/b && $TOOLS/setfacl -m u:1001:r $LAYER/ns/b/m2.md || fail "acl-set-$fs"
    $BIN/query --uid 1001 $LAYER open ns/b/m2.md | grep -q ': ok' || fail "acl-open-grant-$fs"
    $BIN/query --uid 1001 $LAYER kind eq string note > /tmp/k.txt || fail "acl-query-$fs"
    grep -q 'ns/b/m2.md' /tmp/k.txt || fail "acl-grant-not-seen-$fs"
    grep -q 'ns/b/m1.md' /tmp/k.txt && fail "acl-overreach-$fs"
    $TOOLS/setfacl -m u:1001:- $LAYER/ns/shared/m1.md || fail "acl-deny-$fs"
    $BIN/query --uid 1001 $LAYER open ns/shared/m1.md | grep -q 'Permission denied' || fail "acl-open-deny-$fs"
    $BIN/query --uid 1001 $LAYER kind eq string note > /tmp/k.txt || fail "acl-query2-$fs"
    grep -q 'ns/shared/m1.md' /tmp/k.txt && fail "acl-deny-leak-$fs"
    $TOOLS/setfacl -b $LAYER/ns/b $LAYER/ns/b/m2.md $LAYER/ns/shared/m1.md || fail "acl-clear-$fs"
    $BIN/query --uid 1001 $LAYER kind eq string note > /tmp/k.txt || fail "acl-query3-$fs"
    grep -q 'ns/b/m2.md' /tmp/k.txt && fail "acl-clear-leak-$fs"
    grep -q 'ns/shared/m1.md' /tmp/k.txt || fail "acl-clear-missing-$fs"
    # identity (spec-m3 §2): same id across namespaces is normal, on two inodes in one namespace is an error
    for f in ns/a/m1.md ns/shared/m1.md; do $BIN/moobfs-attr set $LAYER/$f memory_id string id-1 || fail "id-set-$fs"; done
    $BIN/moobfs-attr set $LAYER/ns/a/m1.md namespace string a && $BIN/moobfs-attr set $LAYER/ns/shared/m1.md namespace string shared || fail "id-ns-$fs"
    $BIN/moobfs-check $LOWER > /tmp/check.json || { cat /tmp/check.json; fail "id-check-normal-$fs"; }
    grep -q '"same_id_other_namespace": 1' /tmp/check.json || fail "id-cross-count-$fs"
    ln $LAYER/ns/a/m1.md $LAYER/ns/a/link1.md 2>/dev/null && { $BIN/moobfs-check $LOWER > /tmp/check.json || fail "id-hardlink-flagged-$fs"; rm $LAYER/ns/a/link1.md; }
    $BIN/moobfs-attr set $LAYER/ns/a/m2.md memory_id string id-1 && $BIN/moobfs-attr set $LAYER/ns/a/m2.md namespace string a || fail "id-dup-set-$fs"
    $BIN/moobfs-check $LOWER > /tmp/check.json; [ $? -eq 2 ] || fail "id-dup-not-detected-$fs"
    grep -q 'duplicate (namespace, memory_id)' /tmp/check.json || fail "id-dup-reason-$fs"
    $BIN/moobfs-attr rm $LAYER/ns/a/m2.md memory_id || fail "id-dup-rm-$fs"
    # QUERY v2 (docs/spec-query-v2.md, phase Q2a): first-level directories are domains.
    # The tree is built under domain_depth=0 so that no permission change happens after the
    # domain mount; the kernel answer is compared with the oracle's independent homogeneity rule.
    mkdir -p $LAYER/da/sub $LAYER/db $LAYER/dshared || fail "q2-mkdir-$fs"
    for d in da db dshared da/sub; do
        for i in 1 2 3; do
            [ $d = da/sub ] && [ $i -gt 1 ] && continue
            echo "q2 $d $i" > $LAYER/$d/m$i.md
            $BIN/moobfs-attr set $LAYER/$d/m$i.md kind string note >/dev/null || fail "q2-attr-$fs"
        done
    done
    chmod 0600 $LAYER/da/m*.md $LAYER/da/sub/m*.md $LAYER/db/m*.md && chmod 0644 $LAYER/dshared/m*.md || fail "q2-mode-$fs"
    chown -R 1001:1001 $LAYER/da && chmod 0700 $LAYER/da $LAYER/da/sub || fail "q2-chown-a-$fs"
    chown -R 1002:1002 $LAYER/db && chmod 0700 $LAYER/db || fail "q2-chown-b-$fs"
    chmod 0755 $LAYER/dshared || fail "q2-shared-$fs"
    umount $LAYER || fail "q2-umount-$fs"
    mount -t moobfs_layer -o lower=$LOWER none $LAYER || fail "q2-mount-$fs"
    $BIN/query $LAYER info > /tmp/info.txt || fail "q2-info-$fs"
    grep -q 'domain_depth=1 ' /tmp/info.txt || { cat /tmp/info.txt; fail "q2-info-depth-$fs"; }
    grep -q 'fallback_domains=0' /tmp/info.txt || { cat /tmp/info.txt; fail "q2-info-fallback0-$fs"; }
    f=$(sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p' /tmp/info.txt)
    [ $(( f & 16 )) -ne 0 ] || { cat /tmp/info.txt; fail "q2-info-flag-$fs"; }   # MOOB_FLAG_DOMAINS
    hetero() { $BIN/query $LAYER info | sed -n 's/.*heterogeneous=\([0-9]*\).*/\1/p'; }
    q2cmp() {   # uid domain expected-count tag
        $BIN/query --uid $1 $LAYER/$2 q2 kind eq string note > /tmp/k.txt || fail "q2-query-$4-$fs"
        $BIN/moobfs-index --domain $LOWER/$2 kind eq string note > /tmp/o.txt || fail "q2-oracle-$4-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "q2-mismatch-$4-$fs"; }
        [ "$(wc -l < /tmp/k.txt)" -eq $3 ] || { cat /tmp/k.txt; fail "q2-count-$4-$fs"; }
    }
    q2cmp 1001 da 4 own-a
    q2cmp 1002 db 3 own-b
    q2cmp 1001 dshared 3 shared
    grep -q 'sub/m1.md' /tmp/k.txt && fail "q2-shared-path-$fs"
    $BIN/query --uid 1001 $LAYER/da q2 kind eq string note | grep -q "	sub/m1.md$" || fail "q2-relative-path-$fs"
    # isolation (test 5) and descriptors that are not domains
    [ "$($BIN/query --uid 1001 $LAYER/db q2errno kind eq string note)" = "q2 13" ] || fail "q2-isolation-$fs"
    [ "$($BIN/query $LAYER/da/sub q2errno kind eq string note)" = "q2 25" ] || fail "q2-enotty-sub-$fs"
    [ "$($BIN/query $LAYER q2errno kind eq string note)" = "q2 25" ] || fail "q2-enotty-root-$fs"
    $BIN/query $LAYER/dshared q2invalid | grep -q QUERY2_INVALID_REQUESTS_REFUSED || fail "q2-invalid-$fs"
    # v1 is restricted to CAP_SYS_ADMIN while domains are on (spec §4.5)
    $BIN/query --uid 1001 $LAYER kind eq string note > /tmp/k.txt 2>/dev/null && fail "q2-v1-open-to-user-$fs"
    $BIN/query $LAYER kind eq string note > /tmp/k.txt || fail "q2-v1-root-$fs"
    grep -q 'db/m1.md' /tmp/k.txt || fail "q2-v1-root-content-$fs"
    # homogeneity (test 3): a stricter mode removes a file at the next call; reverting restores it
    h0=$(hetero)
    chmod 0640 $LAYER/dshared/m1.md || fail "q2-chmod-file-$fs"
    q2cmp 1001 dshared 2 hetero-mode
    grep -q 'm1.md' /tmp/k.txt && fail "q2-hetero-leak-$fs"
    [ "$(hetero)" -eq $((h0 + 1)) ] || fail "q2-hetero-count-$fs"
    chmod 0644 $LAYER/dshared/m1.md || fail "q2-chmod-back-$fs"
    q2cmp 1001 dshared 3 hetero-back
    [ "$(hetero)" -eq $h0 ] || fail "q2-hetero-count-back-$fs"
    # an extended access ACL excludes a file unless the mode alone makes it world readable
    $TOOLS/setfacl -m u:1002:r $LAYER/dshared/m2.md || fail "q2-acl-set-$fs"
    q2cmp 1001 dshared 2 acl
    $TOOLS/setfacl -b $LAYER/dshared/m2.md && chmod 0644 $LAYER/dshared/m2.md || fail "q2-acl-clear-$fs"
    q2cmp 1001 dshared 3 acl-back
    # hard links make a file heterogeneous (another name may sit in another domain)
    ln $LAYER/dshared/m3.md $LAYER/dshared/l3.md || fail "q2-link-$fs"
    q2cmp 1001 dshared 2 nlink
    rm $LAYER/dshared/l3.md || fail "q2-unlink-$fs"
    # back to one link the file may stay excluded (fail-safe) until its next mode change
    chmod 0644 $LAYER/dshared/m3.md || fail "q2-chmod-relink-$fs"
    q2cmp 1001 dshared 3 relinked
    # a directory permission change inside a domain switches it to per-result checks
    chmod 0600 $LAYER/da/sub || fail "q2-chmod-dir-$fs"
    $BIN/query $LAYER info | grep -q 'fallback_domains=1' || fail "q2-fallback-$fs"
    q2cmp 1001 da 3 fallback
    chmod 0700 $LAYER/da/sub || fail "q2-chmod-dir-back-$fs"
    q2cmp 1001 da 4 fallback-back
    # renames: a directory may not leave its domain or depth (EXDEV); a file changes partition
    [ "$($BIN/query $LAYER rename $LAYER/da/sub $LAYER/dshared/sub)" = "rename 18" ] || fail "q2-exdev-domain-$fs"
    [ "$($BIN/query $LAYER rename $LAYER/da/sub $LAYER/sub)" = "rename 18" ] || fail "q2-exdev-depth-$fs"
    [ "$($BIN/query $LAYER rename $LAYER/da/sub $LAYER/da/sub2)" = "rename 0" ] || fail "q2-rename-inside-$fs"
    mv $LAYER/da/sub2 $LAYER/da/sub || fail "q2-rename-back-$fs"
    mv $LAYER/dshared/m3.md $LAYER/da/m9.md && chown 1001:1001 $LAYER/da/m9.md && chmod 0600 $LAYER/da/m9.md || fail "q2-move-file-$fs"
    q2cmp 1001 da 5 moved-in
    q2cmp 1001 dshared 2 moved-out
    # a remount rebuilds the same partitions and clears the fallback
    umount $LAYER && mount -t moobfs_layer -o lower=$LOWER none $LAYER || fail "q2-remount-$fs"
    $BIN/query $LAYER info | grep -q 'fallback_domains=0' || fail "q2-fallback-cleared-$fs"
    q2cmp 1001 da 5 remount-a
    q2cmp 1002 db 3 remount-b
    q2cmp 1002 dshared 2 remount-shared
    # QUERY2 ordering and cursor (phase Q2b, spec test 7): 30 files in domain da with a time
    # attribute (ties included), two of them with an int64 under the same name, and string
    # titles, ten of them longer than the cursor's inline space
    long=$(head -c 100 /dev/zero | tr '\0' x)
    for i in $(seq 1 30); do
        f=$LAYER/da/o$i.md; echo "o $i" > $f
        if [ $i -ge 29 ]; then t="int64"; else t="time"; fi
        $BIN/moobfs-attr set $f created $t $(( (i % 10) * 1000 )) >/dev/null || fail "q2b-attr-$fs"
        [ $((i % 2)) -eq 1 ] && { $BIN/moobfs-attr set $f kind string note >/dev/null || fail "q2b-kind-$fs"; }
        if [ $i -le 10 ]; then ti="$long$((40 - i))"; else ti="t$i"; fi
        $BIN/moobfs-attr set $f title string "$ti" >/dev/null || fail "q2b-title-$fs"
        chown 1001:1001 $f && chmod 0600 $f || fail "q2b-mode-$fs"
    done
    q2ord() {   # tag expected-count query-options... -- oracle-options...
        tag=$1; want=$2; shift 2
        kq=""; while [ "$1" != "--" ]; do kq="$kq $1"; shift; done; shift
        # shellcheck disable=SC2086
        $BIN/query --uid 1001 $LAYER/da q2 $kq > /tmp/k.txt || fail "q2b-query-$tag-$fs"
        # shellcheck disable=SC2086
        $BIN/moobfs-index --domain "$@" > /tmp/o.txt || fail "q2b-oracle-$tag-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "q2b-mismatch-$tag-$fs"; }
        [ "$(wc -l < /tmp/k.txt)" -eq $want ] || { cat /tmp/k.txt; fail "q2b-count-$tag-$fs"; }
    }
    q2ord asc 28 --order created time asc -- --order created time asc $LOWER/da
    q2ord desc 28 --order created time desc -- --order created time desc $LOWER/da
    q2ord int64 2 --order created int64 desc -- --order created int64 desc $LOWER/da
    q2ord top5 5 --order created time desc --limit 5 kind eq string note -- --order created time desc --limit 5 $LOWER/da kind eq string note
    q2ord pages-desc 14 --order created time desc --limit 4 --pages kind eq string note -- --order created time desc $LOWER/da kind eq string note
    q2ord pages-asc 28 --order created time asc --limit 1 --pages -- --order created time asc $LOWER/da
    q2ord pages-long 30 --order title string asc --limit 3 --pages -- --order title string asc $LOWER/da
    q2ord pages-long-desc 30 --order title string desc --limit 4 --pages -- --order title string desc $LOWER/da
    q2ord range 8 --order created time asc --pages --limit 2 created ge time 7000 -- --order created time asc $LOWER/da created ge time 7000
    q2ord unordered-pages 20 --limit 3 --pages kind eq string note -- $LOWER/da kind eq string note
    # cursors: the same query continues; another query, a remount or a fallback refuses it
    $BIN/query --uid 1001 $LAYER/da q2 --limit 3 --cursor-out /tmp/c1 kind eq string note > /dev/null 2>&1 || fail "q2b-cursor-out-$fs"
    [ "$($BIN/query --uid 1001 $LAYER/da q2errno --limit 3 --cursor-in /tmp/c1 kind eq string note)" = "q2 0" ] || fail "q2b-cursor-same-$fs"
    [ "$($BIN/query --uid 1001 $LAYER/da q2errno --limit 9 --cursor-in /tmp/c1 kind eq string note)" = "q2 0" ] || fail "q2b-cursor-other-limit-$fs"
    [ "$($BIN/query --uid 1001 $LAYER/da q2errno --cursor-in /tmp/c1 kind exists x x)" = "q2 116" ] || fail "q2b-cursor-other-query-$fs"
    [ "$($BIN/query --uid 1001 $LAYER/da q2errno --cursor-in /tmp/c1 --order created time asc kind eq string note)" = "q2 116" ] || fail "q2b-cursor-other-order-$fs"
    chmod 0700 $LAYER/da/sub || fail "q2b-fallback-$fs"
    [ "$($BIN/query --uid 1001 $LAYER/da q2errno --limit 3 --cursor-in /tmp/c1 kind eq string note)" = "q2 116" ] || fail "q2b-cursor-fallback-$fs"
    q2ord fallback-pages 14 --order created time desc --limit 4 --pages kind eq string note -- --order created time desc $LOWER/da kind eq string note
    $BIN/query --uid 1001 $LAYER/da q2 --limit 3 --cursor-out /tmp/c1 kind eq string note > /dev/null 2>&1 || fail "q2b-cursor-out2-$fs"
    umount $LAYER && mount -t moobfs_layer -o lower=$LOWER none $LAYER || fail "q2b-remount-$fs"
    [ "$($BIN/query --uid 1001 $LAYER/da q2errno --limit 3 --cursor-in /tmp/c1 kind eq string note)" = "q2 116" ] || fail "q2b-cursor-remount-$fs"
    q2ord remount-pages 30 --order title string desc --limit 7 --pages -- --order title string desc $LOWER/da
    echo "QUERY2_ORDER_PASS: $fs"
    # leapfrog intersection of equality terms (phase Q2c): answers equal the oracle, paging
    # works, and the plan is really taken: with a non-equality first term whose values fall
    # as inodes rise, a plain scan would answer in falling inode order, the intersection in
    # rising inode order
    for i in $(seq 1 30); do
        f=$LAYER/da/o$i.md
        $BIN/moobfs-attr set $f ns string n$((i % 3)) >/dev/null &&
        $BIN/moobfs-attr set $f agent int64 $((i % 5)) >/dev/null &&
        $BIN/moobfs-attr set $f rank string r$((100 - i)) >/dev/null || fail "q2c-attr-$fs"
    done
    q2ord leap2 2 ns eq string n1 agent eq int64 2 -- $LOWER/da ns eq string n1 agent eq int64 2
    q2ord leap3 1 ns eq string n1 agent eq int64 2 kind eq string note -- $LOWER/da ns eq string n1 agent eq int64 2 kind eq string note
    q2ord leap-mixed 2 rank exists x x ns eq string n1 agent eq int64 2 -- $LOWER/da rank exists x x ns eq string n1 agent eq int64 2
    q2ord leap-range 1 ns eq string n1 created ge time 7000 agent eq int64 2 -- $LOWER/da ns eq string n1 created ge time 7000 agent eq int64 2
    q2ord leap-none 0 ns eq string n1 agent eq int64 7 -- $LOWER/da ns eq string n1 agent eq int64 7
    q2ord leap-type 0 ns eq string n1 agent eq string 2 -- $LOWER/da ns eq string n1 agent eq string 2
    q2ord leap-pages 5 --limit 2 --pages ns eq string n1 kind eq string note -- $LOWER/da ns eq string n1 kind eq string note
    q2ord leap-pages1 5 --limit 1 --pages kind eq string note ns eq string n1 -- $LOWER/da kind eq string note ns eq string n1
    $BIN/query --uid 1001 $LAYER/da q2 --raw rank exists x x ns eq string n1 kind eq string note | cut -f1 > /tmp/raw.txt || fail "q2c-raw-$fs"
    [ "$(wc -l < /tmp/raw.txt)" -eq 5 ] || fail "q2c-raw-count-$fs"
    sort -n -c /tmp/raw.txt || { cat /tmp/raw.txt; fail "q2c-plan-not-leapfrog-$fs"; }
    q2ord single-eq 10 rank exists x x ns eq string n1 -- $LOWER/da rank exists x x ns eq string n1
    # "missing or ..." terms and batches (phase Q2d): an expiry on every fourth file.
    # Operators such as gt? are unquoted words: globbing stays off until the block ends.
    set -f
    for i in 4 8 12 16 20 24 28; do
        $BIN/moobfs-attr set $LAYER/da/o$i.md expires time $((i * 1000)) >/dev/null || fail "q2d-attr-$fs"
    done
    q2ord miss-gt 9 ns eq string n1 expires gt? time 10000 -- $LOWER/da ns eq string n1 expires gt? time 10000
    q2ord miss-le 8 ns eq string n1 expires le? time 4000 -- $LOWER/da ns eq string n1 expires le? time 4000
    q2ord miss-first 9 expires gt? time 10000 ns eq string n1 -- $LOWER/da expires gt? time 10000 ns eq string n1
    q2ord miss-eq 10 ns eq string n1 kind eq? string note -- $LOWER/da ns eq string n1 kind eq? string note
    q2ord miss-type 0 ns eq string n1 agent eq? string 2 -- $LOWER/da ns eq string n1 agent eq? string 2
    q2ord miss-ordered 26 --order created time desc expires gt? time 10000 -- --order created time desc $LOWER/da expires gt? time 10000
    q2ord miss-pages 9 --limit 2 --pages ns eq string n1 expires gt? time 10000 -- $LOWER/da ns eq string n1 expires gt? time 10000
    q2batchcmp() {   # tag: three requests, one per namespace value, each equal to the oracle
        $BIN/query --uid 1001 $LAYER/da q2batch -- ns eq string n0 -- ns eq string n1 expires gt? time 10000 -- ns eq string n2 > /tmp/kb.txt || fail "q2d-batch-$1-$fs"
        k=0
        for q in "ns eq string n0" "ns eq string n1 expires gt? time 10000" "ns eq string n2"; do
            grep "^q$k	" /tmp/kb.txt | cut -f2- > /tmp/k.txt
            # shellcheck disable=SC2086
            $BIN/moobfs-index --domain $LOWER/da $q > /tmp/o.txt || fail "q2d-batch-oracle-$1-$fs"
            cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/kb.txt; echo "oracle $k:"; cat /tmp/o.txt; fail "q2d-batch-mismatch-$1-$k-$fs"; }
            k=$((k + 1))
        done
        [ "$(wc -l < /tmp/kb.txt)" -eq 29 ] || { cat /tmp/kb.txt; fail "q2d-batch-count-$1-$fs"; }
    }
    q2batchcmp plain
    [ "$($BIN/query --uid 1001 $LAYER/db q2batcherrno -- kind exists x x)" = "q2batch 13" ] || fail "q2d-batch-isolation-$fs"
    [ "$($BIN/query $LAYER/da/sub q2batcherrno -- kind exists x x)" = "q2batch 25" ] || fail "q2d-batch-enotty-$fs"
    [ "$($BIN/query $LAYER/da q2batcherrno -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x)" = "q2batch 22" ] || fail "q2d-batch-nine-$fs"
    [ "$($BIN/query $LAYER/da q2batcherrno -- kind exists x x -- kind eq? string note)" = "q2batch 22" ] || fail "q2d-batch-one-invalid-$fs"
    echo "QUERY2_MISSING_BATCH_PASS: $fs"
    set +f
    chmod 0700 $LAYER/da/sub || fail "q2c-fallback-$fs"
    q2ord leap-fallback 5 --limit 2 --pages ns eq string n1 kind eq string note -- $LOWER/da ns eq string n1 kind eq string note
    set -f; q2batchcmp fallback; set +f
    echo "QUERY2_LEAPFROG_PASS: $fs"
    # QUERY2 under concurrency (phase Q2e, spec test 8): mode changes, renames, unlink and
    # recreate, files moving to another domain and back, and chmod of the domain directory
    # itself (which switches the domain to fallback and closes it for a moment), racing for
    # 5 s with paged QUERY2 as uid 1001 over the plain, equality and ordered plans. Answers
    # must stay well formed and every result openable unless changed after the answer; the
    # final answer must equal the oracle, paged or not.
    mkdir -p $LAYER/dr $LAYER/dr2 && chmod 0755 $LAYER/dr $LAYER/dr2 || fail "q2e-mkdir-$fs"
    q2e_new() {   # $1: file index
        f=$LAYER/dr/r$1.md
        echo "r$1" > $f && $BIN/moobfs-attr set $f kind string note >/dev/null &&
            $BIN/moobfs-attr set $f ns string r >/dev/null &&
            $BIN/moobfs-attr set $f created time $(( $1 * 1000 )) >/dev/null && chmod 0644 $f
    }
    for i in $(seq 1 40); do q2e_new $i || fail "q2e-attr-$fs"; done
    umount $LAYER && mount -t moobfs_layer -o lower=$LOWER none $LAYER || fail "q2e-remount-$fs"
    $BIN/query $LAYER info | grep -q 'fallback_domains=0' || fail "q2e-fallback-start-$fs"
    : > /tmp/q2e.go
    ( n=0; while [ -e /tmp/q2e.go ]; do
          i=$(( n % 40 + 1 )); n=$(( n + 1 ))
          chmod 0600 $LAYER/dr/r$i.md 2>/dev/null; chmod 0644 $LAYER/dr/r$i.md 2>/dev/null
          mv $LAYER/dr/r$i.md $LAYER/dr/t$i.md 2>/dev/null && mv $LAYER/dr/t$i.md $LAYER/dr/r$i.md 2>/dev/null
          rm -f $LAYER/dr/r$i.md; q2e_new $i 2>/dev/null
          [ $(( n % 5 )) -eq 0 ] && { mv $LAYER/dr/r$i.md $LAYER/dr2/r$i.md 2>/dev/null; mv $LAYER/dr2/r$i.md $LAYER/dr/r$i.md 2>/dev/null; }
          [ $(( n % 7 )) -eq 0 ] && { chmod 0711 $LAYER/dr; chmod 0755 $LAYER/dr; }
      done ) &
    racer=$!
    $BIN/query --uid 1001 $LAYER/dr race2 5 | tee /tmp/q2e.txt || { rm -f /tmp/q2e.go; wait $racer; fail "q2e-race-$fs"; }
    rm -f /tmp/q2e.go; wait $racer
    grep -q '^race2: [1-9]' /tmp/q2e.txt || fail "q2e-no-iterations-$fs"
    # a rename or move cut by the end of the race may leave t<i>.md behind or r<i>.md in dr2
    chmod 0755 $LAYER/dr && rm -f $LAYER/dr/t*.md $LAYER/dr2/*.md || fail "q2e-tidy-$fs"
    for i in $(seq 1 40); do [ -e $LAYER/dr/r$i.md ] || q2e_new $i || fail "q2e-restore-$fs"; done
    chmod 0644 $LAYER/dr/*.md || fail "q2e-final-mode-$fs"
    $BIN/query $LAYER info | grep -q 'fallback_domains=0' && fail "q2e-no-fallback-$fs"   # the race must have switched it
    $BIN/query --uid 1001 $LAYER/dr q2 kind eq string note > /tmp/k.txt || fail "q2e-final-query-$fs"
    $BIN/moobfs-index --domain $LOWER/dr kind eq string note > /tmp/o.txt || fail "q2e-final-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt || { diff /tmp/k.txt /tmp/o.txt; fail "q2e-final-mismatch-$fs"; }
    [ "$(wc -l < /tmp/k.txt)" -eq 40 ] || fail "q2e-final-count-$fs"
    $BIN/query --uid 1001 $LAYER/dr q2 --limit 3 --pages kind eq string note > /tmp/k2.txt || fail "q2e-final-pages-$fs"
    cmp -s /tmp/k.txt /tmp/k2.txt || fail "q2e-final-pages-mismatch-$fs"
    rm -rf $LAYER/dr $LAYER/dr2 || fail "q2e-cleanup-$fs"
    echo "QUERY2_RACE_PASS: $fs"
    echo "kind eq string note" > /tmp/q2q.txt
    $BIN/bench-load --workload W3 --target $LAYER/da --root $LAYER/da --query2 1 --uid 1001 --queries /tmp/q2q.txt --repeat 3 --out /tmp/q2w3 || fail "q2-bench-w3-$fs"
    rm -rf $LAYER/da $LAYER/db $LAYER/dshared || fail "q2-cleanup-$fs"
    umount $LAYER && mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail "q2-restore-$fs"
    echo "QUERY2_PASS: $fs"
    # mount contract: idmapped mounts and user-namespace mounts are refused by the VFS
    $BIN/idmap idmap $LAYER || fail "contract-idmap-$fs"
    mkdir -p /tmp/userns-mnt && $BIN/idmap userns $LOWER /tmp/userns-mnt || fail "contract-userns-$fs"
    # concurrency: chmod, rename and unlink+recreate racing with QUERY as uid 1001 for 5 s;
    # answers must stay well formed, every result openable unless changed after the answer,
    # and the final answer must equal the oracle
    mkdir -p $LAYER/ns/race && chmod 0755 $LAYER/ns/race || fail "race-mkdir-$fs"
    for i in $(seq 1 40); do echo r$i > $LAYER/ns/race/r$i.md; $BIN/moobfs-attr set $LAYER/ns/race/r$i.md kind string note >/dev/null || fail "race-attr-$fs"; done
    chmod 0644 $LAYER/ns/race/*.md || fail "race-mode-$fs"
    : > /tmp/race.go
    ( n=0; while [ -e /tmp/race.go ]; do
          i=$(( n % 40 + 1 )); n=$(( n + 1 ))
          chmod 0600 $LAYER/ns/race/r$i.md 2>/dev/null; chmod 0644 $LAYER/ns/race/r$i.md 2>/dev/null
          mv $LAYER/ns/race/r$i.md $LAYER/ns/race/t$i.md 2>/dev/null && mv $LAYER/ns/race/t$i.md $LAYER/ns/race/r$i.md 2>/dev/null
          rm -f $LAYER/ns/race/r$i.md; echo r$i > $LAYER/ns/race/r$i.md; $BIN/moobfs-attr set $LAYER/ns/race/r$i.md kind string note >/dev/null 2>&1
          [ $(( n % 7 )) -eq 0 ] && { mv $LAYER/ns/race $LAYER/ns/race2 2>/dev/null; mv $LAYER/ns/race2 $LAYER/ns/race 2>/dev/null; }
      done ) &
    racer=$!
    $BIN/query --uid 1001 $LAYER race 5 | tee /tmp/race.txt || { rm -f /tmp/race.go; wait $racer; fail "race-query-$fs"; }
    rm -f /tmp/race.go; wait $racer
    grep -q '^race: [1-9]' /tmp/race.txt || fail "race-no-iterations-$fs"
    chmod 0644 $LAYER/ns/race/*.md || fail "race-final-mode-$fs"
    $BIN/query --uid 1001 $LAYER kind eq string note > /tmp/k.txt || fail "race-final-query-$fs"
    $BIN/moobfs-index --uid 1001 $LOWER kind eq string note > /tmp/o.txt || fail "race-final-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt || { diff /tmp/k.txt /tmp/o.txt; fail "race-final-mismatch-$fs"; }
    rm -rf $LAYER/ns || fail "iso-cleanup-$fs"
    echo "ISOLATION_PASS: $fs"
    # M4 harness over the layer (correctness, no numbers): W1 creates a profile-16 dataset,
    # W2 reads it, every W3 shape agrees with the oracle, W3 and W4 record one entry per call
    mkdir -p $LAYER/bench && chmod 0755 $LAYER/bench || fail "bench-mkdir-$fs"
    $BIN/bench-gen --seed 11 --inodes 300 --profile 16 --out /tmp/bench-m.txt || fail "bench-gen-$fs"
    $BIN/bench-load --manifest /tmp/bench-m.txt --target $LAYER/bench --workload W1 --procs 2 --out /tmp/bench-w1 || fail "bench-w1-$fs"
    [ "$(find $LAYER/bench -type f -name '*.md' | wc -l)" -eq 300 ] || fail "bench-w1-count-$fs"
    $BIN/moobfs-check $LOWER/bench > /tmp/bench-check.json || { cat /tmp/bench-check.json; fail "bench-check-$fs"; }
    bench_attrs=$(grep -v '^#' /tmp/bench-m.txt | awk -F'\t' '{ n += split($5, a, " ") } END { print n }')
    grep -q "\"typed_attributes\": $bench_attrs" /tmp/bench-check.json || fail "bench-check-attrs-$fs"
    $BIN/bench-load --manifest /tmp/bench-m.txt --target $LAYER/bench --workload W2 --procs 2 --out /tmp/bench-w2 || fail "bench-w2-$fs"
    grep -v '^#' $BIN/bench-queries-w3.txt > /tmp/bench-q.txt || fail "bench-queries-$fs"
    nq=0
    while read -r qline; do
        # shellcheck disable=SC2086
        same_query $qline || fail "bench-w3-equiv-$nq-$fs"
        nq=$((nq + 1))
    done < /tmp/bench-q.txt
    [ "$nq" -eq 9 ] || fail "bench-w3-shapes-$fs"
    $BIN/bench-load --queries /tmp/bench-q.txt --target $LAYER/bench --root $LAYER --workload W3 --out /tmp/bench-w3 || fail "bench-w3-$fs"
    # SQLite baseline next to the files: plan gate (SEARCH, never SCAN) and three-way equivalence
    rm -f /tmp/bench-base.db*
    SQLITE=$TOOLS/sqlite3 sh $BIN/bench-sqlbase.sh init /tmp/bench-base.db || fail "bench-sqlbase-init-$fs"
    SQLITE=$TOOLS/sqlite3 sh $BIN/bench-sqlbase.sh load /tmp/bench-base.db /tmp/bench-m.txt $LAYER/bench || fail "bench-sqlbase-load-$fs"
    env SQLITE=$TOOLS/sqlite3 SQLBASE=$BIN/bench-sqlbase.sh BIN=$BIN sh $BIN/bench-equiv.sh $BIN/bench-queries-w3.txt /tmp/bench-base.db $LOWER/bench $LAYER | tee /tmp/bench-equiv.txt | tail -1 | grep -q '^EQUIV_PASS: 9 shapes (kernel' || { cat /tmp/bench-equiv.txt; fail "bench-equiv-$fs"; }
    tail -1 /tmp/bench-equiv.txt
    $BIN/bench-load --manifest /tmp/bench-m.txt --target $LAYER/bench --workload W4 --out /tmp/bench-w4 || fail "bench-w4-$fs"
    [ "$(cat /tmp/bench-w1.*.rec /tmp/bench-w2.*.rec /tmp/bench-w3.*.rec /tmp/bench-w4.*.rec | wc -c)" -eq $(( (300 + 300 + 9 + 600) * 32 )) ] || fail "bench-records-$fs"
    # W8: mixed reads, attribute updates and queries from 10 processes; every call recorded
    $BIN/bench-load --manifest /tmp/bench-m.txt --target $LAYER/bench --root $LAYER --queries /tmp/bench-q.txt --workload W8 --procs 10 --out /tmp/bench-w8 || fail "bench-w8-$fs"
    [ "$(cat /tmp/bench-w8.*.rec | wc -c)" -ge $(( 300 * 32 )) ] || fail "bench-w8-records-$fs"
    # W5: three mutators renaming and recreating every memory while process 0 cycles the queries;
    # afterwards the tree must be whole and every W3 shape must still equal the oracle
    $BIN/bench-load --manifest /tmp/bench-m.txt --target $LAYER/bench --root $LAYER --queries /tmp/bench-q.txt --workload W5 --procs 4 --out /tmp/bench-w5 || fail "bench-w5-$fs"
    [ "$(find $LAYER/bench -type f -name '*.md' | wc -l)" -eq 300 ] || fail "bench-w5-count-$fs"
    find $LAYER/bench -name '*.away' | grep -q . && fail "bench-w5-leftover-$fs"
    $BIN/moobfs-check $LOWER/bench > /tmp/bench-check.json || { cat /tmp/bench-check.json; fail "bench-w5-check-$fs"; }
    while read -r qline; do
        # shellcheck disable=SC2086
        same_query $qline || fail "bench-w5-equiv-$fs"
    done < /tmp/bench-q.txt
    # W6 (cold: page cache dropped) and W7 (warm): remount and first QUERY; the layer must be mounted again afterwards
    $BIN/bench-load --workload W6 --target $LAYER --lower $LOWER --mount-opts domain_depth=0 --queries /tmp/bench-q.txt --out /tmp/bench-w6 --repeat 2 || fail "bench-w6-$fs"
    $BIN/bench-load --workload W7 --target $LAYER --lower $LOWER --mount-opts domain_depth=0 --queries /tmp/bench-q.txt --out /tmp/bench-w7 --repeat 2 || fail "bench-w7-$fs"
    [ "$(cat /tmp/bench-w6.0.rec /tmp/bench-w7.0.rec | wc -c)" -eq $(( 4 * 32 )) ] || fail "bench-mount-records-$fs"
    grep -q "moobfs_layer $LAYER\| $LAYER " /proc/mounts || fail "bench-remounted-$fs"
    # after the remount: index on, paths current, index complete (flag bits 0x2, 0x4 and 0x8 clear;
    # root also gets MOOB_FLAG_COUNTERS 0x20, ADR-0011)
    f=$($BIN/query $LAYER info | sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p')
    [ -n "$f" ] && [ $(( f & 0xe )) -eq 0 ] || fail "bench-remount-flags-$fs"
    rm -rf $LAYER/bench /tmp/bench-* || fail "bench-cleanup-$fs"
    # the campaign orchestrator, one repetition at a tiny scale: procedure only, never numbers
    env BIN=$BIN SQLITE=$TOOLS/sqlite3 sh $BIN/bench-run.sh --out /tmp/bench-run --scale 60 --profile 16 --reps 1 --procs 2 --layer "C-$fs=$LAYER:$LOWER" > /tmp/bench-run.log 2>&1 || { tail -20 /tmp/bench-run.log; fail "bench-run-$fs"; }
    grep -q '^BENCH_RUN_DONE\|BENCH_RUN_DONE' /tmp/bench-run.log || fail "bench-run-done-$fs"
    [ -s /tmp/bench-run/environment.txt ] && [ -s "/tmp/bench-run/rep1/C-$fs/w3.0.rec" ] && [ -s "/tmp/bench-run/rep1/C-$fs/equiv.txt" ] || fail "bench-run-artifacts-$fs"
    rm -rf /tmp/bench-run /tmp/bench-run.log $LAYER/bench
    echo "BENCH_LAYER_PASS: $fs"
    mkdir -p $LAYER/fsx
    $BIN/fsx -q -N 5000 -S 1 $LAYER/fsx/file1 || fail "fsx-1-$fs"
    $BIN/fsx -q -N 3000 -S 2 -r 512 -w 512 $LAYER/fsx/file2 || fail "fsx-2-$fs"
    $BIN/fsx -q -N 1000 -S 3 -l 2000000 $LAYER/fsx/file3 || fail "fsx-large-$fs"
    cmp $LAYER/fsx/file1 $LOWER/fsx/file1 || fail "fsx-lower-mismatch-$fs"
    rm -rf $LAYER/fsx || fail "layer-cleanup-$fs"
    sync
    umount $LAYER || fail "layer-unmount-$fs"
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0,nolinks none $LAYER || fail "layer-remount-$fs"
    $BIN/layer $LAYER $LOWER "$reflink" nolinks || fail "layer-nolinks-tests-$fs"
    umount $LAYER || fail "layer-second-unmount-$fs"
    umount $LOWER || fail "lower-unmount-$fs"
    echo "LAYER_LOWER_PASS: $fs reflink=$reflink"
}
run_lower ext4 "$DEV_EXT4" no
run_lower btrfs "$DEV_BTRFS" yes
run_lower xfs "$DEV_XFS" yes

# Readable demonstration, quoted in the README: typed attributes written through the layer,
# QUERY2 on domain directories under the default mount options, results filtered by permission.
# Lines starting with "DEMO| " are each command as run (with $BIN on PATH) and its output.
demo() {
    printf 'DEMO| # %s\n' "$*"
    ( PATH=$BIN:$PATH; eval "$*" ) > /tmp/demo.out 2>&1; rc=$?
    sed 's/^/DEMO| /' /tmp/demo.out
    return $rc
}
mount -t ext4 "$DEV_EXT4" $LOWER || fail demo-lower-mount
mkdir $LOWER/alice $LOWER/bob $LOWER/team || fail demo-mkdir
echo 'Cache: keep SQLite next to the files' > $LOWER/alice/cache.md || fail demo-write
echo 'Deploys are frozen on Fridays' > $LOWER/bob/deploy.md || fail demo-write
echo 'C code follows the kernel style' > $LOWER/team/style.md || fail demo-write
echo 'Notes for the salary review' > $LOWER/team/salary.md || fail demo-write
chown -R 1001:1001 $LOWER/alice && chmod 0700 $LOWER/alice && chmod 0600 $LOWER/alice/cache.md || fail demo-alice
chown -R 1002:1002 $LOWER/bob && chmod 0700 $LOWER/bob && chmod 0600 $LOWER/bob/deploy.md || fail demo-bob
chmod 0755 $LOWER/team && chmod 0644 $LOWER/team/style.md || fail demo-team
chown 1002:1002 $LOWER/team/salary.md && chmod 0600 $LOWER/team/salary.md || fail demo-salary
echo "DEMO| # (prepared on the lower ext4: alice/ uid 1001 0700, bob/ uid 1002 0700, team/ 0755)"
demo "mount -t moobfs_layer -o lower=$LOWER none $LAYER" || fail demo-mount
for f in alice/cache.md bob/deploy.md team/style.md team/salary.md; do
    demo "moobfs-attr set $LAYER/$f kind string decision" || fail "demo-attr-$f"
done
demo "moobfs-attr list $LOWER/alice/cache.md" || fail demo-attr-lower
grep -q "^kind	string	decision$" /tmp/demo.out || fail demo-attr-lower-content
demo "stat -c '%A %u %n' $LAYER/team/*" || fail demo-stat
demo "query --uid 1001 $LAYER/alice q2 kind eq string decision" || fail demo-q2-alice
[ "$(wc -l < /tmp/demo.out)" -eq 1 ] && grep -q '	cache.md$' /tmp/demo.out || fail demo-q2-alice-content
demo "query --uid 1001 $LAYER/team q2 kind eq string decision" || fail demo-q2-team
[ "$(wc -l < /tmp/demo.out)" -eq 1 ] && grep -q '	style.md$' /tmp/demo.out || fail demo-q2-team-content
demo "query --uid 1001 $LAYER/bob q2 kind eq string decision" && fail demo-q2-bob-answered
grep -q 'Permission denied' /tmp/demo.out || fail demo-q2-bob-errno
demo "query --uid 1001 $LAYER kind eq string decision" && fail demo-v1-user-answered
grep -q 'Operation not permitted' /tmp/demo.out || fail demo-v1-user-errno
demo "query $LAYER kind eq string decision" || fail demo-v1-root
[ "$(wc -l < /tmp/demo.out)" -eq 4 ] || fail demo-v1-root-count
demo "query $LAYER info" || fail demo-info
grep -q 'domain_depth=1 heterogeneous=1 fallback_domains=0' /tmp/demo.out || fail demo-info-counts
# ADR-0011 F1: the index counters sum every user's memories; root gets them, a user gets zeros
grep -q 'counters=shown' /tmp/demo.out || fail demo-info-root-counters
demo "query --uid 1001 $LAYER info" || fail demo-info-user
grep -q 'domain_depth=1 heterogeneous=0 fallback_domains=0 counters=hidden' /tmp/demo.out || fail demo-info-user-hidden
grep -q ' used=0 entries=0 inodes=0$' /tmp/demo.out || fail demo-info-user-counters
umount $LAYER || fail demo-umount
rm -rf $LOWER/alice $LOWER/bob $LOWER/team /tmp/demo.out || fail demo-cleanup
umount $LOWER || fail demo-lower-umount
echo "DEMO_PASS: QUERY2 per domain, v1 refused to a user, heterogeneous file left out"

# Phase 2e: acceptance with clients that do not know MoobFS (git, SQLite), an own memory
# client, power loss under dm-log-writes and I/O errors under dm-flakey.
export PATH=$TOOLS:/bin:/usr/bin:/sbin:/usr/sbin
export HOME=/tmp GIT_CONFIG_NOSYSTEM=1 GIT_EXEC_PATH=/usr/lib/git-core GIT_TEMPLATE_DIR=/nonexistent
export DM_DISABLE_UDEV=1
remember() {   # remember <mount> <relative path> <kind> <score> : the client convention of spec-m2 §6
    tmp="$1/.tmp.$$"
    printf '# %s\n\nmemory %s\n' "$2" "$2" > "$tmp" || return 1
    $BIN/moobfs-attr set "$tmp" kind string "$3" || return 1
    $BIN/moobfs-attr set "$tmp" score int64 "$4" || return 1
    $BIN/moobfs-attr set "$tmp" created time 1760000000000000000 || return 1
    mkdir -p "$(dirname "$1/$2")" || return 1
    sync "$tmp" 2>/dev/null || sync
    mv "$tmp" "$1/$2" || return 1
    sync    # a rename is durable only after the parent directory is synced; clients must fsync it
}
mount -t ext4 "$DEV_EXT4" $LOWER || fail acc-lower-mount
mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail acc-layer-mount
# git: a repository living on the layer, with memories as tracked files
mkdir -p $LAYER/repo && cd $LAYER/repo || fail acc-repo-dir
git init -q . 2>/dev/null || fail acc-git-init
for i in 1 2 3; do
    remember $LAYER/repo "notes/n$i.md" note $((i * 7)) || fail "acc-remember-$i"
    git add -A || fail "acc-git-add-$i"
    git -c user.name=moob -c user.email=moob@test commit -q -m "memory $i" || fail "acc-git-commit-$i"
done
[ "$(git log --oneline | wc -l)" -eq 3 ] || fail acc-git-log
[ -z "$(git status --porcelain)" ] || fail acc-git-status
git fsck --strict --no-progress 2>&1 | grep -v '^$' && fail acc-git-fsck
git cat-file -p HEAD:notes/n2.md | grep -q 'memory notes/n2.md' || fail acc-git-content
cd /
$BIN/query $LAYER kind eq string note score gt int64 10 > /tmp/k.txt || fail acc-query
[ "$(wc -l < /tmp/k.txt)" -eq 2 ] || fail acc-query-count
# SQLite in WAL mode: a writer and a reader at the same time, then integrity_check.
# Run as a controlled experiment: directly on the lower fs (control), on the layer without a
# concurrent reader, and on the layer with one. Counts are printed before any verdict.
sq() { sqlite3 -cmd ".timeout 10000" "$@"; }   # concurrent CLI processes wait for fcntl locks instead of failing
sqlite_run() {   # sqlite_run <writer db path> <reader db path or -> <mode wal|delete> <label> [vfs]
    DB=$1; RDB=$2; MODE=$3
    VFS=${5:-}
    if [ -n "$VFS" ]; then sq() { sqlite3 -vfs "$VFS" -cmd ".timeout 10000" "$@"; }; else sq() { sqlite3 -cmd ".timeout 10000" "$@"; }; fi
    rm -f "$DB" "$DB-wal" "$DB-shm" "$DB-journal"
    sq "$DB" "PRAGMA journal_mode=$MODE; CREATE TABLE memories(id INTEGER PRIMARY KEY, path TEXT, score INT);" | grep -qi "$MODE" || fail "sqlite-mode-$4"
    ( for i in $(seq 1 200); do sq "$DB" "INSERT INTO memories(path, score) VALUES('p$i', $i);" || { echo "writer failed at $i ($4)"; exit 1; }; done ) &
    writer=$!
    reads=0
    if [ "$RDB" != - ]; then
        while kill -0 $writer 2>/dev/null; do sq "$RDB" 'SELECT count(*) FROM memories;' > /dev/null || echo "reader error ($4)"; reads=$((reads + 1)); done
    fi
    wait $writer || echo "writer exit code $? ($4)"
    count=$(sq "$DB" 'SELECT count(*) FROM memories;')
    integrity=$(sq "$DB" 'PRAGMA integrity_check;')
    missing=$(sq "$DB" "WITH RECURSIVE s(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM s WHERE x<200) SELECT group_concat(x) FROM s WHERE x NOT IN (SELECT score FROM memories);")
    echo "SQLITE_RESULT $4: rows=$count integrity=$integrity reads=$reads missing=[$missing]"
    [ "$count" = 200 ]
}
sqlite_run $LOWER/c1.db $LOWER/c1.db wal lower-wal; r1=$?
sqlite_run $LAYER/a1.db - wal layer-wal-alone; r2=$?
sqlite_run $LAYER/m1.db $LAYER/m1.db wal layer-wal; r3=$?
sqlite_run $LAYER/m2.db $LAYER/m2.db delete layer-rollback; r4=$?
sqlite_run $LAYER/m3.db $LAYER/m3.db wal layer-wal-unix-excl unix-excl; r5=$?
sqlite_run $LAYER/m5.db $LAYER/m5.db wal layer-wal-again; r6=$?
echo "SQLITE_VARIANTS: lower=$r1 alone=$r2 layer=$r3 rollback=$r4 excl=$r5 again=$r6"
[ $r1 -eq 0 ] || fail acc-sqlite-control
[ $r2 -eq 0 ] || fail acc-sqlite-layer-alone
[ $r3 -eq 0 ] && [ $r6 -eq 0 ] || fail acc-sqlite-layer-reader
echo "SQLITE_CONCURRENT_OK"
$BIN/moobfs-check $LOWER > /dev/null || fail acc-check
umount $LAYER || fail acc-layer-umount
umount $LOWER || fail acc-lower-umount
echo "ACCEPTANCE_CLIENTS_PASS"
# Power loss: format and use a device under dm-log-writes, then replay the log to each mark
# on a zeroed disk and check that every memory completed before the mark is whole.
SCRATCH=$DEV_SCRATCH; LOGDEV=$DEV_LOG
dd if=/dev/zero of=$SCRATCH bs=1M count=64 2>/dev/null || fail lw-zero
$TOOLS/dmsetup create lw --table "0 $(blockdev --getsz $SCRATCH) log-writes $SCRATCH $LOGDEV" || fail lw-create
$TOOLS/mke2fs -q -F -t ext4 -b 4096 /dev/mapper/lw || fail lw-mkfs
$TOOLS/dmsetup message lw 0 mark mkfs || fail lw-mark-mkfs
mount -t ext4 /dev/mapper/lw $LOWER || fail lw-lower-mount
mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail lw-layer-mount
for i in 1 2 3 4; do
    remember $LAYER "mem/m$i.md" decision $i || fail "lw-remember-$i"
    $TOOLS/dmsetup message lw 0 mark "m$i" || fail "lw-mark-$i"
done
umount $LAYER || fail lw-layer-umount
umount $LOWER || fail lw-lower-umount
$TOOLS/dmsetup message lw 0 mark end || fail lw-mark-end
$TOOLS/dmsetup remove --retry lw || fail lw-remove
check_mark() {   # replay up to a mark; memories 1..$2 must be whole, later ones absent
    dd if=/dev/zero of=$SCRATCH bs=1M count=64 2>/dev/null || return 1
    $BIN/replay-log --log $LOGDEV --replay $SCRATCH --end-mark "$1" || return 1
    mount -t ext4 $SCRATCH $LOWER && umount $LOWER || return 1      # journal recovery, as after a reboot
    $TOOLS/e2fsck -fn $SCRATCH > /tmp/fsck.txt 2>&1 || { cat /tmp/fsck.txt; return 1; }
    mount -t ext4 -o ro $SCRATCH $LOWER || return 1
    mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || return 1
    n=0
    for i in 1 2 3 4; do
        if [ "$i" -le "$2" ]; then
            grep -q "memory mem/m$i.md" "$LAYER/mem/m$i.md" || return 1
            [ "$($BIN/moobfs-attr get $LAYER/mem/m$i.md score)" = "$(printf 'score\tint64\t%s' $i)" ] || return 1
        else
            [ ! -e "$LAYER/mem/m$i.md" ] || return 1
        fi
    done
    $BIN/query $LAYER kind eq string decision > /tmp/k.txt || return 1
    [ "$(wc -l < /tmp/k.txt)" -eq "$2" ] || return 1
    for leftover in "$LOWER"/.tmp.*; do [ -e "$leftover" ] && echo "note: a temporary file survived the crash at $1 (allowed)"; break; done
    umount $LAYER && umount $LOWER
}
check_mark mkfs 0 || fail lw-check-mkfs
check_mark m1 1 || fail lw-check-m1
check_mark m2 2 || fail lw-check-m2
check_mark m4 4 || fail lw-check-m4
check_mark end 4 || fail lw-check-end
echo "POWER_LOSS_REPLAY_PASS: marks mkfs m1 m2 m4 end"
# I/O errors: the lower device starts erroring every write; errors must reach the caller,
# nothing may crash, and the tree must come back clean once the device is healthy again.
dd if=/dev/zero of=$SCRATCH bs=1M count=64 2>/dev/null || fail fl-zero
$TOOLS/mke2fs -q -F -t ext4 -b 4096 $SCRATCH || fail fl-mkfs
SZ=$(blockdev --getsz $SCRATCH)
$TOOLS/dmsetup create fl --table "0 $SZ linear $SCRATCH 0" || fail fl-create
mount -t ext4 /dev/mapper/fl $LOWER || fail fl-lower-mount
mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail fl-layer-mount
remember $LAYER "ok.md" note 1 || fail fl-remember-ok
sync
$TOOLS/dmsetup suspend fl && dmsetup reload fl --table "0 $SZ flakey $SCRATCH 0 0 1 1 error_writes" && dmsetup resume fl || fail fl-reload-down
remember $LAYER "lost.md" note 2 2>/dev/null && sync && echo "note: writes were absorbed by the page cache before the device reported errors"
sync 2>/dev/null
$TOOLS/dmsetup suspend --noflush --nolockfs fl && dmsetup reload fl --table "0 $SZ linear $SCRATCH 0" && dmsetup resume fl || fail fl-reload-up
umount $LAYER 2>/dev/null || true
umount $LOWER 2>/dev/null || true
$TOOLS/dmsetup remove --retry fl || fail fl-remove
$TOOLS/e2fsck -fy $SCRATCH > /tmp/fsck.txt 2>&1; rc=$?; [ $rc -le 1 ] || { cat /tmp/fsck.txt; fail fl-fsck; }
mount -t ext4 $SCRATCH $LOWER || fail fl-remount
mount -t moobfs_layer -o lower=$LOWER,domain_depth=0 none $LAYER || fail fl-layer-remount
grep -q 'memory ok.md' $LAYER/ok.md || fail fl-ok-content
$BIN/moobfs-check $LOWER > /dev/null || fail fl-check
umount $LAYER && umount $LOWER || fail fl-final-umount
echo "IO_ERROR_INJECTION_PASS"
rmmod moobfs_layer || fail layer-unload
if dmesg | grep -E 'BUG:|Oops:|WARNING:|Kernel panic|KASAN:'; then fail kernel-diagnostic-layer; fi
echo "MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey"
echo "MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; layer tests; typed attributes and tools; index and QUERY equal to the oracle; fsx 9000 ops each; reflink on btrfs/xfs; nolinks; unload"
