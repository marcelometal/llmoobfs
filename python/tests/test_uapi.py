# SPDX-License-Identifier: LGPL-2.1-or-later
"""The ctypes mirror against include/moobfs_ioctl.h: sizes, field offsets and ioctl numbers,
from a C program compiled against the header. Needs a C compiler and the Linux UAPI headers;
skipped otherwise."""
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
if not os.environ.get("MOOBFS_TEST_INSTALLED"):   # else test the installed package
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))

from moobfs import _uapi as u  # noqa: E402

STRUCTS = {"Term": "moob_query_term", "Result": "moob_query_result", "Query2": "moob_query2",
           "Query2Batch": "moob_query2_batch", "Info": "moob_info"}
IOCTLS = {"IOC_GET_INFO": "MOOB_IOC_GET_INFO", "IOC_QUERY2": "MOOB_IOC_QUERY2",
          "IOC_QUERY2_BATCH": "MOOB_IOC_QUERY2_BATCH"}


def c_program():
    lines = ['#include <stdio.h>', '#include <stddef.h>', '#include "moobfs_ioctl.h"',
             "int main(void) {"]
    for py, c in STRUCTS.items():
        lines.append(f'printf("{py} size %zu\\n", sizeof(struct {c}));')
        for name, _ in getattr(u, py)._fields_:
            lines.append(f'printf("{py} {name} %zu\\n", offsetof(struct {c}, {name}));')
    for py, c in IOCTLS.items():
        lines.append(f'printf("{py} %lu\\n", (unsigned long){c});')
    lines += ["return 0; }"]
    return "\n".join(lines)


@unittest.skipUnless(shutil.which("cc") and os.path.exists("/usr/include/linux/types.h"),
                     "needs cc and the Linux UAPI headers")
class Layout(unittest.TestCase):
    def test_matches_header(self):
        with tempfile.TemporaryDirectory() as d:
            src, exe = os.path.join(d, "l.c"), os.path.join(d, "l")
            with open(src, "w") as f:
                f.write(c_program())
            subprocess.run(["cc", "-I", os.path.join(REPO, "include"), "-o", exe, src], check=True)
            out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
        seen = 0
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 2:
                self.assertEqual(getattr(u, parts[0]), int(parts[1]), parts[0])
            elif parts[1] == "size":
                self.assertEqual(ctypes.sizeof(getattr(u, parts[0])), int(parts[2]), parts[0])
            else:
                self.assertEqual(getattr(getattr(u, parts[0]), parts[1]).offset, int(parts[2]),
                                 f"{parts[0]}.{parts[1]}")
            seen += 1
        self.assertGreater(seen, 50)


if __name__ == "__main__":
    unittest.main()
