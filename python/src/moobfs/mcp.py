# SPDX-License-Identifier: LGPL-2.1-or-later
"""An MCP server for one agent's memories (roadmap C7, ADR-0024, journal 0208).

    python -m moobfs.mcp --root /srv/memory [--read-only]

The agent's host starts it as a subprocess and talks MCP over its stdin and stdout. It runs
with the agent's own credentials and no others: it refuses to start as root or setuid, offers
no network transport and cannot switch users, so every permission decision is the kernel's,
made for the agent. Every path is a domain under --root and a path relative to it; absolute
paths, "..", NUL and symbolic links are refused, so the tools stay inside the root. That is a
scope guard, not a boundary against the agent itself, which can open with its own uid whatever
the kernel lets it.

Tools: info, query, read, write, delete (the last two absent with --read-only). Needs the
optional dependency: pip install "./python[mcp]".
"""
import argparse
import base64
import errno
import os
import stat
import struct
import sys
from datetime import datetime
from typing import Any

from . import _uapi as u
from .attrs import Value, get_attrs
from .domain import Domain, layer_mounted
from .publish import publish
from .query import ORDER_TYPES, Order, QueryRefused, Term

TYPES = {"string": u.T_STRING, "int64": u.T_INT64, "double": u.T_DOUBLE, "bool": u.T_BOOL,
         "time": u.T_TIME}
READ_MAX = 1 << 20


class ToolError(Exception):
    """A refusal the agent sees as the tool's error, with no partial result."""


def check_identity():
    """Refuse to be a deputy: not root, not setuid or setgid (ADR-0024 decision 3)."""
    if os.geteuid() == 0:
        raise SystemExit("moobfs.mcp: refusing to run as root; start it as the agent's user")
    if os.getuid() != os.geteuid() or os.getgid() != os.getegid():
        raise SystemExit("moobfs.mcp: refusing to run setuid or setgid")


# ---- JSON values -------------------------------------------------------------------------
def to_value(value, type_=None):
    """The Value of a JSON value; an explicit type wins (ADR-0024 decision 5)."""
    if type_ is None:
        if isinstance(value, bool):
            return Value(u.T_BOOL, bytes([value]))
        if isinstance(value, int):
            return Value.of(value)
        if isinstance(value, float):
            return Value.of(value)
        if isinstance(value, str):
            return Value.of(value)
        raise ToolError(f"no attribute type for {type(value).__name__}; give a type")
    if type_ not in TYPES:
        raise ToolError(f"type is one of {', '.join(TYPES)}")
    try:
        if type_ == "string":
            if not isinstance(value, str):
                raise TypeError
            return Value.of(value)
        if type_ == "int64":
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError
            return Value(u.T_INT64, struct.pack("<q", value))
        if type_ == "double":
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise TypeError
            return Value(u.T_DOUBLE, struct.pack("<d", float(value)))
        if type_ == "bool":
            if not isinstance(value, bool):
                raise TypeError
            return Value(u.T_BOOL, bytes([value]))
        if isinstance(value, bool):
            raise TypeError
        if isinstance(value, int):
            return Value.time_ns(value)
        if isinstance(value, str):
            t = datetime.fromisoformat(value.replace("Z", "+00:00"))
            if t.tzinfo is None:
                raise ToolError("a time needs a time zone, for instance 2026-10-27T12:00:00Z")
            return Value.of(t)
        raise TypeError
    except (TypeError, struct.error, OverflowError):
        raise ToolError(f"{value!r} is not a valid {type_}") from None
    except ValueError as err:
        raise ToolError(str(err)) from None


def from_value(v):
    """{type, value} of a stored Value, as JSON."""
    name = {code: n for n, code in TYPES.items()}.get(v.type, "raw")
    py = v.python()
    if v.type == u.T_TIME:
        py = py.isoformat()
    elif v.type == u.T_RAW:
        py = base64.b64encode(py).decode()
    return {"type": name, "value": py}


