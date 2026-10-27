# SPDX-License-Identifier: LGPL-2.1-or-later
"""Queries: terms, ordering, and MOOB_IOC_QUERY2 / QUERY2_BATCH on a domain directory
(docs/spec-query-v2.md section 4).

The matching rules here are the kernel's (layer/index.c: term_check, entry_matches,
value_cmp), so that the scan of moobfs.scan and the kernel give the same answers.
"""
import ctypes
import errno
import fcntl
import math
import os
import struct
from collections import namedtuple

from . import _uapi as u
from .attrs import TYPE_NAMES, Value, check_name, utf8_ok

OPS = {"==": u.OP_EQ, "<": u.OP_LT, "<=": u.OP_LE, ">": u.OP_GT, ">=": u.OP_GE,
       "prefix": u.OP_PREFIX, "exists": u.OP_EXISTS}
ORDER_TYPES = {"string": u.T_STRING, "int64": u.T_INT64, "double": u.T_DOUBLE, "time": u.T_TIME}
RESOURCE_ERRNO = frozenset({errno.ENOMEM, errno.E2BIG, errno.ETIMEDOUT, errno.EDQUOT})
UNAVAILABLE_ERRNO = frozenset({errno.ENOTTY, errno.ENODEV, errno.EOPNOTSUPP})

Match = namedtuple("Match", "path ino")
Match.__doc__ = """One answer: path relative to the domain directory (None when the kernel had no
name for the inode or the name was longer than 256 bytes), and inode number."""


class QueryRefused(OSError):
    """The kernel refused the query for a resource limit (ENOMEM, E2BIG, ETIMEDOUT, EDQUOT).
    No result of that call is returned. The scan gives the same answer at a higher cost."""


class KernelUnavailable(OSError):
    """The descriptor does not serve QUERY2: not on a layer (ENOTTY, ENODEV), or domains or
    the index are off (EOPNOTSUPP)."""


class Term:
    """One condition. op is one of ==, <, <=, >, >=, prefix, exists. missing_ok also accepts
    files without the attribute (MOOB_TERM_MISSING_OK); it cannot go with exists."""

    __slots__ = ("name", "op", "value", "missing_ok")

    def __init__(self, name, op, value=None, missing_ok=False):
        check_name(name)
        if op not in OPS:
            raise ValueError(f"unknown operator {op!r}")
        self.name, self.op, self.missing_ok = name, op, bool(missing_ok)
        if op == "exists":
            if value is not None or missing_ok:
                raise ValueError("exists takes no value and no missing_ok")
            self.value = None
            return
        v = Value.of(value)
        if v.type == u.T_RAW:
            raise ValueError("raw values are not indexed and cannot be queried")
        if op == "prefix" and v.type != u.T_STRING:
            raise ValueError("prefix needs a string")
        if v.type == u.T_STRING and (len(v.payload) > u.VALUE_MAX or not utf8_ok(v.payload)):
            raise ValueError("string term longer than 1024 bytes or not valid UTF-8")
        if v.type == u.T_DOUBLE and math.isnan(struct.unpack("<d", v.payload)[0]):
            raise ValueError("NaN cannot be queried")
        self.value = v

    def matches(self, stored):
        """entry_matches: stored is the file's Value for this name, or None when absent."""
        if stored is None:
            return self.missing_ok
        if self.op == "exists":
            return True
        v = self.value
        if stored.type != v.type:
            return False
        if self.op == "prefix":
            return stored.payload.startswith(v.payload)
        a, b = sort_key(stored), sort_key(v)
        return {"==": a == b, "<": a < b, "<=": a <= b, ">": a > b, ">=": a >= b}[self.op]

    def __repr__(self):
        extra = ", missing_ok=True" if self.missing_ok else ""
        return f"Term({self.name!r}, {self.op!r}, {self.value!r}{extra})"


class Order:
    """Order by one attribute: only entries of this type take part; ties by inode number, in
    the same direction."""

    __slots__ = ("name", "type", "descending")

    def __init__(self, name, type_, descending=False):
        check_name(name)
        if type_ not in ORDER_TYPES:
            raise ValueError(f"order type is one of {', '.join(ORDER_TYPES)}")
        self.name, self.type, self.descending = name, ORDER_TYPES[type_], bool(descending)


def sort_key(v):
    """value_cmp within one type: signed integers, IEEE 754 order for doubles (double_key: -0.0
    sorts before 0.0 and is not equal to it), bytes for strings."""
    if v.type in (u.T_INT64, u.T_TIME):
        return struct.unpack("<q", v.payload)[0]
    if v.type == u.T_DOUBLE:
        bits = struct.unpack("<Q", v.payload)[0]
        return (~bits & 0xFFFFFFFFFFFFFFFF) if bits >> 63 else bits | (1 << 63)
    if v.type == u.T_BOOL:
        return v.payload[0]
    return v.payload


