# SPDX-License-Identifier: LGPL-2.1-or-later
"""moobfs.mcp on a plain Linux directory, through an in-process MCP client: the tools, the
path confinement, read-only mode and the identity check. Skipped without the mcp package or
user extended attributes. The mounted, two-user acceptance is mcp_mounted.py (journal 0208)."""
import asyncio
import os
import shutil
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
if not os.environ.get("MOOBFS_TEST_INSTALLED"):   # else test the installed package
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))

try:
    from mcp import Client
    import moobfs.mcp as srv
    HAVE = sys.platform.startswith("linux")
except ImportError:
    HAVE = False


def call(server, name, args):
    async def go():
        async with Client(server) as client:
            r = await client.call_tool(name, args)
            return r.is_error, (r.structured_content or {}), " ".join(
                getattr(c, "text", "") for c in r.content)
    return asyncio.run(go())


def tools(server):
    async def go():
        async with Client(server) as client:
            return sorted(t.name for t in (await client.list_tools()).tools)
    return asyncio.run(go())


@unittest.skipUnless(HAVE, "needs Linux and the mcp package")
class Server(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="moobfs-mcp-")
        probe = os.path.join(self.root, "p")
        open(probe, "w").close()
        try:
            os.setxattr(probe, "user.moob.p", b"\x04\x01")
        except OSError:
            shutil.rmtree(self.root)
            self.skipTest("no user extended attributes here")
        os.unlink(probe)
        os.mkdir(os.path.join(self.root, "alice"), 0o700)
        self.server = srv.build_server(srv.Memory(self.root))

    def tearDown(self):
        shutil.rmtree(self.root)

    def test_write_read_query_delete(self):
        err, out, _ = call(self.server, "write", {
            "domain": "alice", "path": "notes/a.md", "content": "use staging",
            "attributes": {"kind": "decision", "n": 3, "score": 0.5, "ok": True,
                           "t": {"value": "2026-10-27T12:00:00Z", "type": "time"},
                           "d": {"value": 2, "type": "double"}}})
        self.assertFalse(err, out)
        err, out, _ = call(self.server, "read", {"domain": "alice", "path": "notes/a.md"})
        self.assertFalse(err)
        self.assertEqual(out["content"], "use staging")
        self.assertEqual(out["attributes"]["n"], {"type": "int64", "value": 3})
        self.assertEqual(out["attributes"]["d"], {"type": "double", "value": 2.0})
        self.assertEqual(out["attributes"]["t"]["type"], "time")
        err, out, _ = call(self.server, "query", {"domain": "alice", "terms": [
            {"name": "kind", "op": "==", "value": "decision"}, {"name": "n", "op": ">", "value": 2}]})
        self.assertFalse(err)
        self.assertEqual([m["path"] for m in out["matches"]], ["notes/a.md"])
        self.assertEqual(out["backend"], "scan")
        err, _, text = call(self.server, "write", {"domain": "alice", "path": "notes/a.md",
                                                   "content": "x"})
        self.assertTrue(err)
        self.assertIn("exists", text)
        err, out, _ = call(self.server, "delete", {"domain": "alice", "path": "notes/a.md"})
        self.assertFalse(err)
        err, out, _ = call(self.server, "query", {"domain": "alice",
                                                  "terms": [{"name": "kind", "op": "exists"}]})
        self.assertEqual(out["count"], 0)

    def test_paths_stay_inside_the_root(self):
        outside = tempfile.mkdtemp(prefix="moobfs-mcp-out-")
        try:
            with open(os.path.join(outside, "secret"), "w") as f:
                f.write("no")
            os.symlink(outside, os.path.join(self.root, "alice", "link"))
            os.symlink(outside, os.path.join(self.root, "escape"))
            bad = [("alice", "../alice/x"), ("alice", "/etc/passwd"), ("alice", "link/secret"),
                   ("../tmp", "x"), ("escape", "secret"), ("alice/..", "x"), ("", "x"),
                   ("alice", ""), ("alice", "a\0b")]
            for domain, path in bad:
                err, out, _ = call(self.server, "read", {"domain": domain, "path": path})
                self.assertTrue(err, (domain, path))
                self.assertNotIn("content", out)
                err, _, _ = call(self.server, "write", {"domain": domain, "path": path,
                                                        "content": "x"})
                self.assertTrue(err, (domain, path))
            self.assertEqual(os.listdir(outside), ["secret"])
        finally:
            shutil.rmtree(outside)

    def test_bad_terms_are_tool_errors(self):
        for terms in ([{"name": "Bad-Name", "op": "==", "value": 1}],
                      [{"name": "k", "op": "~", "value": 1}],
                      [{"name": "k", "op": "==", "value": "x", "type": "int64"}],
                      [{"name": "k", "op": "==", "value": [1]}],
                      [{"name": "k", "op": "==", "value": 1}] * 5,
                      [{"op": "=="}]):
            err, _, _ = call(self.server, "query", {"domain": "alice", "terms": terms})
            self.assertTrue(err, terms)

    def test_read_only(self):
        ro = srv.build_server(srv.Memory(self.root, read_only=True))
        self.assertEqual(tools(ro), ["info", "query", "read"])
        self.assertEqual(tools(self.server), ["delete", "info", "query", "read", "write"])

    def test_info(self):
        err, out, _ = call(self.server, "info", {})
        self.assertFalse(err)
        self.assertEqual(out["domains"], ["alice"])
        self.assertFalse(out["layer_mounted"])
        self.assertEqual(out["uid"], os.getuid())

    def test_identity_refused(self):
        with mock.patch("os.geteuid", return_value=0):
            with self.assertRaises(SystemExit):
                srv.check_identity()
        with mock.patch("os.geteuid", return_value=os.getuid() + 1):
            with self.assertRaises(SystemExit):
                srv.check_identity()


if __name__ == "__main__":
    unittest.main()
