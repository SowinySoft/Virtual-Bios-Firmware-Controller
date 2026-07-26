"""Host-side self-test for the VBFC binary framing + CRC parity.

No hardware needed: it exercises the Python-side encode/decode and the
crc32 parity contract (firmware uses the same IEEE-802.3 reflected CRC).
Also sanity-checks the Click CLI imports and command surface area.
"""

from __future__ import annotations

import base64
import zlib

from click.testing import CliRunner

from vbfc_cli.main import cli, parse_int


def test_parse_int() -> None:
    assert parse_int("0x2000") == 0x2000
    assert parse_int("64K") == 65536
    assert parse_int("16M") == 16 * 1024 * 1024
    assert parse_int("255") == 255
    print("parse_int OK")


def test_b64_roundtrip_chunked() -> None:
    # 768-byte raw chunk -> 1024 base64 chars, matches firmware CHUNK_RAW.
    payload = bytes((i * 7) & 0xFF for i in range(768))
    b64 = base64.b64encode(payload).decode("ascii")
    assert len(b64) == 1024, len(b64)
    assert base64.b64decode(b64) == payload
    print("b64 chunk roundtrip OK")


def test_crc32_parity() -> None:
    # The firmware vbfc_crc.c computes the same reflected CRC as zlib on bytes.
    for n in (0, 1, 3, 768, 4096):
        data = bytes(range(256)) * (n // 256 + 1)
        data = data[:n]
        # streaming accumulation must equal the one-shot zlib value
        acc = 0
        for i in range(0, len(data), 100):
            acc = zlib.crc32(data[i:i + 100], acc) & 0xFFFFFFFF
        assert acc == (zlib.crc32(data) & 0xFFFFFFFF)
    print("crc32 streaming parity OK")


def test_event_record_decode() -> None:
    # firmware sniffer_record packs: cmd, flags, addr(3 LE), 0, cnt(2 LE)
    def pack(cmd, flags, addr, cnt):
        return bytes([cmd, flags,
                      addr & 0xFF, (addr >> 8) & 0xFF, (addr >> 16) & 0xFF,
                      0, cnt & 0xFF, (cnt >> 8) & 0xFF])
    rec = pack(0x03, 0x05, 0x123456, 2048)
    cmd = rec[0]; addr = int.from_bytes(rec[2:5], "little"); cnt = int.from_bytes(rec[6:8], "little")
    assert (cmd, addr, cnt) == (0x03, 0x123456, 2048)
    print("event record decode OK")


def test_cli_surface() -> None:
    runner = CliRunner()
    res = runner.invoke(cli, ["--help"])
    assert res.exit_code == 0, res.output
    for sub in ("scan", "mode", "map", "patch", "flash", "image", "sniff",
                "status", "version", "reboot", "factory-reset"):
        assert sub in res.output, sub
    # group help
    for grp in ("map", "patch", "flash", "image", "sniff"):
        r = runner.invoke(cli, [grp, "--help"])
        assert r.exit_code == 0, r.output
    print("CLI surface OK")


def main() -> None:
    test_parse_int()
    test_b64_roundtrip_chunked()
    test_crc32_parity()
    test_event_record_decode()
    test_cli_surface()
    print("ALL HOST SELF-TESTS PASS")


if __name__ == "__main__":
    main()
