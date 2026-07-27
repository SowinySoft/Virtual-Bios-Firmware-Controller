# VBFC Breadboard Test Rig

Desk prototype that simulates the motherboard + VBFC + BIOS flash stack before
building the SOIC-8 interposer PCB.

## Block Diagram

```
┌─────────────────────┐          shared SPI bus           ┌──────────────────────┐
│  Pico A             │  SCK ──────────────────────────── │  W25Q128 "Original"  │
│  (Test Master)      │  MOSI ─────────────────────────── │  (BIOS chip role)    │
│  simulates PCH      │  CS ────────┐                     │                      │
└─────────────────────┘             │                     │  SO (MISO) ──┐       │
         │                          │                     └──────────────│───────┘
         │ MISO ◄───────────────────┼──────────────────────────────────────┘
         │                          │                              │
         │              ┌───────────▼──────────────────────────────▼──────────┐
         │              │  Pico B — VBFC Controller (vbfc_controller.uf2)      │
         │              │                                                      │
         └──────────────│  GP7 SCK snoop    GP3 ORIG_CS (mirror)              │
                        │  GP8 MOSI snoop   GP6 ORIG_SLEEP                     │
                        │  GP4 MB_CS sense   GP9 CTRL_MISO ──► mux             │
                        │  GP5 MISO_OE       GP2 ORIG_MISO ◄── orig chip       │
                        │                                                      │
                        │  SPI1 ──► W25Q128 "Extension" (shadow store)        │
                        └──────────────────────────────────────────────────────┘
```

## Parts List

| Qty | Part | Role |
|-----|------|------|
| 2 | Raspberry Pi Pico | Master (A) + VBFC (B) |
| 2 | W25Q128JV (or W25Q64) | Original + extension flash |
| 1 | 74LVC1G125 | MISO tri-state buffer |
| 1 | SOIC-8 breakout | Original flash socket |
| 1 | SOIC-8 breakout | Extension flash socket |
| — | Jumpers, breadboard, 3.3 V supply | |

Optional: logic analyzer on SCK, MOSI, CS, MISO.

## Wiring Table

### Shared SPI Bus (Master A ↔ Original Flash ↔ VBFC snoop)

| Net | Master Pico A | VBFC Pico B | Original W25Q128 |
|-----|---------------|-------------|------------------|
| SCK | GP18 | GP7 (snoop) + GP0 (SPI0 SCK) | Pin 6 |
| MOSI | GP19 | GP8 (snoop) + GP1 (SPI0 MOSI) | Pin 5 |
| CS# | GP17 | GP4 (sense) → GP3 (orig CS out) | Pin 1 |
| MISO | GP16 | via mux | Pin 2 |

GP0/GP1 on Pico B tie to the same bus as GP7/GP8 for idle SPI master access.

### MISO Mux (74LVC1G125)

| 125 Pin | Connection |
|---------|------------|
| 1A (input) | Original flash SO (pin 2) |
| 1Y (output) | Master A GP16 (MISO) |
| 2A (input) | VBFC GP9 (CTRL_MISO) |
| 2Y | Tie to 1Y (or second 125) |
| OE# (pin 1) | VBFC GP5 (MISO_OE, active low) |

Breadboard shortcut: use two 125 buffers OR a DPDT mux jumper for initial bring-up.

### ORIG_SLEEP (CS override)

VBFC GP6 → Original flash CS# pin through 470 Ω (OR with GP3 CS mirror).

When GP6 high, original chip is deselected/sleeping even if bus CS active.

### Extension Flash (VBFC SPI1 only)

| VBFC GP | W25Q128 Ext |
|---------|-------------|
| GP10 | SCK (pin 6) |
| GP11 | MOSI (pin 5) |
| GP12 | MISO (pin 2) |
| GP13 | CS# (pin 1) |

### Status / Control

| VBFC GP | Function |
|---------|----------|
| GP16 | Green LED + 330 Ω |
| GP17 | Red LED + 330 Ω |
| GP18 | Bypass jumper → GND (pull-up enabled) |
| GP19 | Reset button → GND |

## Flash Procedure

1. Build and flash **test master** to Pico A:
   ```bash
   cd firmware/test_master/build && cmake .. && make
   # Copy vbfc_test_master.uf2 to Pico A (BOOTSEL)
   ```

2. Build and flash **VBFC controller** to Pico B:
   ```bash
   cd firmware/rp2040/build && cmake .. && make
   # Copy vbfc_controller.uf2 to Pico B (BOOTSEL)
   ```

3. Wire per table above, power both Picos via USB.

4. Open two serial terminals (or use test master auto-run):
   - Pico A: test results
   - Pico B: VBFC status (`vbfc-cli scan` works on Pico B USB)

## Recommended Test Sequence

1. **T01 Pass-through** — VBFC in pass-through mode, Master reads addr `0x000000`, compare to direct flash.
2. **T02 RDID** — Send `0x9F`, verify JEDEC ID matches original chip (e.g. `EF 40 18` for W25Q128).
3. **T03 Shadow** — Write pattern `0xAA` to ext flash offset 0, enable shadow mode via `vbfc-cli`, read `0xFF0000`.
4. **T04 Bypass** — Close bypass jumper (GP18→GND), verify reads still work (orig only).

## Validation Checklist

The following checklist is the intended end-to-end flow for the breadboard rig:

1. **Pre-flight**
   - Build and flash the test master to Pico A and the VBFC firmware to Pico B.
   - Confirm both devices enumerate over USB and that the test master prints a JEDEC ID.
   - Connect the shared SPI bus and the MISO mux as documented above.

2. **T01 pass-through**
   - Start with VBFC in pass-through mode.
   - Read a known address from the original flash using Pico A.
   - Compare the byte with the direct flash read from the same address.
   - Expected result: the bytes match exactly.

3. **T02 RDID**
   - Issue the `0x9F` RDID command from the test master.
   - Confirm the returned three-byte JEDEC ID matches the attached flash part.
   - Expected result: `EF 40 18` for a W25Q128-class part.

4. **T03 shadow-map round-trip**
   - Upload a small image to the VBFC extension flash store.
   - Add a shadow-map entry that redirects the target BIOS address range to the extension image.
   - Switch VBFC to shadow mode and read the redirected range.
   - Expected result: the host sees the uploaded image bytes rather than the original flash bytes.

5. **T04 bypass recovery**
   - Close the bypass jumper so the controller is forced into pass-through.
   - Re-run the read test from Pico A.
   - Expected result: reads return to the original flash path.

6. **Record evidence**
   - Save the JEDEC output, the pass-through comparison, and the shadow-map readback for each run.
   - This evidence is the minimum bar before moving to the SOIC-8 interposer PCB.

## SPI Speed

Start at **1 MHz** (test master default). Increase to 10 MHz after T01–T03 pass.
Target production: 20–50 MHz (may require PIO upgrade on VBFC).

## Pin Map Summary

| Signal | Master A | VBFC B |
|--------|----------|--------|
| SCK | GP18 | GP7 / GP0 |
| MOSI | GP19 | GP8 / GP1 |
| CS# | GP17 | GP4 / GP3 |
| MISO | GP16 | mux out |
| CTRL_MISO | — | GP9 |
| MISO_OE | — | GP5 |
| ORIG_SLEEP | — | GP6 |
