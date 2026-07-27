"""VBFC host CLI — Click entry point.

Mirrors the firmware command set: scan / mode / map / patch / flash / image /
sniff / status / version / reboot. Binary transfers (image upload, flash
backup/restore/read, sniff dump) flow through device.upload_to_ext() /
device.dump().
"""

from __future__ import annotations

import re
import sys
import time
from pathlib import Path

import click

from vbfc_cli.device import open_device
from vbfc_cli.crypto import generate_keypair, sign_payload, verify_image, DEFAULT_KEY_DIR
from vbfc_cli.bios_analyzer import analyze_rom
from vbfc_cli.block_check import check_rom


def parse_int(value: str) -> int:
    """Parse a decimal or 0x-hex integer, with optional k/m/g suffix."""
    m = re.fullmatch(r"(?i)(?:0x)?([0-9a-f]+)([kmg]?)", value.strip())
    if not m:
        raise click.BadParameter(f"invalid integer/size: {value}")
    base = 16 if value.strip().lower().startswith("0x") else 10
    n = int(m.group(1), base)
    mult = {"": 1, "k": 1024, "m": 1024 * 1024, "g": 1024 * 1024 * 1024}
    return n * mult[m.group(2).lower()]


@click.group()
@click.option("--port", default=None, help="Serial port (auto-detect if omitted)")
@click.pass_context
def cli(ctx: click.Context, port: str | None) -> None:
    ctx.ensure_object(dict)
    ctx.obj["port"] = port


# --- info / lifecycle -------------------------------------------------------
@cli.command()
@click.pass_context
def scan(ctx: click.Context) -> None:
    """Detect device and show status."""
    with open_device(ctx.obj["port"]) as dev:
        version = dev.ping()
        if not version:
            click.echo("Device not responding.")
            raise SystemExit(1)
        click.echo(f"Device:  vbfc-{version}")
        click.echo(f"Port:    {dev.port}")
        click.echo(f"Mode:    {dev.get_mode()}")
        entries = dev.get_map()
        if entries:
            click.echo("Shadow map:")
            for e in entries:
                click.echo(f"  [{e['index']}] 0x{e['start']:08X} "
                           f"{e['size']} bytes -> {e['source']} "
                           f"(ext 0x{e['ext_offset']:X})")
        else:
            click.echo("Shadow map: (empty)")


@cli.command()
@click.pass_context
def status(ctx: click.Context) -> None:
    """Show full device status (mode, jedec ids, map/patch counts, sniff)."""
    with open_device(ctx.obj["port"]) as dev:
        for k, v in dev.status().items():
            click.echo(f"{k:>14}: {v}")


@cli.command()
@click.pass_context
def version(ctx: click.Context) -> None:
    """Show firmware + protocol versions."""
    with open_device(ctx.obj["port"]) as dev:
        for l in dev.command("VERSION"):
            if l.startswith("VERSION "):
                click.echo(l[len("VERSION "):])


@cli.command()
@click.pass_context
def reboot(ctx: click.Context) -> None:
    """Reboot the controller."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Rebooting..." if dev.reboot() else "Reboot failed.")


# --- mode -------------------------------------------------------------------
@cli.command("mode")
@click.argument("value", type=click.Choice(["shadow", "hotpatch", "pass-through"]))
@click.pass_context
def mode_set(ctx: click.Context, value: str) -> None:
    """Set operating mode."""
    with open_device(ctx.obj["port"]) as dev:
        ok = dev.set_mode(value)
        click.echo(f"Mode set to {value}." if ok else "Failed to set mode.")


# --- bank (dual-BIOS) ----------------------------------------------------------
@cli.command("bank")
@click.argument("bank", type=click.Choice(["0", "1"]))
@click.pass_context
def bank_cmd(ctx: click.Context, bank: str) -> None:
    """Switch between virtual BIOS banks (0=image slot A, 1=image slot B)."""
    with open_device(ctx.obj["port"]) as dev:
        ok = dev.set_bank(int(bank))
        click.echo(f"Bank set to {bank}." if ok else "Failed to set bank.")


# --- map --------------------------------------------------------------------
@cli.group("map")
def map_cmd() -> None:
    """Manage shadow map entries."""


@map_cmd.command("add")
@click.option("--start", required=True, help="Start address (e.g. 0xFF0000)")
@click.option("--size", required=True, help="Region size (e.g. 64K)")
@click.option("--source", type=click.Choice(["ext", "orig"]), default="ext")
@click.option("--ext-offset", default="0", help="Offset in extension flash")
@click.pass_context
def map_add(ctx, start, size, source, ext_offset):
    """Add a shadow map entry."""
    start_addr = parse_int(start)
    region_size = parse_int(size)
    ext_off = parse_int(ext_offset)
    with open_device(ctx.obj["port"]) as dev:
        ok = dev.map_add(start_addr, region_size, source, ext_off)
        click.echo("Entry added." if ok else "Failed to add entry.")


@map_cmd.command("remove")
@click.argument("index", type=int)
@click.pass_context
def map_remove(ctx, index):
    """Remove a shadow map entry by index."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Removed." if dev.map_remove(index) else "Failed to remove.")