# ---- paths -------------------------------------------------------------------------------
class Memory:
    def __init__(self, root, read_only=False):
        self.root = os.path.realpath(root)
        if not os.path.isdir(self.root):
            raise SystemExit(f"moobfs.mcp: {root} is not a directory")
        self.read_only = read_only

    def domain_dir(self, domain):
        if (not isinstance(domain, str) or not domain or "/" in domain or "\0" in domain
                or domain in (".", "..")):
            raise ToolError(f"invalid domain {domain!r}: one directory name under the root")
        path = os.path.join(self.root, domain)
        try:
            st = os.lstat(path)
        except FileNotFoundError:
            raise ToolError(f"no domain {domain!r}") from None
        if not stat.S_ISDIR(st.st_mode):
            raise ToolError(f"{domain!r} is not a directory")
        return path

    def file_path(self, domain, path, must_exist=True):
        """domain/path, refusing escapes and symbolic links on the way."""
        base = self.domain_dir(domain)
        if not isinstance(path, str) or not path or "\0" in path or path.startswith("/"):
            raise ToolError(f"invalid path {path!r}: relative to the domain")
        parts = [p for p in path.split("/") if p not in ("", ".")]
        if not parts or ".." in parts:
            raise ToolError(f"invalid path {path!r}: no '..'")
        cur = base
        for part in parts:
            cur = os.path.join(cur, part)
            try:
                st = os.lstat(cur)
            except FileNotFoundError:
                if must_exist:
                    raise ToolError(f"{domain}/{path}: no such file") from None
                break               # write creates what is missing
            except PermissionError:
                raise ToolError(f"{domain}/{path}: permission denied") from None
            if stat.S_ISLNK(st.st_mode):
                raise ToolError(f"{domain}/{path}: symbolic links are refused")
        real = os.path.realpath(os.path.join(base, *parts))
        if real != self.root and not real.startswith(self.root + os.sep):
            raise ToolError(f"{domain}/{path}: outside the root")
        return os.path.join(base, *parts), parts

    # ---- tools ---------------------------------------------------------------------------
    def info(self):
        domains = []
        try:
            entries = sorted(os.scandir(self.root), key=lambda e: e.name)
        except PermissionError:
            entries = []
        for e in entries:
            if e.is_dir(follow_symlinks=False) and os.access(e.path, os.R_OK | os.X_OK):
                domains.append(e.name)
        return {"root": self.root, "layer_mounted": layer_mounted(self.root), "uid": os.getuid(),
                "gid": os.getgid(), "read_only": self.read_only, "domains": domains}

    def query(self, domain, terms=None, order=None, limit=100):
        base = self.domain_dir(domain)
        built = []
        for t in terms or []:
            if not isinstance(t, dict) or "name" not in t or "op" not in t:
                raise ToolError("a term is {name, op, value, type?, missing_ok?}")
            try:
                value = None if t["op"] == "exists" else to_value(t.get("value"), t.get("type"))
                built.append(Term(t["name"], t["op"], value, missing_ok=bool(t.get("missing_ok"))))
            except ValueError as err:
                raise ToolError(f"term {t}: {err}") from None
        ordering = None
        if order:
            if not isinstance(order, dict) or order.get("type") not in ORDER_TYPES:
                raise ToolError(f"order is {{name, type: {'|'.join(ORDER_TYPES)}, descending?}}")
            try:
                ordering = Order(order.get("name", ""), order["type"], bool(order.get("descending")))
            except ValueError as err:
                raise ToolError(str(err)) from None
        if not isinstance(limit, int) or limit < 1:
            raise ToolError("limit is a positive integer")
        d = Domain(base)
        try:
            found = d.query(*built, order=ordering)
        except ValueError as err:
            raise ToolError(str(err)) from None
        except QueryRefused as err:
            raise ToolError(f"query refused: {errno.errorcode.get(err.errno, err.errno)} "
                            "(a resource limit; nothing returned)") from None
        except PermissionError:
            raise ToolError(f"{domain}: permission denied") from None
        return {"backend": d.last_backend, "count": len(found), "truncated": len(found) > limit,
                "matches": [{"path": m.path, "ino": m.ino} for m in found[:limit]]}

    def read(self, domain, path, max_bytes=65536):
        full, _ = self.file_path(domain, path)
        if not isinstance(max_bytes, int) or not 0 < max_bytes <= READ_MAX:
            raise ToolError(f"max_bytes is 1 to {READ_MAX}")
        try:
            fd = os.open(full, os.O_RDONLY | os.O_NOFOLLOW)
        except PermissionError:
            raise ToolError(f"{domain}/{path}: permission denied") from None
        except FileNotFoundError:
            raise ToolError(f"{domain}/{path}: no such file") from None
        try:
            st = os.fstat(fd)
            if not stat.S_ISREG(st.st_mode):
                raise ToolError(f"{domain}/{path}: not a regular file")
            data = os.read(fd, max_bytes + 1)
            attrs = get_attrs(fd, indexed_only=False)
        finally:
            os.close(fd)
        return {"path": path, "size": st.st_size, "truncated": len(data) > max_bytes,
                "content": data[:max_bytes].decode("utf-8", "replace"),
                "attributes": {k: from_value(v) for k, v in sorted(attrs.items())}}

    def write(self, domain, path, content, attributes=None, replace=False):
        if self.read_only:
            raise ToolError("read-only server")
        full, parts = self.file_path(domain, path, must_exist=False)
        if not isinstance(content, str):
            raise ToolError("content is a string")
        attrs = {}
        for name, spec in (attributes or {}).items():
            if isinstance(spec, dict) and "value" in spec:
                attrs[name] = to_value(spec["value"], spec.get("type"))
            else:
                attrs[name] = to_value(spec)
        base = self.domain_dir(domain)
        mode = stat.S_IMODE(os.stat(base).st_mode)     # setgid of a group domain included
        cur = base
        for part in parts[:-1]:
            cur = os.path.join(cur, part)
            if not os.path.isdir(cur):
                try:
                    os.mkdir(cur, 0o700)
                    os.chmod(cur, mode)
                except PermissionError:
                    raise ToolError(f"{domain}/{path}: permission denied") from None
        try:
            publish(os.path.dirname(full), parts[-1], content.encode(), attrs, domain=base,
                    replace=bool(replace))
        except FileExistsError:
            raise ToolError(f"{domain}/{path} exists; pass replace to replace it") from None
        except PermissionError:
            raise ToolError(f"{domain}/{path}: permission denied") from None
        except ValueError as err:
            raise ToolError(str(err)) from None
        return {"path": path, "replaced": bool(replace)}

    def delete(self, domain, path):
        if self.read_only:
            raise ToolError("read-only server")
        full, _ = self.file_path(domain, path)
        st = os.lstat(full)
        if not stat.S_ISREG(st.st_mode):
            raise ToolError(f"{domain}/{path}: not a regular file")
        try:
            os.unlink(full)
        except PermissionError:
            raise ToolError(f"{domain}/{path}: permission denied") from None
        return {"path": path, "deleted": True}