def check_request(terms, order):
    if len(terms) > u.MAX_TERMS:
        raise ValueError("at most 4 terms")
    if not terms and order is None:
        raise ValueError("a query needs a term or an order")
    if terms and order is None and all(t.missing_ok for t in terms):
        raise ValueError("at least one term must be without missing_ok, unless ordered")


def _fill(q, terms, order, limit, results):
    q.version, q.nterms, q.limit, q.results = u.QUERY2_VERSION, len(terms), limit, results
    for i, t in enumerate(terms):
        c = q.terms[i]
        c.name, c.op = t.name.encode(), OPS[t.op]
        c.flags = u.TERM_MISSING_OK if t.missing_ok else 0
        if t.value is not None:
            c.type, c.len = t.value.type, len(t.value.payload)
            ctypes.memmove(c.value, t.value.payload, len(t.value.payload))
    if order is not None:
        q.order_by = order.name.encode()
        q.order = u.ORDER_DESC if order.descending else u.ORDER_ASC
        q.order_type = order.type


def _matches(buf, count):
    out = []
    for r in buf[:count]:
        known = not r.flags & (u.RES_PATH_UNKNOWN | u.RES_PATH_TRUNCATED)
        out.append(Match(os.fsdecode(r.path[:r.path_len]) if known else None, r.ino))
    return out


def _ioctl(fd, request, arg):
    try:
        fcntl.ioctl(fd, request, arg)
    except OSError as err:
        if err.errno in RESOURCE_ERRNO:
            raise QueryRefused(err.errno, f"QUERY2 refused: {os.strerror(err.errno)}") from err
        if err.errno in UNAVAILABLE_ERRNO:
            raise KernelUnavailable(err.errno, f"QUERY2 not served: {os.strerror(err.errno)}") from err
        raise


def kernel_query(fd, terms, order=None, page=u.MAX_LIMIT, stats=None):
    """Every answer of one QUERY2 on an open domain directory, all pages followed. Raises
    QueryRefused or KernelUnavailable with no partial list. stats, a dict, counts the ioctl
    calls in "query2_calls"."""
    terms = list(terms)
    check_request(terms, order)
    q = u.Query2()
    buf = (u.Result * page)()
    _fill(q, terms, order, page, ctypes.addressof(buf))
    out = []
    while True:
        _ioctl(fd, u.IOC_QUERY2, q)
        if stats is not None:
            stats["query2_calls"] = stats.get("query2_calls", 0) + 1
        out.extend(_matches(buf, q.count))
        if not q.truncated:
            return out
        q.count = q.truncated = 0


def kernel_batch(fd, requests, page=u.MAX_LIMIT, stats=None):
    """Answers of up to 8 (terms, order) requests in QUERY2_BATCH calls, all pages followed.
    Only a zero return is used: on any error every output of that call is dropped, and the
    error is raised (spec-query-v2 section 4.3)."""
    requests = [(list(t), o) for t, o in requests]
    if not 0 < len(requests) <= u.BATCH_MAX:
        raise ValueError("a batch holds 1 to 8 requests")
    for t, o in requests:
        check_request(t, o)
    answers = [[] for _ in requests]
    cursors = [bytes(u.CURSOR)] * len(requests)
    pending = list(range(len(requests)))
    while pending:
        qs = (u.Query2 * len(pending))()
        bufs = [(u.Result * page)() for _ in pending]
        for slot, i in enumerate(pending):
            _fill(qs[slot], requests[i][0], requests[i][1], page, ctypes.addressof(bufs[slot]))
            ctypes.memmove(qs[slot].cursor, cursors[i], u.CURSOR)
        b = u.Query2Batch(version=u.QUERY2_VERSION, nqueries=len(pending),
                          queries=ctypes.addressof(qs))
        _ioctl(fd, u.IOC_QUERY2_BATCH, b)
        if stats is not None:
            stats["query2_calls"] = stats.get("query2_calls", 0) + 1
        nxt = []
        for slot, i in enumerate(pending):
            answers[i].extend(_matches(bufs[slot], qs[slot].count))
            if qs[slot].truncated:
                cursors[i] = bytes(qs[slot].cursor)
                nxt.append(i)
        pending = nxt
    return answers


def describe(terms, order=None):
    """A one-line text of a request, for logs and errors."""
    parts = [f"{t.name} {t.op}" + ("" if t.value is None else f" {t.value.python()!r}")
             + (" (or missing)" if t.missing_ok else "") for t in terms]
    if order is not None:
        parts.append(f"order by {order.name} {TYPE_NAMES[order.type]}"
                     + (" desc" if order.descending else ""))
    return "; ".join(parts)