@map_cmd.command("clear")
@click.pass_context
def map_clear(ctx):
    """Remove all shadow map entries."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Cleared." if dev.map_clear() else "Failed to clear.")


@map_cmd.command("list")
@click.pass_context
def map_list(ctx):
    """List shadow map entries."""
    with open_device(ctx.obj["port"]) as dev:
        entries = dev.get_map()
        if not entries:
            click.echo("(empty)")
            return
        for e in entries:
            click.echo(f"[{e['index']}] 0x{e['start']:08X} {e['size']} -> "
                       f"{e['source']} (ext 0x{e['ext_offset']:X})")


# --- patch ------------------------------------------------------------------
@cli.group("patch")
def patch_cmd() -> None:
    """Manage hot-patch entries."""


@patch_cmd.command("add")
@click.option("--addr", required=True, help="Target byte address (e.g. 0xFF0020)")
@click.option("--orig", "orig", default="0xFF",
              help="Expected original byte (0xFF = wildcard, default)")
@click.option("--new", "new", required=True, help="Replacement byte")
@click.pass_context
def patch_add(ctx, addr, orig, new):
    """Add a hot-patch entry (orig -> new at addr)."""
    a, o, n = parse_int(addr) & 0xFFFFFFFF, parse_int(orig) & 0xFF, parse_int(new) & 0xFF
    with open_device(ctx.obj["port"]) as dev:
        ok = dev.patch_add(a, o, n)
        click.echo("Patch added." if ok else "Failed to add patch.")


@patch_cmd.command("list")
@click.pass_context
def patch_list(ctx):
    """List hot-patch entries."""
    with open_device(ctx.obj["port"]) as dev:
        for p in dev.patch_list():
            click.echo(f"[{p['index']}] 0x{p['addr']:08X}: "
                       f"0x{p['orig']:02X} -> 0x{p['new']:02X} "
                       f"({'on' if p['enabled'] else 'off'})")


@patch_cmd.command("remove")
@click.argument("index", type=int)
@click.pass_context
def patch_remove(ctx, index):
    """Remove a patch by index."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Removed." if dev.patch_remove(index) else "Failed to remove.")


