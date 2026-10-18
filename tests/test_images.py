#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Independent format oracle, malformed images, and VM fixture generation."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / os.environ.get("MOOB_BUILD_DIR", "build/host")
BLOCK = 4096


def crc32c(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def u64(data, at):
    return struct.unpack_from("<Q", data, at)[0]


def repair_crcs(data):
    ot, ft, ds = (u64(data, at) for at in (24, 32, 40))
    struct.pack_into("<I", data, 60, crc32c(data[ot:ft]))
    struct.pack_into("<I", data, 64, crc32c(data[ft:ds]))
    struct.pack_into("<I", data, 56, 0)
    struct.pack_into("<I", data, 56, crc32c(data[:BLOCK]))


def run(*args, ok=True):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True)
    if ok and result.returncode:
        raise AssertionError(f"{args}: {result.stderr}")
    return result


def create(folder, entries, name="image.img"):
    args = []
    for i, (filename, payload) in enumerate(entries):
        source = folder / f"source-{i}"
        source.write_bytes(payload)
        args.append(f"{filename}={source}")
    image = folder / name
    run(BIN / "mkfs.moobfs", image, *args)
    return image


def malformed(original):
    """Checksums repaired where needed to exercise structural validation."""
    cases = {}
    data = bytearray(original)
    data[0] ^= 1
    cases["magic"] = bytes(data)
    data = bytearray(original)
    struct.pack_into("<I", data, 8, 2)
    repair_crcs(data)
    cases["version"] = bytes(data)
    data = bytearray(original)
    struct.pack_into("<I", data, 16, 0xFFFFFFFF)
    repair_crcs(data)
    cases["count"] = bytes(data)
    data = bytearray(original)
    struct.pack_into("<Q", data, 24, 0xFFFFFFFFFFFFFFFF)
    struct.pack_into("<I", data, 56, 0)
    struct.pack_into("<I", data, 56, crc32c(data[:BLOCK]))
    cases["table-offset"] = bytes(data)
    ot, ft, ds = (u64(original, at) for at in (24, 32, 40))
    data = bytearray(original)
    struct.pack_into("<Q", data, ot + 8, 0)
    repair_crcs(data)
    cases["metadata-overlap"] = bytes(data)
    data = bytearray(original)
    struct.pack_into("<Q", data, ot + 16, 0xFFFFFFFFFFFFFFFF)
    repair_crcs(data)
    cases["object-size"] = bytes(data)
    data = bytearray(original)
    struct.pack_into("<Q", data, ft + 8, 2)
    repair_crcs(data)
    cases["missing-object"] = bytes(data)
    data = bytearray(original)
    data[ft + 128 + 16:ft + 128 + 128] = data[ft + 16:ft + 128]
    repair_crcs(data)
    cases["duplicate-name"] = bytes(data)
    data = bytearray(original)
    data[ft + 24] = ord("/")
    repair_crcs(data)
    cases["slash-name"] = bytes(data)
    data = bytearray(original)
    data[ft + 24] = 0
    repair_crcs(data)
    cases["nul-name"] = bytes(data)
    data = bytearray(original)
    data[ds] ^= 0x80
    cases["payload"] = bytes(data)
    data = bytearray(original)
    data[ot + 2] ^= 1
    cases["table-checksum"] = bytes(data)
    data = bytearray(original)
    data[100] = 1
    repair_crcs(data)
    cases["reserved"] = bytes(data)
    cases["truncated"] = original[:-BLOCK]
    cases["trailing"] = original + bytes(BLOCK)
    return cases


