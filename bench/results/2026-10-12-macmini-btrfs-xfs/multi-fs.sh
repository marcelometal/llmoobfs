#!/bin/sh
# Runs bench/q2-campaign.sh and bench/sqlite-baseline.sh on the btrfs and XFS test volumes
# (journal 0049). Run as root on the Mac mini host from ~/moobfs.
set -eu
P=/dev/disk/by-id/ata-OCZ-VERTEX4_OCZ-JB99W5Y904ONQVF5
R=/home/debian/results
for spec in btrfs:part2 xfs:part3; do
    fs=${spec%%:*}; part=${spec#*:}
    mkdir -p /srv/moob/$fs /srv/moob/layer-$fs
    findmnt /srv/moob/$fs > /dev/null || mount $P-$part /srv/moob/$fs
    [ "$(findmnt -no FSTYPE /srv/moob/$fs)" = "$fs" ] || { echo "multi-fs: /srv/moob/$fs is not $fs"; exit 1; }
    mkdir -p /srv/moob/$fs/root /srv/moob/$fs/sqlite
    findmnt /srv/moob/layer-$fs > /dev/null || mount -t moobfs_layer -o lower=/srv/moob/$fs/root none /srv/moob/layer-$fs
    echo "multi-fs: $(date -u +%H:%M:%S) $fs campaign"
    sh bench/q2-campaign.sh --layer /srv/moob/layer-$fs --lower /srv/moob/$fs/root --out $R/q2-campaign-$fs-2026-10-12
    echo "multi-fs: $(date -u +%H:%M:%S) $fs sqlite"
    printf '%s\n' "-- QUERY2 shapes in SQL (same as the ext4 run)" > $R/sqlite-extra-$fs.sql
    grep -v '^--' /home/debian/sqlite-extra.sql >> $R/sqlite-extra-$fs.sql
    sh bench/sqlite-baseline.sh --manifest $R/q2-campaign-$fs-2026-10-12/manifest.txt --target /srv/moob/$fs/root \
        --db /srv/moob/$fs/sqlite/base.db --queries $R/q2-campaign-$fs-2026-10-12/q-v1.txt \
        --extra-sql $R/sqlite-extra-$fs.sql --out $R/sqlite-baseline-$fs-2026-10-12
done
echo "MULTI_FS_DONE"
