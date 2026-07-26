#!/usr/bin/env python3
"""
BIOS structure analysis — detect hidden/locked features in a ROM dump.

Pure host-side. Takes a raw BIOS dump (from `DUMP ORIG` or `FLASH BACKUP`),
scans for known signatures, locates setup/NVRAM regions, and emits concrete
`PATCH ADD` commands the VBFC device can apply to unlock hidden options.

Two detection paths:
- UEFI: locate the `EFI PART` GUID and parse variable stores for locked
         setup entries
- Legacy (AMI): locate the ROMSIP setup-table bytes and flag non-default
                values that differ from the packed default block
"""
import struct
import hashlib
from typing import Union

# ── signatures ──────────────────────────────────────────────────────────────

EFI_PART_GUID = b"\x89\xC6\x09\x56\x73\x41\xE0\x41\xB3\xE0\x9F\xD0\x81\xE1\x38\xBC"
# "EFI PART" as u64-le = bytes(0x5452415020494645)
EFI_PART_SIG = b"EFI PART"

# UEFI variable store header
VSTORE_SIG = b"_ASL_"  # common for AMI UEFI NVRAM
EFI_GLOBAL_VARIABLE = "8be4df61-93ca-11d2-aa0d-00e098032b8c"
SETUP_GUIDS = [
    "e1c9f8ba-c332-4dd9-9b77-03663c03d043",  # common AMI Setup
    "ec87d643-eba4-4bb2-aade-6d3e00523a2b",  # Aptio Setup
]

# Legacy AMI signatures
AMIBIOS8_SIG = b"AMIBOOT"
ROMSIP_SIG = b"$ROM$X"   # AMI ROMSIP (setup table) — often "$ROM$X" or "$ROMSIP"
CORESEC_SIG = b"COREBOOT"

# ── helpers ─────────────────────────────────────────────────────────────────

def _find_all(haystack: bytes, needle: bytes, step: int = 1) -> list[int]:
    """Return all offsets where `needle` appears in `haystack`."""
    res, pos = [], 0
    while True:
        pos = haystack.find(needle, pos)
        if pos == -1:
            break
        res.append(pos)
        pos += step
    return res

# ── UEFI parser ─────────────────────────────────────────────────────────────

_GUID_CHARS = "0123456789abcdef-"


def _guid_str_to_bytes(guid: str) -> bytes:
    """Convert a 36-char GUID string ('XXXXXXXX-...') into bytes (LE fields)."""
    g = guid.lower().replace("-", "")
    return bytes.fromhex(g[6:8] + g[4:6] + g[2:4] + g[0:2] +  # Data1 (LE)
                          g[10:12] + g[8:10] +                  # Data2 (LE)
                          g[14:16] + g[12:14] +                  # Data3 (LE)
                          g[16:])                                # Data4 (as-is)


def _find_variable_stores(rom: bytes) -> list[int]:
    """Find offsets of UEFI variable stores using the `_ASL_` signature."""
    return _find_all(rom, VSTORE_SIG)


def _find_setup_guids(rom: bytes) -> list[dict]:
    """Search for known Setup GUIDs in the ROM and return {'offset': ..., 'guid': ...}."""
    results = []
    setup_good = [_guid_str_to_bytes(g) for g in SETUP_GUIDS]
    for i in range(0, len(rom) - 16, 1):
        chunk = rom[i:i+16]
        for sg, guid_str in zip(setup_good, SETUP_GUIDS):
            if chunk == sg:
                results.append({"offset": i, "guid": guid_str})
                break
    return results


def _extract_variable_names_around_guid(rom: bytes, guid_offset: int) -> list[dict]:
    """Rudimentary UEFI variable extraction near a found GUID.

    UEFI variables are stored in a serialised stream: GUID + attributes +
    name_size + data_size + name (UTF-16LE) + data. We scan a window
    backwards from the GUID offset to find plausible variable headers.
    """
    # This is heuristic: look for non-volatile + boot-service attribute bits
    # (0x07 = EFI_VARIABLE_NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS)
    variables = []
    scan_start = max(0, guid_offset - 0x400)
    i = scan_start
    while i < guid_offset:
        if i + 4 > len(rom):
            break
        # Look for a plausible variable: 4-byte state XOR'd with 0x55AA
        if rom[i] == 0xAA and rom[i+1] == 0x55:
            # Possible VARA header start — skip ahead to the attributes byte
            attr_off = i + 4
            if attr_off + 4 <= guid_offset:
                attrs = struct.unpack_from("<I", rom, attr_off)[0]
                # Common attributes for setup variables
                if attrs == 0x07 or attrs == 0x03 or attrs == 0x27:
                    # Name size at attr_off + 4, Data size at attr_off + 8
                    name_sz = struct.unpack_from("<I", rom, attr_off + 4)[0]
                    if 0 < name_sz < 512:
                        name_start = attr_off + 20
                        if name_start + name_sz <= guid_offset:
                            try:
                                name = rom[name_start:name_start+name_sz].decode("utf-16-le", errors="replace")
                                variables.append({
                                    "offset": i,
                                    "attr": attrs,
                                    "name": name.strip(),
                                    "is_setup": True,
                                })
                            except Exception:
                                pass
        i += 1
    return variables


