# SPDX-License-Identifier: LGPL-2.1-or-later
"""The C library, for statfs(2) and renameat2(2), which the os module lacks."""
import ctypes
import functools


@functools.lru_cache(maxsize=None)
def libc():
    # dlopen(NULL) finds the C library this process already runs on; ctypes.util.find_library
    # would start ldconfig, or a compiler or linker from PATH, on every call to look for it
    return ctypes.CDLL(None, use_errno=True)
