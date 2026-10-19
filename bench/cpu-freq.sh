#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# bench/cpu-freq: holds the CPU frequency still during a measurement (docs/benchmark-plan.md,
# "Method"). Dynamic frequency scaling and turbo change latencies from one repetition to the
# next and make a difference hard to explain.
#
#   sudo sh bench/cpu-freq.sh fix       # governor "performance" on every CPU, turbo off;
#                                       # the previous state is saved in $STATE
#   sudo sh bench/cpu-freq.sh restore   # puts the saved state back
#   sh bench/cpu-freq.sh show           # one line for environment.txt
#
# Uses the kernel's cpufreq sysfs files (Documentation/admin-guide/pm/cpufreq.rst): the
# per-CPU scaling_governor, and intel_pstate/no_turbo or cpufreq/boost, whichever exists
# (Documentation/admin-guide/pm/intel_pstate.rst). In a VM these files are usually absent:
# "show" says so, and "fix" fails, since the host's frequency is then out of reach.
set -eu
CPU=/sys/devices/system/cpu
STATE=${STATE:-/run/moob-cpufreq.state}

turbo_file() {
    if [ -e "$CPU/intel_pstate/no_turbo" ]; then echo "$CPU/intel_pstate/no_turbo"
    elif [ -e "$CPU/cpufreq/boost" ]; then echo "$CPU/cpufreq/boost"
    fi
}

show() {
    govs=$(cat "$CPU"/cpu[0-9]*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c | awk '{printf "%s%s x%s", sep, $2, $1; sep=", "}')
    t=$(turbo_file)
    case $t in
        */no_turbo) turbo="no_turbo=$(cat "$t")" ;;
        */boost) turbo="boost=$(cat "$t")" ;;
        *) turbo="turbo control absent" ;;
    esac
    echo "cpufreq: governors [${govs:-absent}] $turbo driver $(cat "$CPU/cpu0/cpufreq/scaling_driver" 2>/dev/null || echo absent)"
}

case ${1:-} in
    show) show ;;
    fix)
        [ "$(id -u)" = 0 ] || { echo "cpu-freq: run as root" >&2; exit 1; }
        ls "$CPU"/cpu[0-9]*/cpufreq/scaling_governor > /dev/null 2>&1 || { echo "cpu-freq: no cpufreq on this machine (a VM?)" >&2; exit 1; }
        [ -e "$STATE" ] && { echo "cpu-freq: $STATE exists; restore first" >&2; exit 1; }
        t=$(turbo_file)
        {
            for g in "$CPU"/cpu[0-9]*/cpufreq/scaling_governor; do echo "$g $(cat "$g")"; done
            if [ -n "$t" ]; then echo "$t $(cat "$t")"; fi
        } > "$STATE"
        for g in "$CPU"/cpu[0-9]*/cpufreq/scaling_governor; do echo performance > "$g"; done
        case $t in
            */no_turbo) echo 1 > "$t" ;;
            */boost) echo 0 > "$t" ;;
        esac
        show
        ;;
    restore)
        [ "$(id -u)" = 0 ] || { echo "cpu-freq: run as root" >&2; exit 1; }
        [ -e "$STATE" ] || { echo "cpu-freq: nothing saved in $STATE" >&2; exit 1; }
        while read -r f v; do echo "$v" > "$f"; done < "$STATE"
        rm -f "$STATE"
        show
        ;;
    *) echo "usage: see the header of bench/cpu-freq.sh" >&2; exit 64 ;;
esac
