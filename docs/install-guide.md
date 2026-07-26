# VBFC Install Guide

## Prerequisites

- SOIC-8 BIOS chip on target motherboard (3.3 V, single-SPI up to 50 MHz)
- Soldering station with hot air or SOIC-8 socket adapter
- USB-C cable for programming
- Host PC with Python 3.10+

## Hardware Installation

1. **Power off** and disconnect all power sources.
2. Identify the SPI BIOS chip (typically 8-pin SOIC near the PCH).
3. **Option A — Socket adapter:** Use a SOIC-8 test clip during development.
4. **Option B — Permanent install:**
   - Desolder original BIOS chip.
   - Solder VBFC interposer PCB to motherboard pads.
   - Seat original BIOS chip on interposer footprint.
5. Verify **bypass jumper is OPEN** (controller active).
6. Connect USB-C programming cable.

## First Boot

1. Power on with bypass jumper **OPEN**.
2. Board should boot normally (empty shadow map = full pass-through).
3. Connect USB and run:

   ```bash
   pip install -e host/vbfc-cli
   vbfc-cli scan
   ```

4. Expected output: device detected, mode=pass-through, firmware version.

## Enabling Shadow Region

```bash
# Upload 64 KB payload to extension flash offset 0
vbfc-cli upload --file payload.bin --offset 0

# Configure shadow map (default top 64 KB)
vbfc-cli map add --start 0xFF0000 --size 64K --source ext --ext-offset 0

# Switch to shadow mode
vbfc-cli mode shadow

# Reboot target and verify
vbfc-cli scan
```

## Recovery

| Problem | Fix |
|---------|-----|
| Boot failure after shadow enable | Close **bypass jumper**, power cycle |
| Corrupt shadow map | `vbfc-cli factory-reset` |
| USB not responding | Close bypass jumper, reflash firmware |
| Bricked controller | Original BIOS chip still works with bypass closed |

## Pin Reference (Interposer)

| SOIC-8 Pin | Signal | VBFC Connection |
|------------|--------|-----------------|
| 1 | CS# | RP2040 GPIO + original chip |
| 2 | SO (MISO) | Tri-state mux |
| 3 | WP# | Pass-through |
| 4 | GND | Common ground |
| 5 | SI (MOSI) | Pass-through |
| 6 | SCK | Pass-through |
| 7 | HOLD# | Pass-through |
| 8 | VCC | 3.3 V |

See `hardware/interposer-soic8/README.md` for full schematic.
