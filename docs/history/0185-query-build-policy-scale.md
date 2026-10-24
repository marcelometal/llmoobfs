# 0185 — Query-build policy at larger domains and delayed lower I/O

Date: 2026-10-24. Status: investigation in progress.
Work started: 2026-10-24. End and labor time: not yet recorded.

## Goal and decision

After PR 135 merged the experimental guard, the maintainer chose to keep
issue 118 open and measure larger domains and slower lower storage before
changing the numerical policy or closing the issue. The motivation is the
legitimate 17,000-file domain denied just above the 32,768-work-unit cap:
the controlled comparison in journal 0182 saved only 3.370 ms of paired
median process CPU at that threshold, while a 60,000-page bulk client replay
covered ten domains of about 6,000 pages on one host. Neither result chooses
a general threshold or proves a strict CPU/I/O cap.

## Planned bounded experiment

Use a dedicated VM on the Mac mini's Debian 6.12.111 KVM host. Prepare a
fresh, uniquely named ext4 disk image, plant a specified count of empty
files with exclusive creation, and issue one unprivileged cold `QUERY2`.
Repeat on the same lower image through the kernel's `dm-delay` target, with
a documented read delay; unmount and request cache eviction before each
condition. Record the ioctl verdict, wall and process CPU time, task read
bytes and underlying block-device read sectors. The helper checks that a
denied call leaves its output untouched. Keep the disk, VM logs and host
configuration outside Git; version only the runner and factual summary.

This is a synthetic lower-I/O sensitivity probe, not a claim that a
particular HDD, network filesystem or cloud device was tested. Device
read-sector counts include other guest activity and are not an exact
per-query accounting source. A configured delay does not convert the
cooperative ten-second deadline into a hard CPU or I/O bound. Run a small
pilot before larger cases and refuse existing output directories or disk
images rather than overwrite them.

## Results and next step

Pending. Record setup failures and their fixes, exact executed commands,
guest verdict markers, resource observations and limits here at each round.
Only after the evidence is checked should the maintainer choose whether to
keep, raise or make the cap configurable. No policy change or issue closure
follows from beginning this experiment.