def build_server(memory):
    """The MCPServer with the tools of ADR-0024 bound to memory."""
    from mcp.server import MCPServer
    from mcp.server.mcpserver.exceptions import ToolError as SDKToolError

    server = MCPServer("moobfs", instructions=(
        "Memories are files under one root, grouped in domain directories. query finds files by "
        "typed attributes; read returns content and attributes; write publishes a memory. Every "
        "access uses this process's own user, so the kernel decides what it may see."))

    def guard(fn, *args):
        try:
            return fn(*args)
        except ToolError as err:
            raise SDKToolError(str(err)) from None     # the agent sees it as the tool's error

    @server.tool()
    def info() -> dict[str, Any]:
        """The root, whether the MoobFS layer is mounted there, this server's uid and gid, and
        the domains this user can enter."""
        return guard(memory.info)

    @server.tool()
    def query(domain: str, terms: list[dict[str, Any]] | None = None,
              order: dict[str, Any] | None = None, limit: int = 100) -> dict[str, Any]:
        """Files of one domain whose typed attributes match every term. A term is
        {"name": ..., "op": "==|<|<=|>|>=|prefix|exists", "value": ..., "type":
        "string|int64|double|bool|time" (optional), "missing_ok": bool (optional)}; at most
        four. order is {"name", "type": "string|int64|double|time", "descending"}. Without
        order the order is unspecified."""
        return guard(memory.query, domain, terms, order, limit)

    @server.tool()
    def read(domain: str, path: str, max_bytes: int = 65536) -> dict[str, Any]:
        """Content (UTF-8) and typed attributes of one file, path relative to the domain."""
        return guard(memory.read, domain, path, max_bytes)

    if not memory.read_only:
        @server.tool()
        def write(domain: str, path: str, content: str,
                  attributes: dict[str, Any] | None = None, replace: bool = False) -> dict[str, Any]:
            """Publish one memory: content and typed attributes ({name: value} or
            {name: {"value", "type"}}) written in one sequence; an existing path is refused
            unless replace is true."""
            return guard(memory.write, domain, path, content, attributes, replace)

        @server.tool()
        def delete(domain: str, path: str) -> dict[str, Any]:
            """Remove one file, path relative to the domain."""
            return guard(memory.delete, domain, path)

    return server


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m moobfs.mcp",
                                 description="MCP server for one agent's MoobFS memories (stdio)")
    ap.add_argument("--root", required=True, help="the memory root; domains are its directories")
    ap.add_argument("--read-only", action="store_true", help="leave out write and delete")
    args = ap.parse_args(argv)
    check_identity()
    build_server(Memory(args.root, args.read_only)).run("stdio")


if __name__ == "__main__":
    main(sys.argv[1:])
