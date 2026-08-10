# VBFC Real Hardware Test Phase — Gigabyte G41 Motherboard

> **Document Version**: 1.0  
> **Target Board**: Gigabyte G41 (LGA 775, ICH7/ICH7R southbridge)  
> **Typical BIOS Chip**: Macronix MX25L6406E or Winbond W25Q64 (8 MB, SOIC-8, 3.3 V)  
> **VBFC Hardware**: Rev 1.2 PCB, RP2040 firmware ≥ v0.9.0, `vbfc-host` ≥ 0.9.0  

---

## ⚠️ Safety & Recovery First

| Risk | Mitigation |
|------|------------|
| **Bricked board** | Keep CH341A programmer + SOIC-8 clip on bench; stock BIOS dump saved |
| **Corrupted SPI traffic** | VBFC bypass jumper (JP1) forces stock flash passthrough |
| **ESD damage** | Wear wrist strap; work on anti-static mat |
| **Clip misalignment** | Verify pin-1 dot on chip matches clip marker before power |

**Recovery command (if needed):**
```cmd
flashrom -p ch341a_spi -w E:\g41_stock_bios_YYYYMMDD.bin -c "MX25L6406E"
```

---

## Phase 0 — Prerequisites Checklist

| Item | Done? |
|------|-------|
| VBFC rev 1.2 assembled & RP2040 flashed (`vbfc.uf2`) | ☐ |
| `vbfc-host` installed (`pip install vbfc-host`) | ☐ |
| USB flash drive mounted as **E:** with ≥ 50 MB free | ☐ |
| SOIC-8 clip (Pomona 5250 or equiv.) | ☐ |
| CH341A programmer + `flashrom` (Windows) | ☐ |
| Target board: Gigabyte G41, BIOS chip identified | ☐ |
| Stock BIOS dump saved & SHA-256 verified | ☐ |

---

## Phase 1 — Identify BIOS Chip

```powershell
# Run as Administrator
flashrom -p ch341a_spi -R
```

**Record:**
- Vendor/Model: _______________ (e.g., `Macronix MX25L6406E`)
- Capacity: _______________ (expected 8 MB = 8,388,608 bytes)
- Package: SOIC-8, 150 mil
- Pin-1 orientation: _______________

> This determines shadow map capacity and supported SPI commands.

---

## Phase 2 — Dump Stock BIOS (Baseline)

```cmd
flashrom -p ch341a_spi -r E:\g41_stock_bios_%DATE:~10,4%%DATE:~4,2%%DATE:~7,2%.bin -c "MX25L6406E"
```

**Verify:**
```cmd
dir E:\g41_stock_bios_*.bin
certutil -hashfile E:\g41_stock_bios_*.bin SHA256
```
- File size must be **8,388,608 bytes** (8 MB)
- Save SHA-256 hash: `________________________________________`

---

## Phase 3 — Prepare Modified Firmware

1. Open `E:\g41_stock_bios_*.bin` in **UEFITool** (v0.28+).
2. Locate target region (G41 typical layout):
   - **Setup DXE driver**: GUID `899407D7-99FE-43D8-9A21-79EC328CAC21` (or search "Setup")
   - **Boot logo / POST text**: in `FV_MAIN` or `FV_SUPV`
   - **ME region**: G41/ICH7 usually **no ME** — skip unless present
3. Extract region → modify binary (example: unlock hidden menu):
   - Find conditional jump suppressing "Advanced" tab
   - Patch `74 XX` → `EB XX` (JZ → JMP short)
4. Save **full modified image** as `E:\g41_modified_bios.bin`.

---

## Phase 4 — Provision VBFC

```cmd
# 1. Connect VBFC via USB-C to Windows PC
# 2. Sign modified image (version 1 = first deployment)
vbfc-host sign --input E:\g41_modified_bios.bin --output E:\g41_signed_bios.bin --version 1

# 3. Flash to VBFC extension flash (slot 0 = active)
vbfc-host flash --image E:\g41_signed_bios.bin --slot 0

# 4. Configure shadow map for 8 MB chip
#    Region 0: 0x000000–0x0FFFFF (PEI/SEC) → original flash
#    Region 1: 0x100000–0x1FFFFF (Setup DXE) → hybrid + patching
#    Region 2: 0x200000–0x7FFFFF (rest) → extension flash
vbfc-host shadow add --base 0x000000 --limit 0x0FFFFF --type original --offset 0x000000
vbfc-host shadow add --base 0x100000 --limit 0x1FFFFF --type hybrid --offset 0x100000 --patch-enable
vbfc-host shadow add --base 0x200000 --limit 0x7FFFFF --type extension --offset 0x200000
vbfc-host shadow commit

# 5. Verify
vbfc-host info
vbfc-host verify --slot 0
```