def _flag_hidden_vars(variables: list[dict]) -> list[dict]:
    """Flag variables whose names or attributes suggest a locked/hidden option
    that could be unlocked via patch."""
    flagged = []
    for v in variables:
        name = v.get("name", "").lower()
        hints = []
        if "hidden" in name:
            hints.append("name contains 'hidden'")
        if "lock" in name:
            hints.append("name contains 'lock'")
        if "setup" in name and v.get("attr", 0) == 0x07:
            hints.append("standard setup variable (may contain sub-options)")
        if 0 < v.get("attr", 7) < 7:
            hints.append(f"unusual attrs=0x{v['attr']:02X}")
        if hints:
            flagged.append({**v, "hints": hints})
    return flagged


# ── Legacy AMI parser ──────────────────────────────────────────────────────

def _find_romsip(rom: bytes) -> list[dict]:
    """Find ROMSIP entries in an AMI Legacy BIOS.

    Returns list of offsets with the signature and surrounding context.
    """
    results = []
    for sig, label in [(ROMSIP_SIG, "ROMSIP"), (b"$ROMSIP", "ROMSIP-long")]:
        for off in _find_all(rom, sig, step=1):
            # ROMSIP typically has a 4-byte size word after the signature
            size_val = struct.unpack_from("<I", rom, off + len(sig))[0] if off + len(sig) + 4 <= len(rom) else 0
            results.append({
                "offset": off,
                "signature": sig,
                "label": label,
                "size": size_val,
                "data_table_offset": off + len(sig) + 4 if size_val else 0,
            })
            break  # one per scan
    return results


def _analyze_romsip_defaults(rom: bytes, entry: dict) -> list[dict]:
    """Flag ROMSIP entries that differ from their default-packed block.

    AMI setup stores a default block and a current/working block. If the
    working block differs from the default block, those offsets represent
    options the user (or OEM) has changed — and hidden options are often
    set to 'disabled' in the default block but can be force-enabled.
    """
    findings = []
    table_off = entry.get("data_table_offset", 0)
    sz = entry.get("size", 0)
    if not table_off or sz < 64:
        return findings

    # Heuristic: look ahead 256 bytes; if there's a mirrored block (identical
    # region ~512B later), flag it as a potential override point.
    block_size = 256
    for offset in (table_off, table_off + 64, table_off + 128):
        if offset + block_size > len(rom):
            break
        b1 = rom[offset:offset+block_size]
        b2_offset = offset + 512
        if b2_offset + block_size <= len(rom):
            b2 = rom[b2_offset:b2_offset+block_size]
            if b1 == b2:
                findings.append({
                    "offset": offset,
                    "type": "mirrored_default_block",
                    "note": "possible setup-option double buffer — toggle this region to alter locked defaults",
                })
    return findings


# ── PCI device / chipset port disable detection ────────────────────────────

# Common PCI vendor IDs
KNOWN_VENDORS = {0x8086: "Intel", 0x1022: "AMD", 0x10DE: "NVIDIA",
                 0x1106: "VIA", 0x1002: "AMD/ATI", 0x14E4: "Broadcom",
                 0x168C: "Qualcomm", 0x10EC: "Realtek", 0x1B4B: "Marvell",
                 0x8087: "Intel"}