@patch_cmd.command("clear")
@click.pass_context
def patch_clear(ctx):
    """Clear all patches."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Cleared." if dev.patch_clear() else "Failed to clear.")


# --- flash ------------------------------------------------------------------
@cli.group("flash")
def flash_cmd() -> None:
    """Original + extension flash operations."""


@flash_cmd.command("backup")
@click.argument("outfile", type=click.Path())
@click.option("--length", default="16M", help="Bytes to read (default 16M)")
@click.pass_context
def flash_backup(ctx, outfile, length):
    """Backup original BIOS chip to a host file."""
    n = parse_int(length)
    with open_device(ctx.obj["port"]) as dev:
        click.echo(f"Reading {n} bytes from original chip...")
        data = dev.flash_backup(n)
        if data is None:
            click.echo("Backup failed (no data / CRC mismatch).")
            raise SystemExit(1)
        Path(outfile).write_bytes(data)
        click.echo(f"Saved {len(data)} bytes -> {outfile}")


@flash_cmd.command("restore")
@click.argument("infile", type=click.Path(exists=True))
@click.argument("ext_offset",
                default="0x2000")  # ext flash offset to stage the image
@click.pass_context
def flash_restore(ctx, infile, ext_offset):
    """Stage a BIOS image into ext flash (for later redirect/emulate).

    The image lands in the ext-flash image store; redirect it into the BIOS
    address space with `vbfc-cli map add --start <bios-addr> --size <n> \
    --source ext --ext-offset <ext-offset>` then `mode shadow`.
    """
    data = Path(infile).read_bytes()
    off = parse_int(ext_offset)
    with open_device(ctx.obj["port"]) as dev:
        click.echo(f"Uploading {len(data)} bytes -> ext 0x{off:X}...")
        ok = dev.upload_to_ext(data, off)
        if ok:
            click.echo(f"Image staged at ext 0x{off:X}.")
        else:
            click.echo("Restore/upload failed.")
            raise SystemExit(1)


@flash_cmd.command("read")
@click.argument("which", type=click.Choice(["ext", "orig"]))
@click.argument("offset")
@click.argument("length")
@click.argument("outfile", type=click.Path())
@click.pass_context
def flash_read(ctx, which, offset, length, outfile):
    """Read raw bytes from ext or orig flash to a file."""
    off, n = parse_int(offset), parse_int(length)
    with open_device(ctx.obj["port"]) as dev:
        click.echo(f"Reading {n} bytes from {which} @ 0x{off:X}...")
        data = dev.flash_read_ext(off, n) if which == "ext" \
            else dev.flash_read_orig(off, n)
        if data is None:
            click.echo("Read failed.")
            raise SystemExit(1)
        Path(outfile).write_bytes(data)
        click.echo(f"Saved {len(data)} bytes -> {outfile}")


@flash_cmd.command("erase")
@click.argument("offset")
@click.argument("length")
@click.pass_context
def flash_erase(ctx, offset, length):
    """Erase a range of ext-flash sectors (must be in the image store)."""
    off, n = parse_int(offset), parse_int(length)
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Erased." if dev.flash_erase(off, n) else "Erase failed.")


# --- image (convenience wrappers over upload/read) -------------------------
@cli.group("image")
def image_cmd() -> None:
    """Firmware image store (ext flash)."""


@image_cmd.command("upload")
@click.argument("infile", type=click.Path(exists=True))
@click.argument("ext_offset", default="0x2000")
@click.pass_context
def image_upload(ctx, infile, ext_offset):
    """Stage a firmware image at an ext-flash offset."""
    data = Path(infile).read_bytes()
    off = parse_int(ext_offset)
    with open_device(ctx.obj["port"]) as dev:
        ok = dev.upload_to_ext(data, off)
        click.echo(f"Uploaded {len(data)} bytes -> 0x{off:X}." if ok
                   else "Upload failed.")


@image_cmd.command("verify")
@click.argument("infile", type=click.Path(exists=True))
@click.argument("ext_offset", default="0x2000")
@click.pass_context
def image_verify(ctx, infile, ext_offset):
    """Verify staged image matches a local file (byte-for-byte)."""
    local = Path(infile).read_bytes()
    off = parse_int(ext_offset)
    with open_device(ctx.obj["port"]) as dev:
        remote = dev.flash_read_ext(off, len(local))
        if remote == local:
            click.echo(f"OK — {len(local)} bytes match at 0x{off:X}.")
        else:
            click.echo("MISMATCH.")
            raise SystemExit(1)


# --- sniff ------------------------------------------------------------------
@cli.group("sniff")
def sniff_cmd() -> None:
    """SPI traffic sniffer."""


@sniff_cmd.command("start")
@click.pass_context
def sniff_start(ctx):
    """Start capturing SPI transactions."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Sniffing started." if dev.sniff_start() else "Failed (bypass active?)")


