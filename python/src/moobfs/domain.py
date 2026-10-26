# SPDX-License-Identifier: LGPL-2.1-or-later
"""Capability detection and the Domain object (client contract section 7).

Domain(path).query(...) asks the kernel when the directory is on a mounted layer that serves
QUERY2, and otherwise walks the directory with the kernel's rules (moobfs.scan). Both give the
same answers; the kernel is the fast path, the scan the fallback.
"""
import ctypes
import ctypes.util
import fcntl
import os
import struct
import sys

from . import _uapi as u
from . import query as q
from . import scan

BACKENDS = ("auto", "kernel", "scan")


def layer_mounted(path):
    """True when path is on a mounted moobfs_layer (statfs type 0x4d4f4f42). False on systems
    other than Linux. Does not open the mount root."""
    if not sys.platform.startswith("linux"):
        return False
    libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
    buf = ctypes.create_string_buffer(256)   # struct statfs; f_type is its first word
    if libc.statfs(os.fsencode(path), buf) != 0:
        e = ctypes.get_errno()
        raise OSError(e, os.strerror(e), path)
    return struct.unpack_from("l", buf.raw)[0] == u.FS_MAGIC


def get_info(mount_root):
    """MOOB_IOC_GET_INFO on the mount root, as a dict. The index counters are zero unless the
    caller has CAP_SYS_ADMIN (flags has FLAG_COUNTERS then)."""
    info = u.Info()
    fd = os.open(mount_root, os.O_RDONLY | os.O_DIRECTORY)
    try:
        fcntl.ioctl(fd, u.IOC_GET_INFO, info)
    finally:
        os.close(fd)
    out = {name: getattr(info, name) for name, _ in u.Info._fields_ if name != "reserved"}
    out["lower_fs"] = info.lower_fs.decode(errors="replace")
    return out


def usable_info(info):
    """Contract section 7 step 2: domains on, index not off, not marked incomplete (older
    modules set that flag)."""
    f = info["flags"]
    return bool(f & u.FLAG_DOMAINS) and not f & (u.FLAG_INDEX_OFF | u.FLAG_INDEX_INCOMPLETE)


class Domain:
    """A domain directory. backend is "auto" (kernel when served, else scan), "kernel" (never
    scan) or "scan" (never ask the kernel). With on_refusal="scan", a QueryRefused from the
    kernel is answered by the scan instead of raised. last_backend says which one answered."""

    def __init__(self, path, backend="auto", on_refusal="raise"):
        if backend not in BACKENDS:
            raise ValueError(f"backend is one of {', '.join(BACKENDS)}")
        if on_refusal not in ("raise", "scan"):
            raise ValueError('on_refusal is "raise" or "scan"')
        self.path = os.path.abspath(path)
        self.backend, self.on_refusal = backend, on_refusal
        self.last_backend = None
        self.mounted = layer_mounted(self.path) if backend != "scan" else False

    def _kernel(self, call):
        fd = os.open(self.path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            return call(fd)
        finally:
            os.close(fd)

    def _answer(self, kernel_call, scan_call):
        if self.backend == "scan" or (self.backend == "auto" and not self.mounted):
            self.last_backend = "scan"
            return scan_call()
        try:
            out = self._kernel(kernel_call)
        except q.KernelUnavailable:
            if self.backend == "kernel":
                raise
            self.last_backend = "scan"
            return scan_call()
        except q.QueryRefused:
            if self.on_refusal != "scan":
                raise
            self.last_backend = "scan"
            return scan_call()
        self.last_backend = "kernel"
        return out

    def query(self, *terms, order=None):
        """Every answer of the terms (all must hold), as a list of Match. Without order the
        order is unspecified."""
        return self._answer(lambda fd: q.kernel_query(fd, terms, order),
                            lambda: scan.scan_query(self.path, terms, order))

    def batch(self, requests):
        """Answers of up to 8 (terms, order) requests, one list each: "in a set" is one request
        per value, and the union is the caller's (contract section 6)."""
        requests = [(tuple(t), o) for t, o in requests]
        return self._answer(lambda fd: q.kernel_batch(fd, requests),
                            lambda: scan.scan_batch(self.path, requests))