# PCH/SB register offsets that control port enables (Intel PCH-centric)
# These live inside the BIOS image as part of the chipset init tables
# (typically in the MRC/Coreboot or UEFI PEI phase).
PCH_PORT_REGISTERS = {
    "PCH_PCIE_PORT_ENABLE": [0xE0,  # PCIe root port enable mask
                             "0:port 0,1:port 1,...; bit=0 => port disabled"],
    "PCH_USB_OC_PIN": [0xEC,       # USB overcurrent pin mapping
                        "bit pair per port"],
    "PCH_SATA_PORT_ENABLE": [0x94,  # SATA port enable (PCH SIR)
                              "bit per port; 0=disabled"],
    "PCH_LPC_ENABLE": [0x80,        # LPC I/O enable
                        "bit0=COM1, bit1=COM2, bit2=LPT, bit3=floppy"],
}


def _find_pci_config_headers(rom: bytes) -> list[dict]:
    """Scan for PCI configuration space headers that appear disabled.

    A disabled PCI device has vendor ID = 0xFFFF or 0x0000 in its config
    header. We find these by scanning the ROM for the *enabled* counterparts
    (known vendor IDs) and flag peers that look disabled.
    """
    findings = []
    # Scan for known-live vendor IDs and check nearby offsets for their
    # "disabled" counterparts
    for i in range(0, len(rom) - 4, 256):  # PCI config = 256B aligned
        vid = struct.unpack_from("<H", rom, i)[0]
        did = struct.unpack_from("<H", rom, i + 2)[0]
        # Known enabled device
        if vid in KNOWN_VENDORS and did != 0xFFFF and did != 0x0000:
            # Check if there's a mirror offset nearby with vid=0xFFFF
            # (common in UEFI DXE: the flash has both "enabled" and "disabled" tables)
            for delta in (0x100, -0x100, 0x200, -0x200, 0x400):
                peer = i + delta
                if 0 <= peer < len(rom) - 4:
                    pvid = struct.unpack_from("<H", rom, peer)[0]
                    pdid = struct.unpack_from("<H", rom, peer + 2)[0]
                    if (pvid == 0xFFFF or pvid == 0x0000) and pdid == 0xFFFF:
                        findings.append({
                            "type": "DISABLED_PCI_DEVICE",
                            "enabled_offset": i,
                            "disabled_offset": peer,
                            "vendor_id": vid,
                            "vendor_name": KNOWN_VENDORS.get(vid, "Unknown"),
                            "device_id": did,
                            "note": f"PCI device 0x{vid:04X}:0x{did:04X} has a disabled mirror at 0x{peer:X}",
                        })
                        break
    return findings


def _find_chipset_port_disable(rom: bytes) -> list[dict]:
    """Find chipset port-disable registers by looking for the PCH IBASE or
    chipset init table signatures."""
    findings = []
    # Look for PCH chipset init: commonly TSEGMB/... but easily ID'd by
    # scanning for POWER_MGMT / DMIC / PcieRootPort sequences
    known_sigs = [
        (b"PCH", "PCH_BASE"),
        (b"PM_TMR", "PM_TIMER_BLOCK"),
        (b"GPIO_BASE", "GPIO_REGISTER"),
        (b"SB_REG", "SB_REGION"),
    ]
    for sig_bytes, sig_name in known_sigs:
        for off in _find_all(rom, sig_bytes):
            for reg_name, (reg_offset, desc) in PCH_PORT_REGISTERS.items():
                check = off + reg_offset
                if check + 4 <= len(rom):
                    val = struct.unpack_from("<I", rom, check)[0]
                    # Non-zero often means "enabled" — zero means disabled
                    if val == 0:
                        findings.append({
                            "type": "DISABLED_REGISTER",
                            "signature": sig_name,
                            "sig_offset": off,
                            "reg_offset": check,
                            "register": reg_name,
                            "value": val,
                            "note": f"{sig_name}+0x{reg_offset:X}: {reg_name} = 0x{val:08X} — port likely disabled ({desc})",
                        })
                    else:
                        findings.append({
                            "type": "ENABLED_REGISTER",
                            "signature": sig_name,
                            "sig_offset": off,
                            "reg_offset": check,
                            "register": reg_name,
                            "value": val,
                            "note": f"{sig_name}+0x{reg_offset:X}: {reg_name} = 0x{val:08X} — port enabled",
                        })
            break  # one pass per signature match
    return findings


# ── entry point ─────────────────────────────────────────────────────────────

