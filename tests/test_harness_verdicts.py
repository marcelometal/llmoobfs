#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Controlled userspace failures; run in a disposable Linux Docker container.

No module is needed. SOURCE_ROOT can point to a negative worktree with the same
regressions but the producer/child-verdict corrections removed.
"""
import os
import shlex
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('SOURCE_ROOT', Path(__file__).resolve().parents[1]))
SUITE = (ROOT / 'vm/suite.sh').read_text()


class HarnessVerdicts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)
        # Include only definitions, never source the module-loading suite.
        self.helper = SUITE.split('want() {', 1)[0]
        self.producer = self.work / 'query'
        self.producer.write_text('#!/bin/sh\necho "race2: 1 plausible pagination"\n'
                                 'echo "race: 1 plausible query"\n'
                                 'echo BUILDRACE_DONE\n'
                                 'exit "${PRODUCER_EXIT:-9}"\n')
        self.producer.chmod(0o755)

    def shell(self, code, status='9'):
        return subprocess.run(shlex.split(os.environ.get('SHELL_UNDER_TEST', 'sh')) +
                              ['-c', self.helper + '\n' + code], text=True,
                              capture_output=True, env={**os.environ,
                              'BIN': str(self.work), 'LAYER': str(self.work),
                              'PRODUCER_EXIT': status})

    def check_race_producer(self, token):
        line = next(s for s in SUITE.splitlines() if token in s)
        line = line.replace('/tmp/', str(self.work) + '/')
        setup = ('fs=fixture\nfail() { echo "MOOBFS_VM_FAIL: $*"; exit 1; }\n'
                 'touch "$BIN/q2e.go" "$BIN/race.go"\n'
                 'sleep 0.01 & racer=$!\n')
        for status in ('9', '0'):
            run = self.shell(setup + line + '\necho CHECK_PASS\n', status)
            self.assertIn('plausible', run.stdout)
            if status == '9':
                self.assertNotEqual(run.returncode, 0, run)
                self.assertIn('MOOBFS_VM_FAIL:', run.stdout)
                self.assertNotIn('CHECK_PASS', run.stdout)
                flag = 'q2e.go' if token == 'race2 5' else 'race.go'
                self.assertFalse((self.work / flag).exists())
            else:
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertIn('CHECK_PASS', run.stdout)

    def test_race2_producer_status_and_cleanup(self):
        self.check_race_producer("race2 5")

    def test_race_producer_status_and_cleanup(self):
        self.check_race_producer("race 5")

    def test_buildrace_status(self):
        line = next(s for s in SUITE.splitlines() if '"$LAYER" 24 60' in s)
        line = line.replace('/tmp/', str(self.work) + '/')
        (self.work / 'buildrace').symlink_to(self.producer)
        for status in ('9', '0'):
            run = self.shell(line + '\n[ "$rc" -eq 0 ] || exit 1\necho CHECK_PASS', status)
            self.assertIn('BUILDRACE_DONE', run.stdout)
            self.assertEqual('CHECK_PASS' in run.stdout, status == '0')
            self.assertEqual(run.returncode == 0, status == '0')

    def test_equivalence_producer_status(self):
        line = next(s for s in SUITE.splitlines() if 'sh "$BIN/bench-equiv.sh"' in s)
        line = line.replace('/tmp/', str(self.work) + '/')
        (self.work / 'bench-equiv.sh').write_text(
            'echo "EQUIV_PASS: 9 shapes (kernel fixture)"\nexit "${PRODUCER_EXIT:-9}"\n')
        for status in ('9', '0'):
            run = self.shell('TOOLS=$BIN LOWER=$LAYER fs=fixture\n'
                             'fail() { echo "MOOBFS_VM_FAIL: $*"; exit 1; }\n' +
                             line + '\necho CHECK_PASS', status)
            self.assertIn('EQUIV_PASS:', (self.work / 'bench-equiv.txt').read_text())
            self.assertEqual('CHECK_PASS' in run.stdout, status == '0')
            self.assertEqual(run.returncode == 0, status == '0')

    def test_fork_child_failure_has_no_summary(self):
        mock = self.work / 'mock.c'
        mock.write_text('''#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include "moobfs_ioctl.h"
int ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);
    (void)fd;
    if (request != MOOB_IOC_QUERY2) return -1;
    struct moob_query2 *q = arg;
    q->count = 0;
    q->truncated = 0;
    return 0;
}
void _exit(int status)
{
    if (getenv("FAIL_CHILD")) status = 9;
    syscall(SYS_exit_group, status);
    __builtin_unreachable();
}
''')
        binary = self.work / 'real-query'
        for args in (['gcc', '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'include'),
                      str(ROOT / 'tests/query.c'), str(ROOT / 'common/attr.c'), '-o', str(binary)],
                     ['gcc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                      '-I' + str(ROOT / 'include'), str(mock), '-o', str(self.work / 'mock.so')]):
            built = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
        for failed in (True, False):
            env = {**os.environ, 'LD_PRELOAD': str(self.work / 'mock.so'), 'MOOB_RACE2_FORK': '1'}
            env.pop('FAIL_CHILD', None)
            if failed:
                env['FAIL_CHILD'] = '1'
            run = subprocess.run([str(binary), str(self.work), 'race2', '0.01'],
                                 env=env, text=True, capture_output=True)
            if failed:
                self.assertNotEqual(run.returncode, 0)
                self.assertIn('WIFEXITED', run.stderr)
                self.assertNotIn('race2:', run.stdout)
            else:
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertEqual(run.stdout.count('race2:'), 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
