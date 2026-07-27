"""Serial transport for the VBFC text+b64 protocol.

Two response shapes:
  * short replies — end with `OK` or `ERR <reason>`
  * streamed/bulk replies — start lines like `DUMP ...` / `MAP ...` / `STATUS ...`,
    interspersed with `DUMP CHUNK <b64>` lines, terminated by `DUMP DONE <crc>`
    (or `OK`). `command()` collects *all* lines until it sees a terminal token.
"""

from __future__ import annotations

import base64
import glob
import sys
import time
import zlib

import serial
import serial.tools.list_ports

# Reply lines that terminate a command's output set.
_TERMINATORS = ("OK", "ERR", "PONG", "DUMP DONE", "RESTORE PLAN")
# (Multi-line data prefixes like MAP / PATCH / DUMP / STATUS are streamed in
# full by command(), which keeps reading until a TERMINATOR is seen.)

# default chunk: 768 raw bytes -> 1024 base64 chars (matches firmware)
RAW_CHUNK = 768


class VbfcDevice:
    """USB CDC connection to a VBFC controller."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 2.0):
        self.port = port
        self._ser = serial.Serial(port, baud, timeout=timeout)

    def close(self) -> None:
        if self._ser.is_open:
            self._ser.close()

    def __enter__(self) -> "VbfcDevice":
        return self

    def __exit__(self, *args) -> None:
        self.close()

    # --- low-level send/recv -------------------------------------------------
    def _send(self, cmd: str) -> None:
        self._ser.reset_input_buffer()
        self._ser.write((cmd.strip() + "\n").encode("ascii"))
        self._ser.flush()

    def command(self, cmd: str, deadline: float = 30.0) -> list[str]:
        """Send a line, return ALL reply lines until a terminal token."""
        self._send(cmd)
        lines: list[str] = []
        end = time.monotonic() + deadline
        while time.monotonic() < end:
            raw = self._ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            if any(line.startswith(t) for t in _TERMINATORS):
                break
        return lines

    def _ok(self, lines: list[str]) -> bool:
        return any(l == "OK" or l.startswith("OK ") for l in lines)

    # --- identity ------------------------------------------------------------
    def ping(self) -> str | None:
        for l in self.command("PING"):
            if l.startswith("PONG"):
                return l.split(" ", 1)[1] if " " in l else "unknown"
        return None

    # --- mode ----------------------------------------------------------------
    def get_mode(self) -> str | None:
        for l in self.command("GET MODE"):
            if l.startswith("MODE "):
                return l.split(" ", 1)[1]
        return None

    def set_mode(self, mode: str) -> bool:
        return self._ok(self.command(f"SET MODE {mode}"))

    def set_bank(self, bank: int) -> bool:
        return self._ok(self.command(f"SET BANK {bank}"))

    # --- map -----------------------------------------------------------------
    def get_map(self) -> list[dict]:
        entries = []
        for l in self.command("GET MAP"):
            if not l.startswith("MAP "):
                continue
            parts = l.split()
            entries.append({
                "index": int(parts[1]),
                "start": int(parts[2], 16),
                "size": int(parts[3]),
                "source": parts[4],
                "ext_offset": int(parts[5], 16),
            })
        return entries

    def map_add(self, start: int, size: int, source: str, ext_offset: int) -> bool:
        cmd = f"MAP ADD 0x{start:X} {size} {source} 0x{ext_offset:X}"
        return self._ok(self.command(cmd))

    def map_remove(self, index: int) -> bool:
        return self._ok(self.command(f"MAP REMOVE {index}"))

    def map_clear(self) -> bool:
        return self._ok(self.command("MAP CLEAR"))

    # --- patch ---------------------------------------------------------------
    def patch_add(self, addr: int, orig: int, new: int) -> bool:
        cmd = f"PATCH ADD 0x{addr:X} 0x{orig:02X} 0x{new:02X}"
        return self._ok(self.command(cmd))

    def patch_list(self) -> list[dict]:
        out = []
        for l in self.command("PATCH LIST"):
            if not l.startswith("PATCH "):
                continue
            parts = l.split()
            out.append({
                "index": int(parts[1]),
                "addr": int(parts[2], 16),
                "orig": int(parts[3], 16),
                "new": int(parts[4], 16),
                "enabled": parts[5] == "on",
            })
        return out

    def patch_remove(self, index: int) -> bool:
        return self._ok(self.command(f"PATCH REMOVE {index}"))

    def patch_clear(self) -> bool:
        return self._ok(self.command("PATCH CLEAR"))

    # --- sniff ---------------------------------------------------------------
    def sniff_start(self) -> bool:
        return self._ok(self.command("SNIFF START"))

    def sniff_stop(self) -> bool:
        return self._ok(self.command("SNIFF STOP"))

    def sniff_status(self) -> dict | None:
        for l in self.command("SNIFF STATUS"):
            if l.startswith("SNIFF "):
                parts = l.split()
                return {"on": parts[1] == "on",
                        "events": int(parts[3]),
                        "capacity": int(parts[5])}
        return None

    # --- flash ---------------------------------------------------------------
    def flash_erase(self, offset: int, length: int) -> bool:
        return self._ok(self.command(f"FLASH ERASE 0x{offset:X} {length}"))

    def factory_reset(self) -> bool:
        return self._ok(self.command("FACTORY RESET"))

    def status(self) -> dict:
        info: dict[str, str] = {}
        for l in self.command("STATUS"):
            if l.startswith("STATUS "):
                kv = l[len("STATUS "):]
                k, _, v = kv.partition(" ")
                info[k] = v
        return info

    def reboot(self) -> bool:
        return self._ok(self.command("REBOOT", deadline=3.0))

    # --- binary transfer -----------------------------------------------------
    def upload_to_ext(self, data: bytes, ext_offset: int) -> bool:
        """ULOAD a blob into ext flash at ext_offset. Auto-erases on the device."""
        # START
        reply = self.command(f"ULOAD START {ext_offset} {len(data)}")
        if not any(l.startswith("OK upload-started") for l in reply):
            return False

        crc = 0
        sent = 0
        while sent < len(data):
            chunk = data[sent:sent + RAW_CHUNK]
            b64 = base64.b64encode(chunk).decode("ascii")
            r = self.command(f"ULOAD CHUNK {b64}", deadline=60.0)
            ack = r[-1] if r else ""
            if not ack.startswith("OK "):
                return False
            crc = zlib.crc32(chunk, crc) & 0xFFFFFFFF
            sent += len(chunk)

        r = self.command(f"ULOAD DONE 0x{crc:08X}")
        return self._ok(r)

    def dump(self, cmd: str) -> tuple[bytes, int] | None:
        """Run a DUMP command (DUMP EXT/ORIG/SNIFF or FLASH READ/BACKUP) and
        reassemble the base64 chunked stream. Returns (blob, crc32)."""
        self._send(cmd)
        blob = bytearray()
        crc = 0
        end = time.monotonic() + 120.0
        while time.monotonic() < end:
            raw = self._ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if not line:
                continue
            if line.startswith("DUMP CHUNK "):
                b64 = line[len("DUMP CHUNK "):]
                chunk = base64.b64decode(b64)
                blob.extend(chunk)
                crc = zlib.crc32(chunk, crc) & 0xFFFFFFFF
            elif line.startswith("DUMP DONE "):
                reported = int(line.split()[2], 16)
                if reported == crc:
                    return bytes(blob), crc
                return None  # crc mismatch
            elif line.startswith("ERR "):
                return None
            elif line.startswith(("DUMP ", "RESTORE PLAN")):
                continue
        return None

    def flash_read_ext(self, offset: int, length: int) -> bytes | None:
        r = self.dump(f"FLASH READ ext {offset} {length}")
        return r[0] if r else None

    def flash_read_orig(self, offset: int, length: int) -> bytes | None:
        r = self.dump(f"FLASH READ orig {offset} {length}")
        return r[0] if r else None

    def flash_backup(self, length: int) -> bytes | None:
        r = self.dump(f"FLASH BACKUP {length}")
        return r[0] if r else None

    def sniff_dump(self) -> bytes | None:
        r = self.dump("DUMP SNIFF")
        return r[0] if r else None


def auto_detect_port() -> str | None:
    """Find likely VBFC/Pico USB serial port."""
    patterns = ["/dev/ttyACM*", "/dev/cu.usbmodem*", "COM*"]
    candidates: list[str] = []
    for pattern in patterns:
        candidates.extend(glob.glob(pattern))

    for port in serial.tools.list_ports.comports():
        desc = (port.description or "").lower()
        if any(k in desc for k in ("pico", "usb serial", "cdc", "vbfc")):
            return port.device

    return candidates[0] if candidates else None


def open_device(port: str | None) -> VbfcDevice:
    resolved = port or auto_detect_port()
    if not resolved:
        print("No VBFC device found. Specify --port.", file=sys.stderr)
        sys.exit(1)
    return VbfcDevice(resolved)