def analyze_rom(rom: bytes) -> dict:
    """Full analysis of a raw BIOS ROM dump.

    Returns a dict with firmware type, detected structures, flagged items,
    and generated PATCH ADD commands.
    """
    result = {
        "fw_type": None,
        "size": len(rom),
        "sha256": hashlib.sha256(rom).hexdigest(),
        "detected_structures": [],
        "flagged_variables": [],
        "disabled_pci_devices": [],
        "chipset_ports": [],
        "patch_suggestions": [],
    }

    # ── 1. Detect firmware type ──
    for off in _find_all(rom, EFI_PART_SIG):
        result["fw_type"] = "UEFI"
        result["detected_structures"].append({
            "type": "EFI_PART_TABLE",
            "offset": off,
            "note": f"UEFI partition table at 0x{off:X}",
        })
        break

    if result["fw_type"] is None:
        for off in _find_all(rom, AMIBIOS8_SIG):
            result["fw_type"] = "Legacy (AMI)"
            result["detected_structures"].append({
                "type": "AMIBIOS8",
                "offset": off,
                "note": f"AMIBIOS8 boot block at 0x{off:X}",
            })
            break

    if result["fw_type"] is None:
        for off in _find_all(rom, CORESEC_SIG):
            result["fw_type"] = "Legacy (Coreboot)"
            result["detected_structures"].append({
                "type": "COREBOOT",
                "offset": off,
                "note": f"Coreboot anchor at 0x{off:X}",
            })
            break

    if result["fw_type"] is None:
        result["fw_type"] = "Unknown"

    # ── 2. UEFI: find variable stores + setup GUIDs + flag hidden vars ──
    if result["fw_type"] == "UEFI":
        for off in _find_variable_stores(rom):
            result["detected_structures"].append({
                "type": "VARIABLE_STORE",
                "offset": off,
                "note": f"UEFI variable store (_ASL_) at 0x{off:X}",
            })

        for sg in _find_setup_guids(rom):
            result["detected_structures"].append({
                "type": "SETUP_GUID",
                "offset": sg["offset"],
                "note": f"Setup GUID {sg['guid']} at 0x{sg['offset']:X}",
            })
            variables = _extract_variable_names_around_guid(rom, sg["offset"])
            flagged = _flag_hidden_vars(variables)
            result["flagged_variables"].extend(flagged)
            for fv in flagged:
                addr_hex = f"0x{fv['offset']:08X}"
                result["patch_suggestions"].append({
                    "addr": fv["offset"],
                    "description": f"UEFI variable '{fv['name']}' (attrs=0x{fv['attr']:02X}) — location 0x{fv['offset']:X}",
                    "hints": fv.get("hints", []),
                    "suggested_command": f"PATCH ADD {addr_hex} FF FF",
                })

    # ── 3. Legacy: find ROMSIP + analyze defaults ──
    if "Legacy" in (result["fw_type"] or ""):
        for romsip in _find_romsip(rom):
            result["detected_structures"].append({
                "type": romsip["label"],
                "offset": romsip["offset"],
                "note": f"ROMSIP at 0x{romsip['offset']:X}, size={romsip['size']}",
            })
            for finding in _analyze_romsip_defaults(rom, romsip):
                result["patch_suggestions"].append({
                    "addr": finding["offset"],
                    "description": finding["note"],
                    "hints": ["read-back the current byte at this offset to determine the orig_byte"],
                    "suggested_command": f"PATCH ADD 0x{finding['offset']:08X} <orig_byte> <new_byte>",
                })

    # ── 4. PCI disabled device detection ──
    for pci in _find_pci_config_headers(rom):
        result["disabled_pci_devices"].append(pci)
        off = pci["disabled_offset"]
        result["patch_suggestions"].append({
            "addr": off,
            "description": f"PCI device {pci['vendor_name']} {pci['vendor_id']:04X}:{pci['device_id']:04X} — disabled mirror at 0x{off:X}, enable by patching vendor/device ID",
            "hints": ["set vendor ID to the enabled counterpart's VID", "read the 'enabled_offset' 0x{pci['enabled_offset']:X} to get the correct bytes"],
            "suggested_command": f"PATCH ADD 0x{off:08X} FF FF <vendor_id_lo> (and second patch for second byte)",
        })

    # ── 5. Chipset port/bus disable detection ──
    for port in _find_chipset_port_disable(rom):
        result["chipset_ports"].append(port)
        if port["type"] == "DISABLED_REGISTER":
            result["patch_suggestions"].append({
                "addr": port["reg_offset"],
                "description": port["note"],
                "hints": ["set the register bits to enable the corresponding port"],
                "suggested_command": f"PATCH ADD 0x{port['reg_offset']:08X} 00 FF",
            })

    return result