# SPDX-License-Identifier: LGPL-2.1-or-later
"""Typed attributes (docs/spec-m2.md): a type byte, then the payload, in user.moob.<name>.

Python values map to types as follows: str is a string, bool a bool, int an int64, float a
double, an aware datetime a time (nanoseconds since the epoch), bytes a raw value (stored, never
indexed). A Value gives the type explicitly, for instance Value.time_ns(n). Validation is the
one of common/attr.c, so a value this module accepts is one the layer indexes.
"""
import errno
import math
import os
import struct
from datetime import datetime, timezone

from . import _uapi as u

TYPE_NAMES = {u.T_STRING: "string", u.T_INT64: "int64", u.T_DOUBLE: "double", u.T_BOOL: "bool",
              u.T_TIME: "time", u.T_RAW: "raw"}


class Value:
    """A typed attribute value: type code and payload bytes, without the type byte."""

    __slots__ = ("type", "payload")

    def __init__(self, type_, payload):
        self.type, self.payload = type_, bytes(payload)

    @classmethod
    def of(cls, value):
        """The Value of a Python value, as the module docstring maps it."""
        if isinstance(value, Value):
            return value
        if isinstance(value, bool):
            return cls(u.T_BOOL, bytes([value]))
        if isinstance(value, int):
            return cls(u.T_INT64, struct.pack("<q", value))
        if isinstance(value, float):
            return cls(u.T_DOUBLE, struct.pack("<d", value))
        if isinstance(value, str):
            return cls(u.T_STRING, value.encode())
        if isinstance(value, datetime):
            if value.tzinfo is None:
                raise ValueError("a time needs an aware datetime")
            delta = value - datetime(1970, 1, 1, tzinfo=timezone.utc)
            ns = (delta.days * 86400 + delta.seconds) * 10**9 + delta.microseconds * 1000
            return cls.time_ns(ns)
        if isinstance(value, (bytes, bytearray, memoryview)):
            return cls(u.T_RAW, bytes(value))
        raise TypeError(f"no attribute type for {type(value).__name__}")

    @classmethod
    def time_ns(cls, ns):
        return cls(u.T_TIME, struct.pack("<q", ns))

    def encode(self):
        """The bytes stored in the extended attribute."""
        return bytes([self.type]) + self.payload

    @classmethod
    def decode(cls, raw):
        """The Value of stored bytes, or None when the layer would not index them."""
        if validate(raw) is not None:
            return None
        return cls(raw[0], raw[1:])

    def python(self):
        """The Python value: str, int, float, bool, aware datetime (microseconds) or bytes."""
        t, p = self.type, self.payload
        if t == u.T_STRING:
            return p.decode()
        if t == u.T_INT64:
            return struct.unpack("<q", p)[0]
        if t == u.T_DOUBLE:
            return struct.unpack("<d", p)[0]
        if t == u.T_BOOL:
            return bool(p[0])
        if t == u.T_TIME:
            ns = struct.unpack("<q", p)[0]
            return datetime.fromtimestamp(ns // 10**9, timezone.utc).replace(
                microsecond=(ns % 10**9) // 1000)
        return p

    def __eq__(self, other):
        return isinstance(other, Value) and (self.type, self.payload) == (other.type, other.payload)

    def __hash__(self):
        return hash((self.type, self.payload))

    def __repr__(self):
        return f"Value({TYPE_NAMES.get(self.type, self.type)}, {self.python()!r})"


def name_ok(name):
    """moob_attr_name_ok: [a-z0-9_.], 1 to 64 bytes, no dot at an edge, no two dots in a row."""
    if not 0 < len(name) <= u.NAME_MAX or name[0] == "." or name[-1] == "." or ".." in name:
        return False
    return all("a" <= c <= "z" or "0" <= c <= "9" or c in "_." for c in name)


def utf8_ok(b):
    """moob_utf8_valid: well-formed UTF-8 without NUL (Python's codec rejects overlongs,
    surrogates and code points above U+10FFFF, as the C function does)."""
    if b"\0" in b:
        return False
    try:
        b.decode("utf-8", "strict")
    except UnicodeDecodeError:
        return False
    return True


def validate(raw):
    """None when raw is a valid stored value (moob_attr_validate), else the reason."""
    if not raw:
        return "empty value"
    t, n = raw[0], len(raw) - 1
    if t == u.T_STRING:
        if n > u.STRING_MAX:
            return "payload size does not match type"
        return None if utf8_ok(raw[1:]) else "string is not valid UTF-8"
    if t in (u.T_INT64, u.T_TIME):
        return None if n == 8 else "payload size does not match type"
    if t == u.T_DOUBLE:
        if n != 8:
            return "payload size does not match type"
        return "payload is not a valid value" if math.isnan(struct.unpack("<d", raw[1:])[0]) else None
    if t == u.T_BOOL:
        if n != 1:
            return "payload size does not match type"
        return None if raw[1] <= 1 else "payload is not a valid value"
    if t == u.T_RAW:
        return None if n <= u.RAW_MAX else "payload size does not match type"
    return "unknown type byte"


def check_name(name):
    if not name_ok(name):
        raise ValueError(f"invalid attribute name {name!r}")
    if name in u.RESERVED_NAMES:
        raise ValueError(f"reserved attribute name {name!r}")


def set_attr(target, name, value):
    """Set user.moob.<name> on a path or an open descriptor. Raises ValueError for a name or
    value the layer would refuse to index, before any write."""
    check_name(name)
    raw = Value.of(value).encode()
    reason = validate(raw)
    if reason:
        raise ValueError(f"{name}: {reason}")
    os.setxattr(target, u.PREFIX + name, raw)


def remove_attr(target, name):
    os.removexattr(target, u.PREFIX + name)


def get_attrs(target, indexed_only=True):
    """{name: Value} of the typed attributes of a path or descriptor. With indexed_only, the
    ones the layer indexes: valid names, valid values, not reserved, not raw."""
    out = {}
    for full in os.listxattr(target):
        if not full.startswith(u.PREFIX):
            continue
        name = full[len(u.PREFIX):]
        if not name_ok(name) or name in u.RESERVED_NAMES:
            continue
        try:
            raw = os.getxattr(target, full)
        except OSError as err:
            if err.errno == errno.ENODATA:
                continue        # removed meanwhile
            raise
        v = Value.decode(raw)
        if v is None or (indexed_only and v.type == u.T_RAW):
            continue
        out[name] = v
    return out
