/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tracepoints of the index (security roadmap S13, journal 0102). They show what ADR-0012 made
 * invisible: when a domain is built and what it cost, when one is released and why, when a file
 * being read by a build is read again, and when the index is marked incomplete. tracefs is
 * readable by root only, which keeps these counts as private as the GET_INFO counters
 * (ADR-0011, finding F1). Not a stable interface.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM moobfs

#if !defined(_MOOBL_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MOOBL_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(moobfs_domain_build,
	TP_PROTO(dev_t dev, u64 domain, int err, unsigned int tries, unsigned int released,
		 s64 bytes, u64 ns),
	TP_ARGS(dev, domain, err, tries, released, bytes, ns),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, domain)
		__field(int, err)
		__field(unsigned int, tries)
		__field(unsigned int, released)
		__field(s64, bytes)
		__field(u64, ns)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->domain = domain;
		__entry->err = err;
		__entry->tries = tries;
		__entry->released = released;
		__entry->bytes = bytes;
		__entry->ns = ns;
	),
	TP_printk("dev=%u:%u domain=%llu err=%d tries=%u released=%u bytes=%lld ns=%llu",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->domain, __entry->err,
		  __entry->tries, __entry->released, __entry->bytes, __entry->ns)
);

TRACE_EVENT(moobfs_domain_release,
	TP_PROTO(dev_t dev, u64 domain, bool reclaim, s64 bytes),
	TP_ARGS(dev, domain, reclaim, bytes),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, domain)
		__field(bool, reclaim)
		__field(s64, bytes)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->domain = domain;
		__entry->reclaim = reclaim;
		__entry->bytes = bytes;
	),
	TP_printk("dev=%u:%u domain=%llu by=%s bytes=%lld",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->domain,
		  __entry->reclaim ? "shrinker" : "budget", __entry->bytes)
);

TRACE_EVENT(moobfs_scan_reread,
	TP_PROTO(dev_t dev, u64 ino),
	TP_ARGS(dev, ino),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u64, ino)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->ino = ino;
	),
	TP_printk("dev=%u:%u ino=%llu", MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino)
);

TRACE_EVENT(moobfs_index_incomplete,
	TP_PROTO(dev_t dev, int err),
	TP_ARGS(dev, err),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(int, err)
	),
	TP_fast_assign(
		__entry->dev = dev;
		__entry->err = err;
	),
	TP_printk("dev=%u:%u err=%d", MAJOR(__entry->dev), MINOR(__entry->dev), __entry->err)
);

#endif /* _MOOBL_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE moobl_trace
#include <trace/define_trace.h>