**Expected `vbfc-host info` output:**
```
Firmware: 0.9.0
Flash ID: 0xEF4017 (W25Q128) or 0xC22017 (MX25L128)
Shadow Map Entries: 3
  [0] 0x000000-0x0FFFFF → ORIGINAL @ 0x000000
  [1] 0x100000-0x1FFFFF → HYBRID @ 0x100000 (patch enabled)
  [2] 0x200000-0x7FFFFF → EXTENSION @ 0x200000
```

---

## Phase 5 — Wire VBFC to Motherboard

| VBFC Pin | SOIC-8 Pin | Clip Pin | Signal | Notes |
|----------|------------|----------|--------|-------|
| CS#      | 1          | 1        | CS#    | Active low |
| SCK      | 6          | 6        | CLK    | 50 MHz max |
| MOSI     | 5          | 5        | DI     | Board → VBFC → chip |
| MISO     | 2          | 2        | DO     | **VBFC drives this** |
| VCC      | 8          | 8        | 3.3 V  | From board |
| GND      | 4          | 4        | GND    | Common |
| WP#      | 3          | 3        | WP#    | Leave floating (VBFC pulls high) |
| HOLD#    | 7          | 7        | HOLD#  | Leave floating (VBFC pulls high) |

**Physical steps:**
1. Power OFF motherboard, unplug ATX PSU cord.
2. Align clip pin-1 marker with chip pin-1 dot (△ or notch).
3. Press clip firmly — verify all 8 pins contact.
4. Connect VBFC ribbon to clip.
5. Connect VBFC USB-C to **same Windows PC** (power + sniffer).
6. **Double-check** clip seating before power.

---

## Phase 6 — First Boot (Sniffer Mode)

```cmd
# Start capture BEFORE power-on
vbfc-host sniff --duration 60 --output E:\boot_trace_1.csv
```

1. Flip ATX PSU switch ON.
2. Press motherboard power button.
3. Observe:
   - POST code display (if port 80 card)
   - Serial console (UART header, 115200 8N1)
   - VBFC LED: should blink rapidly during SPI traffic
4. Let boot proceed to BIOS setup or OS loader.
5. Stop sniffer: `Ctrl+C`

**Quick trace check:**
```cmd
head -30 E:\boot_trace_1.csv
```
Look for normal SPI sequence: `0x03`/`0x0B` reads, addresses 0x000000 → 0x7FFFFF, no timeouts.

---

## Phase 7 — Functional Validation

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| **Cold boot** | ATX off 10 s → on | POST completes, no hangs |
| **BIOS entry** | Press `Del` / `F2` during POST | Setup utility enters |
| **Hidden menu** | Navigate to "Advanced" tab | **Visible & functional** |
| **Save & Exit** | F10 → Yes | Clean reboot |
| **USB boot** | Insert Windows/Linux USB, select boot | OS loader starts |
| **5× cold reboot** | ATX off/on × 5 | All 5 consistent |
| **5× warm reset** | Reset button × 5 | All 5 consistent |

**If any test fails:**
1. Power off immediately (ATX switch).
2. Check sniffer trace for anomalies.
3. Verify shadow map addresses match patched region.
4. Set VBFC bypass jumper (JP1) → board boots stock BIOS.

---

## Phase 8 — Stress & Edge Cases

```cmd
# 1. Rapid reboot loop (20×)
for /L %i in (1,1,20) do (
  shutdown /r /t 0 /f
  timeout 30
)

# 2. Warm reset spam
#    Press reset button 10× rapidly — all should POST

# 3. Idle SPI capture (OS runtime)
vbfc-host sniff --duration 60 --output E:\idle_trace.csv
#    Should show near-zero SPI traffic after boot
```

