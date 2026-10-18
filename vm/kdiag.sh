#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Kernel diagnostics in a dmesg capture, for the VM harnesses (sourced, defines kdiag).
#
# kdiag FILE REGEX: prints every line of FILE matching the extended REGEX and returns 1 if
# there was one. KCSAN reports are judged apart (journal 0115): one counts, is printed and
# fails only when one of its racing accesses is in moobfs_layer, that is when the top frame of
# a "read"/"write" stack of the report is a function of the module. The others are races of
# the kernel itself (virtio, timers, the dcache, the ext4 xattr cache), reached through the
# layer or not; they are counted on one KCSAN_OUTSIDE line and do not fail. Without KCSAN in
# the kernel there are no such reports and this is the plain grep.
# The timestamp is removed before REGEX is tried, so a kernel warning is matched as ^WARNING:
# (with KVM the boot prints "Speculative Return Stack Overflow: WARNING: ...", not a warning).
kdiag() {
    awk -v re="$2" '
    { sub(/\r$/, ""); sub(/^\[ *[0-9.]+\] /, "") }
    /BUG: KCSAN:/ { ink = 1; layer = 0; via = 0; want = 0; title = $0; next }
    ink && /^=+$/ {
        if (layer) { print title " (racing access in moobfs_layer)"; bad = 1 }
        else { out++; if (via) outvia++ }
        ink = 0; next
    }
    ink {
        if ($0 ~ /^(read|write).* to 0x/ || $0 ~ /race at unknown origin/) { want = 1; next }
        if (want && $0 ~ /^ [A-Za-z_][A-Za-z0-9_.]*\+0x/) {
            if ($0 ~ /\[moobfs_layer\]/) layer = 1
            want = 0
        }
        if ($0 ~ /\[moobfs_layer\]/) via = 1
        next
    }
    $0 ~ re { print; bad = 1 }
    END {
        if (ink && layer) { print title " (racing access in moobfs_layer, report cut)"; bad = 1 }
        else if (ink) { out++; if (via) outvia++ }
        if (out)
            printf "KCSAN_OUTSIDE: %d report(s) with no racing access in moobfs_layer (%d reached through it)\n", out, outvia + 0
        exit bad
    }' "$1"
}