@sniff_cmd.command("stop")
@click.pass_context
def sniff_stop(ctx):
    """Stop capturing."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Stopped." if dev.sniff_stop() else "Failed.")


@sniff_cmd.command("status")
@click.pass_context
def sniff_status(ctx):
    """Show sniff buffer status."""
    with open_device(ctx.obj["port"]) as dev:
        s = dev.sniff_status()
        if s:
            click.echo(f"sniff: {'on' if s['on'] else 'off'}, "
                       f"events {s['events']}/{s['capacity']}")
        else:
            click.echo("No status.")


@sniff_cmd.command("dump")
@click.option("--outfile", default=None, help="Write raw binary events here")
@click.pass_context
def sniff_dump(ctx, outfile):
    """Drain captured events and summarize them."""
    with open_device(ctx.obj["port"]) as dev:
        raw = dev.sniff_dump()
        if raw is None:
            click.echo("No sniff data.")
            return
        if outfile:
            Path(outfile).write_bytes(raw)
            click.echo(f"Wrote {len(raw)} bytes of event records -> {outfile}")
        # Decode 8-byte records for a human-readable summary.
        n = len(raw) // 8
        clicks = {"READ": 0, "FAST_READ": 0, "PAGE_PROG": 0, "SECTOR_ER": 0,
                  "RDID": 0, "OTHER": 0}
        cmds = {0x03: "READ", 0x0B: "FAST_READ", 0x02: "PAGE_PROG",
                0x20: "SECTOR_ER", 0x9F: "RDID"}
        for i in range(n):
            rec = raw[i * 8:(i + 1) * 8]
            cmd = rec[0]
            addr = int.from_bytes(rec[2:5], "little")
            cnt = int.from_bytes(rec[6:8], "little")
            name = cmds.get(cmd, "OTHER")
            clicks[name] += 1
            if i < 8 or i >= n - 4:
                click.echo(f"  {name:>9} addr=0x{addr:06X} cnt={cnt}")
            elif i == 8:
                click.echo("  ...")
        click.echo("Summary: " + ", ".join(f"{k}={v}" for k, v in clicks.items() if v))


# --- key management ----------------------------------------------------------
@cli.group("key")
def key_cmd() -> None:
    """Manage signing keys."""

@key_cmd.command("gen")
def key_gen() -> None:
    """Generate a 32-byte HMAC-SHA256 key and save to ~/.vbfc/."""
    generate_keypair(DEFAULT_KEY_DIR)


# --- image signing -----------------------------------------------------------
@cli.group("image")
def image_cmd() -> None:
    """Sign and verify firmware images."""

@image_cmd.command("sign")
@click.argument("infile", type=click.Path(exists=True))
@click.argument("outfile", type=click.Path())
@click.option("--version", default=1, help="Image version (monotonic anti-rollback)")
@click.option("--key", default=None, help="Path to HMAC key file (default ~/.vbfc/hmac.key)")
@click.pass_context
def image_sign(ctx: click.Context, infile: str, outfile: str, version: int, key: str | None) -> None:
    """Sign a raw firmware image with HMAC-SHA256 and prepend the 256-byte header."""
    key_path = Path(key) if key else (DEFAULT_KEY_DIR / "hmac.key")
    if not key_path.exists():
        click.echo(f"Key not found: {key_path}. Generate one with `vbfc-cli key gen`.")
        raise click.Abort()
    key_data = key_path.read_bytes()
    payload = Path(infile).read_bytes()
    signed = sign_payload(payload, key_data, image_version=version)
    Path(outfile).write_bytes(signed)
    click.echo(f"Signed image written to {outfile} ({len(signed)} bytes, v{version})")

@image_cmd.command("verify")
@click.argument("infile", type=click.Path(exists=True))
@click.option("--key", default=None, help="Path to HMAC key file (default ~/.vbfc/hmac.key)")
@click.pass_context
def image_verify(ctx: click.Context, infile: str, key: str | None) -> None:
    """Verify the signed image header HMAC-SHA256."""
    key_path = Path(key) if key else (DEFAULT_KEY_DIR / "hmac.key")
    if not key_path.exists():
        click.echo(f"Key not found: {key_path}.")
        raise click.Abort()
    key_data = key_path.read_bytes()
    signed = Path(infile).read_bytes()
    result = verify_image(signed, key_data)
    for k, v in result.items():
        click.echo(f"{k:>15}: {v}")
    if not result.get("valid"):
        click.echo("SIGNATURE INVALID — do not upload this image.")
        raise click.Abort()


# --- BIOS analysis ----------------------------------------------------------
@cli.command()
@click.argument("romfile", type=click.Path(exists=True))
@click.pass_context
def analyze(ctx: click.Context, romfile: str) -> None:
    """Analyze a ROM dump for hidden/locked BIOS features and emit patch suggestions."""
    rom = Path(romfile).read_bytes()
    click.echo(f"Analyzing {romfile} ({len(rom)} bytes)...")
    analysis = analyze_rom(rom)
    click.echo(f"\nFirmware type: {analysis['fw_type']}")
    click.echo(f"SHA-256: {analysis['sha256']}")
    click.echo("\nDetected structures:")
    for s in analysis["detected_structures"]:
        click.echo(f"  [{s['type']}] at 0x{s['offset']:X}: {s['note']}")
    click.echo(f"\nFlagged variables ({len(analysis['flagged_variables'])}):")
    for fv in analysis["flagged_variables"]:
        click.echo(f"  · {fv['name']} (0x{fv['offset']:X}, attrs=0x{fv['attr']:02X}) — {', '.join(fv.get('hints', []))}")
    click.echo(f"\nPatch suggestions ({len(analysis['patch_suggestions'])}):")
    for ps in analysis["patch_suggestions"]:
        click.echo(f"  # {ps['description']}")
        click.echo(f"  {ps['suggested_command']}")
        if ps.get('hints'):
            click.echo(f"  #    hint: {'; '.join(ps['hints'])}")
    click.echo(f"\nDisabled PCI devices detected ({len(analysis['disabled_pci_devices'])}):")
    for pci in analysis["disabled_pci_devices"]:
        click.echo(f"  · {pci['note']}")
    click.echo(f"\nChipset port/register findings ({len(analysis['chipset_ports'])}):")
    for port in analysis["chipset_ports"]:
        click.echo(f"  · {port['note']}")
    click.echo(f"\nTo apply: send PATCH ADD commands to the VBFC device via serial.")


# --- block check (#1) ----------------------------------------------------------
@cli.command("check")
@click.argument("romfile", type=click.Path(exists=True))
@click.option("--good", default=None, help="Known-good ROM for comparison")
@click.option("--block-size", default="4k", help="Block size: 4k, 64k, 256k, 1m")
@click.pass_context
def block_check(ctx: click.Context, romfile: str, good: str | None, block_size: str) -> None:
    """Check ROM for defective blocks (CRC32 per block, diff against known-good)."""
    rom = Path(romfile).read_bytes()
    good_rom = Path(good).read_bytes() if good else None
    bs = {"4k": 4096, "64k": 65536, "256k": 262144, "1m": 1048576}.get(block_size, 4096)
    expected = len(good_rom) if good_rom else None
    result = check_rom(rom, good_rom, bs, expected)
    click.echo(f"  size: {result['size']} bytes, size OK: {result['size_ok']}")
    click.echo(f"  SHA-256: {result['sha256']}")
    click.echo(f"  blocks: {result['total_blocks']}, good: {result['good_blocks']}, bad: {len(result['bad_blocks'])}")
    click.echo(f"  COMPLIANCE: {'PASS' if result['compliance'] else 'FAIL'}")
    for b in result["bad_blocks"][:20]:
        click.echo(f"  [x] block {b['index']} at 0x{b['offset']:X} CRC={b['rom_crc32']} good CRC={b['good_crc32']} ({b['severity']})")
    if len(result["bad_blocks"]) > 20:
        click.echo(f"  ... and {len(result['bad_blocks'])-20} more bad blocks")
    if good:
        click.echo(f"\n  total CRC (ROM): 0x{result['total_crc']:08X}")


# --- factory reset ----------------------------------------------------------
@cli.command("factory-reset")
@click.pass_context
def factory_reset(ctx: click.Context) -> None:
    """Restore factory defaults (pass-through mode, clear map+patches+sniff)."""
    with open_device(ctx.obj["port"]) as dev:
        click.echo("Factory reset complete." if dev.factory_reset()
                   else "Factory reset failed.")


if __name__ == "__main__":
    cli()