---

## Phase 9 — Green Checklist (Final Acceptance)

| # | Criterion | Pass/Fail | Notes |
|---|-----------|-----------|-------|
| 1 | Stock BIOS dump SHA-256 matches vendor | ☐ | |
| 2 | Modified image boots to BIOS setup | ☐ | |
| 3 | Target hidden menu **unlocked & functional** | ☐ | |
| 4 | 5 consecutive cold boots succeed | ☐ | |
| 5 | 5 consecutive warm resets succeed | ☐ | |
| 6 | No SPI errors in sniffer (timeouts, bad CRC) | ☐ | |
| 7 | Bypass jumper (JP1) works — stock BIOS boots | ☐ | |
| 8 | ME region (if any) untouched / clean | ☐ | N/A on G41 |
| 9 | VBFC LED shows expected activity pattern | ☐ | |
| 10 | Power-cycle survival (ATX off 10 s → on) × 3 | ☐ | |

**All 10 = GREEN → Experiment finalized.**

---

## Phase 10 — Cleanup & Documentation

1. **Archive on E::**
   ```
   E:\
   ├── g41_stock_bios_YYYYMMDD.bin          (original, 8 MB)
   ├── g41_modified_bios.bin                (patched)
   ├── g41_signed_bios.bin                  (signed, 8 MB + 256 B header)
   ├── boot_trace_1.csv                     (first boot)
   ├── idle_trace.csv                       (OS idle)
   ├── vbfc_info.txt                        (vbfc-host info output)
   └── photos/                              (wiring, clip, board)
   ```

2. **Restore stock BIOS** (if returning board to service):
   ```cmd
   flashrom -p ch341a_spi -w E:\g41_stock_bios_YYYYMMDD.bin -c "MX25L6406E"
   ```

3. **Remove VBFC**, store in anti-static bag.

4. **Record results** in test log:
   - Board serial: _______________
   - BIOS chip: _______________
   - Patch applied: _______________
   - Test date: _______________
   - Tester: _______________
   - Result: **PASS / FAIL**

---

## Appendix A — Quick Reference Card (Print & Tape to Bench)

```
VBFC TEST FLOW — G41
━━━━━━━━━━━━━━━━━━━
1. DUMP stock (CH341A) → verify SHA256
2. PATCH in UEFITool → save modified.bin
3. SIGN + FLASH → vbfc-host sign/flash
4. SHADOW MAP → original / hybrid / extension
5. CLIP ON → CS#/SCK/MOSI/MISO/VCC/GND
6. SNIFFER ON → vbfc-host sniff
7. POWER ON → observe POST
8. TEST → BIOS menu, 5× reboot, 3× power-cycle
9. PASS? → archive, restore stock if needed
10. FAIL? → bypass jumper, check trace, iterate
```

---

## Appendix B — Common G41 BIOS Chip Pinout (SOIC-8)

```
    △ Pin 1
   ┌─────────┐
CS# │1     8│ VCC
MISO│2     7│ HOLD#
WP# │3     6│ SCK
GND │4     5│ MOSI
   └─────────┘
```

- **CS#** = Chip Select (active low)
- **MISO** = Master In Slave Out (VBFC drives this line)
- **WP#** / **HOLD#** = pulled high by VBFC when active

---

## Appendix C — Troubleshooting Quick Guide

| Symptom | Likely Cause | Action |
|---------|--------------|--------|
| No POST, black screen | Clip misaligned / bad contact | Reseat clip, verify pin-1 |
| Garbled BIOS text | MISO contention (both driving) | Check WP#/HOLD# pulled high |
| Boot loops at POST code | Shadow map address mismatch | Verify region bounds in `vbfc-host info` |
| Sniffer shows timeouts | SCK too fast / signal integrity | Check ribbon cable, reduce SCK in firmware |
| Hidden menu still locked | Patched wrong region / offset | Re-analyze in UEFITool, check region base |
| VBFC LED solid red | Firmware fault / auth fail | Check `vbfc-host verify`, re-sign image |

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-08-10 | VBFC Team | Initial release for G41 test phase |

---

**End of Document** — Proceed phase by phase. Capture sniffer trace at each step for traceability.