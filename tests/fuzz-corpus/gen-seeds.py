#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Writes the libFuzzer seeds of tests/fuzz_attr.c into tests/fuzz-corpus/attr (issue #9).

The first byte of a seed selects the function (tests/fuzz_attr.c): 0 moob_attr_validate() of a
typed value (type byte, then payload), 1 the name checks, 2 the format marker. Each seed is named
by what it exercises; the boundaries come from include/moobfs_attr.h. Run from the repository root:
    python3 tests/fuzz-corpus/gen-seeds.py
"""
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'attr')
STRING_MAX, RAW_MAX, NAME_MAX = 1024, 4096, 64
T_STRING, T_INT64, T_DOUBLE, T_BOOL, T_TIME, T_RAW = 1, 2, 3, 4, 5, 6


def value(t, payload=b''):
    return bytes([0, t]) + payload


def name(s):
    return b'\x01' + (s.encode() if isinstance(s, str) else s)


def fmt(b):
    return b'\x02' + b


seeds = {
    # strings: empty, boundaries, valid multi-byte UTF-8, invalid UTF-8, embedded NUL
    'v-string-empty': value(T_STRING),
    'v-string-max': value(T_STRING, b'a' * STRING_MAX),
    'v-string-max-plus-one': value(T_STRING, b'a' * (STRING_MAX + 1)),
    'v-string-utf8-2-3-4-bytes': value(T_STRING, 'é€😀'.encode()),
    'v-string-utf8-lone-continuation': value(T_STRING, b'\x80'),
    'v-string-utf8-overlong': value(T_STRING, b'\xc0\xaf'),
    'v-string-utf8-surrogate': value(T_STRING, b'\xed\xa0\x80'),
    'v-string-utf8-truncated': value(T_STRING, b'\xe2\x82'),
    'v-string-utf8-above-max-codepoint': value(T_STRING, b'\xf4\x90\x80\x80'),
    'v-string-nul-inside': value(T_STRING, b'a\x00b'),
    # 64-bit integers and times: extremes and wrong lengths
    'v-int64-zero': value(T_INT64, struct.pack('<q', 0)),
    'v-int64-min': value(T_INT64, struct.pack('<q', -2**63)),
    'v-int64-max': value(T_INT64, struct.pack('<q', 2**63 - 1)),
    'v-int64-7-bytes': value(T_INT64, b'\x01' * 7),
    'v-int64-9-bytes': value(T_INT64, b'\x01' * 9),
    'v-time-min': value(T_TIME, struct.pack('<q', -2**63)),
    'v-time-max': value(T_TIME, struct.pack('<q', 2**63 - 1)),
    'v-time-empty': value(T_TIME),
    # doubles: signed zero, infinities, quiet and signalling NaN, subnormal, wrong length
    'v-double-zero': value(T_DOUBLE, struct.pack('<d', 0.0)),
    'v-double-negative-zero': value(T_DOUBLE, struct.pack('<d', -0.0)),
    'v-double-infinity': value(T_DOUBLE, struct.pack('<d', float('inf'))),
    'v-double-negative-infinity': value(T_DOUBLE, struct.pack('<d', float('-inf'))),
    'v-double-quiet-nan': value(T_DOUBLE, struct.pack('<Q', 0x7ff8000000000000)),
    'v-double-signalling-nan': value(T_DOUBLE, struct.pack('<Q', 0x7ff0000000000001)),
    'v-double-negative-nan': value(T_DOUBLE, struct.pack('<Q', 0xfff8000000000000)),
    'v-double-subnormal': value(T_DOUBLE, struct.pack('<Q', 1)),
    'v-double-4-bytes': value(T_DOUBLE, b'\x00' * 4),
    # booleans: the two valid bytes, others, wrong lengths
    'v-bool-0': value(T_BOOL, b'\x00'),
    'v-bool-1': value(T_BOOL, b'\x01'),
    'v-bool-2': value(T_BOOL, b'\x02'),
    'v-bool-255': value(T_BOOL, b'\xff'),
    'v-bool-empty': value(T_BOOL),
    'v-bool-2-bytes': value(T_BOOL, b'\x01\x01'),
    # raw: empty, boundary, boundary plus one
    'v-raw-empty': value(T_RAW),
    'v-raw-max': value(T_RAW, b'\xa5' * RAW_MAX),
    'v-raw-max-plus-one': value(T_RAW, b'\xa5' * (RAW_MAX + 1)),
    # type bytes outside the enumeration, and no type at all
    'v-type-0': value(0, b'x'),
    'v-type-7': value(7, b'x'),
    'v-type-255': value(255, b'x'),
    'v-no-type-byte': b'\x00',
    # names
    'n-plain': name('kind'),
    'n-empty': name(''),
    'n-max': name('a' * NAME_MAX),
    'n-max-plus-one': name('a' * (NAME_MAX + 1)),
    'n-leading-dot': name('.hidden'),
    'n-trailing-dot': name('kind.'),
    'n-double-dot': name('a..b'),
    'n-uppercase': name('Kind'),
    'n-slash': name('a/b'),
    'n-nul-inside': name(b'ki\x00nd'),
    'n-non-ascii': name('tipo-é'),
    'n-reserved-format': name('format'),
    # format marker: decimal ASCII, 1 to 9 digits (moob_attr_parse_format)
    'f-version-1': fmt(b'1'),
    'f-version-0': fmt(b'0'),
    'f-nine-digits': fmt(b'999999999'),
    'f-ten-digits': fmt(b'1234567890'),
    'f-empty': fmt(b''),
    'f-not-a-number': fmt(b'x1'),
    'f-trailing-newline': fmt(b'1\n'),
}

os.makedirs(OUT, exist_ok=True)
for n, data in sorted(seeds.items()):
    with open(os.path.join(OUT, n), 'wb') as f:
        f.write(data)
print(f'{len(seeds)} seeds written to {OUT}')