class Images(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def info(self, path):
        return json.loads(run(BIN / "fsck.moobfs", path).stdout)

    def test_sharing_and_independent_ids(self):
        content = bytes(range(256)) * 49 + b"partial tail"
        path = create(self.folder, [("alpha.txt", content), ("beta.txt", content)])
        info = self.info(path)
        a, b = info["files"]
        self.assertEqual(info["object_count"], 1)
        self.assertNotEqual(a["inode"], b["inode"])
        self.assertEqual(a["offset"], b["offset"])
        self.assertEqual(a["sha256"], hashlib.sha256(content).hexdigest())
        raw = path.read_bytes()
        self.assertEqual(raw[a["offset"]:a["offset"] + a["size"]], content)
        self.assertEqual(raw.count(content), 1)

    def test_different_bytes_not_merged(self):
        info = self.info(create(self.folder, [("a", b"x"), ("b", b"x\n")]))
        self.assertEqual(info["object_count"], 2)

    def test_hash_boundary_vectors_and_empty(self):
        lengths = [0, 1, 55, 56, 63, 64, 65, 4095, 4096, 4097, 65536]
        entries = [(f"n{n}", bytes(i % 251 for i in range(n))) for n in lengths]
        info = self.info(create(self.folder, entries))
        for (_, body), file in zip(entries, info["files"]):
            self.assertEqual(file["sha256"], hashlib.sha256(body).hexdigest())
            self.assertEqual(file["size"], len(body))

    def test_crc_known_vector(self):
        self.assertEqual(crc32c(b"123456789"), 0xE3069283)
        path = create(self.folder, [("a", b"abc")])
        block = bytearray(path.read_bytes()[:BLOCK])
        expected = struct.unpack_from("<I", block, 56)[0]
        struct.pack_into("<I", block, 56, 0)
        self.assertEqual(crc32c(block), expected)

    def test_reproducible(self):
        entries = [("a", b"same"), ("b", b"same")]
        one = create(self.folder, entries, "one.img")
        two = create(self.folder, entries, "two.img")
        self.assertEqual(one.read_bytes(), two.read_bytes())

    def test_never_overwrites(self):
        path = create(self.folder, [("a", b"first")])
        before = path.read_bytes()
        result = run(BIN / "mkfs.moobfs", path, f"b={self.folder / 'source-0'}", ok=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_bytes(), before)

    def test_input_limits_and_duplicate_names(self):
        source = self.folder / "input"
        source.write_bytes(b"x")
        for names in [["a", "a"], ["."], [".."], ["a/b"], ["a" * 64], [""], ["á"], ["a\nb"]]:
            with self.subTest(names=names):
                result = run(BIN / "mkfs.moobfs", self.folder / "bad.img", *(f"{n}={source}" for n in names), ok=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.folder / "bad.img").exists())
        source.write_bytes(bytes(65537))
        self.assertNotEqual(run(BIN / "mkfs.moobfs", self.folder / "big.img", f"a={source}", ok=False).returncode, 0)

    def test_maximum_table_sizes(self):
        entries = [(f"file-{i}", bytes([i])) for i in range(128)]
        info = self.info(create(self.folder, entries))
        self.assertEqual(info["object_count"], 128)
        self.assertEqual(info["file_count"], 128)

    def test_corrupt_images(self):
        original = create(self.folder, [("alpha", b"payload"), ("beta", b"payload")]).read_bytes()
        for name, data in malformed(original).items():
            with self.subTest(case=name):
                path = self.folder / f"bad-{name}.img"
                path.write_bytes(data)
                self.assertNotEqual(run(BIN / "fsck.moobfs", path, ok=False).returncode, 0)


def fixtures(folder):
    folder.mkdir(parents=True, exist_ok=True)
    # Keep fixtures reproducible and never overwrite unrelated user files.
    content = bytes(range(256)) * 49 + b"MoobFS shared tail\n"
    (folder / "expected.bin").write_bytes(content)
    image = create(folder, [("alpha.txt", content), ("beta.txt", content)], "shared.img")
    create(folder, [("empty", b""), ("tiny", b"x"), ("maximum", bytes(range(256)) * 256)], "edges.img")
    for name, data in malformed(image.read_bytes()).items():
        (folder / f"bad-{name}.img").write_bytes(data)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--fixtures":
        fixtures(Path(sys.argv[2]))
    else:
        unittest.main(verbosity=2)
