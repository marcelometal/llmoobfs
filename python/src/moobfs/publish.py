# SPDX-License-Identifier: LGPL-2.1-or-later
"""Publishing a memory (client contract section 6, spec-m2 section 6): write a temporary file in
the same directory, set every attribute on it, fsync it, rename it to the final name, fsync the
directory. A file under its final name then has all its content and attributes.

The file's mode and owner follow the homogeneity rule (spec-query-v2 section 3), so QUERY2 can
index it: by default the mode of file_mode_for(domain directory mode), and the domain's owner
and group where this process may set them.
"""
import ctypes
import errno
import os
import stat

from ._libc import libc
from .attrs import Value, check_name, validate

_RENAME_NOREPLACE = 1
_AT_FDCWD = -100


def file_mode_for(directory_mode):
    """A mode not more private than the directory: each class that can enter it can read and
    write the file. Execute stays off and the setgid bit is not copied."""
    mode = stat.S_IMODE(directory_mode)
    out = 0
    for search, rw in ((0o100, 0o600), (0o010, 0o060), (0o001, 0o006)):
        if mode & search:
            out |= rw
    return out


def _rename_noreplace(src, dst):
    fn = getattr(libc(), "renameat2", None)
    if fn is None:
        raise OSError(errno.ENOSYS, "renameat2 is not available in this C library")
    if fn(_AT_FDCWD, os.fsencode(src), _AT_FDCWD, os.fsencode(dst), _RENAME_NOREPLACE) != 0:
        e = ctypes.get_errno()
        raise OSError(e, os.strerror(e), dst)


def publish(directory, name, data, attrs=None, *, domain=None, mode=None, replace=False,
            _step=None):
    """Write data (bytes) under directory/name with the typed attributes attrs ({name: value}).

    domain is the domain directory whose rule the file must meet (default: directory). mode
    overrides file_mode_for(domain's mode). An existing name is refused (FileExistsError) unless
    replace is true, in which case it is replaced in one rename. Every attribute is checked
    before anything is written. Returns the final path. _step, for tests only, is called with
    the name of each completed step ("content", "attributes", "file_fsync", "rename",
    "dir_fsync"); an exception it raises before "rename" leaves the final name as it was.
    """
    step = _step or (lambda _name: None)
    if "/" in name or name in ("", ".", ".."):
        raise ValueError(f"invalid file name {name!r}")
    typed = []
    for k, v in (attrs or {}).items():
        check_name(k)
        raw = Value.of(v).encode()
        reason = validate(raw)
        if reason:
            raise ValueError(f"{k}: {reason}")
        typed.append((k, raw))
    final = os.path.join(directory, name)
    if not replace and os.path.lexists(final):
        raise FileExistsError(errno.EEXIST, os.strerror(errno.EEXIST), final)
    dom = os.stat(domain or directory)
    fmode = file_mode_for(dom.st_mode) if mode is None else mode
    tmp = os.path.join(directory, f".tmp.{os.getpid()}.{os.urandom(8).hex()}")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        view = memoryview(data)
        while view:
            view = view[os.write(fd, view):]
        step("content")
        for k, raw in typed:
            os.setxattr(fd, "user.moob." + k, raw)
        step("attributes")
        _identity(fd, fmode, dom)
        os.fsync(fd)
        step("file_fsync")
    except BaseException:
        os.close(fd)
        _unlink_quiet(tmp)
        raise
    os.close(fd)
    try:
        if replace:
            os.rename(tmp, final)
        else:
            _rename_noreplace(tmp, final)
    except BaseException:
        _unlink_quiet(tmp)
        raise
    step("rename")
    dfd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(dfd)
    finally:
        os.close(dfd)
    step("dir_fsync")
    return final


def _identity(fd, mode, dom):
    """Mode and owner the homogeneity rule can index. The domain's owner (or root) gives the
    file the domain's owner and group, which condition 2 of the rule needs even when the
    domain grants its group nothing. A publisher who is not the owner cannot give the file to
    that owner, so the file gains read for every class instead; the group is still set when
    the domain has group access and this process may."""
    owns = os.geteuid() in (0, dom.st_uid)
    if not owns:
        mode |= 0o444
    os.fchmod(fd, mode)
    uid = dom.st_uid if owns else -1
    gid = dom.st_gid if owns or dom.st_mode & 0o070 else -1
    if uid == -1 and gid == -1:
        return
    try:
        os.fchown(fd, uid, gid)
    except PermissionError:
        if owns:
            raise


def _unlink_quiet(path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
