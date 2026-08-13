# VBFC FPGA Arbiter — Integration with RP2040 / Host Toolchain Bridge (Step 4)

## Purpose
Define how an FPGA‑based SPI arbiter would integrate with the existing VBFC
software stack (`vbfc-host`, `vbfc-host-cli`, and the RP2040 firmware) so that
the FPGA takes over the arbitration, QSPI/OPI decoding, and patch‑lookup functions
currently performed by the RP2040 PIO state machines.

## 4.1 High‑Level Integration Diagram

```
+---------------------+       SPI          +---------------------+
|  Host (vbfc-host)   | <------------->  |  FPGA SPI Arbiter   |
|  (Linux / Windows)  |  CS / SCK / MOSI|  (Lattice / Xilinx) |
+----------+----------+       MISO/MISO    +----------+----------+
           |                                   |
           |   Wishbone / AHB-Lite Bridge      |  External QSPI Flash
           |                                   (e.g., W25Q128)
           v                                   v
+---------------------+       QSPI         +---------------------+
|  RP2040 Firmware    | <------------->  |  Shadow Map & Patches|
|  (PIO state machines)|             (optional)     |  (stored in FPGA BRAM)|
+---------------------+                    +---------------------+
```

## 4.2 Wishbone / AHB-Lite Bridge Specification

| Signal              | Direction | Width | Description                                                                 |
|---------------------|-----------|-------|-----------------------------------------------------------------------------|
| `wb_cyc`            | out       | 1     | Cycle valid — indicates a bus cycle                                           |
| `wb_stb`            | out       | 1     | Strobe — indicates the beginning of an address phase                          |
| `wb_adr`            | out       | 32    | Address bus (byte‑aligned)                                                    |
| `wb_dat_w`          | out       | 32    | Write data bus                                                                |
| `wb_dat_r`          | in        | 32    | Read data bus                                                                 |
| `wb_we`             | out       | 1     | Write enable — 1 = write, 0 = read                                            |
| `wb_ack`            | in        | 1     | Transfer acknowledge — driven by the slave (FPGA arbiter)                     |
| `wb_err`            | in        | 1     | Transfer error — driven high if the address is outside the supported window     |
| `irq_o`             | out       | 1     | Optional interrupt to the host when a shadow‑map update or error condition occurs|

### Bridge Operational Modes

1. **Command Mode** (`wb_adr[31:24]` = 8'h01)
   - Write a 4‑byte patch entry to the FPGA’s internal patch‑table BRAM.
   - `wb_dat_w` contains the 32‑byte patch data; `wb_adr[11:2]` selects the index.

2. **Shadow‑Map Read Mode** (`wb_adr[31:24]` = 8'h02)
   - Read a shadow‑map entry (offset + size + field description).
   - The FPGA returns the 64‑byte field description (offset, size, reserved, etc.).

3. **QSPI/OPI Configuration Mode** (`wb_adr[31:24]` = 8'h03)
   - Configure the number of data lines (1‑line SPI, 4‑line QSPI, 8‑line OPI).
   - Program the external flash’s QSPI enable bit via the host’s I²C/GPIO mux controller.

4. **Status/Interrupt Mode**
   - The FPGA asserts `irq_o` when:
     - A shadow‑map region is accessed that requires a patch.
     - The QSPI/OPI configuration changes (e.g., mode switch from SPI to QSPI).
     - An address‑overflow condition is detected (attempted access to unsupported flash region).

## 4.3 RP2040 Firmware Changes (Minimal)

The RP2040 firmware currently implements the SPI arbiter in PIO assembly (`vbfc/software/pio/arbiter.pio`). When the FPGA takes over, the following RP2040 changes are required:

| Change | Description |
|--------|-------------|
| `pio_arbiter_disable()` | Disable the PIO SM0/SM1 state machines that drive MOSI/MISO. |
| `spi_cs_gpio_set_function(mux_sel)` | Re‑configure the CS‑GPIO to be controlled by the FPGA’s Wishbone interface (or an external GPIO expander such as `XGPIO` or `MAX7300`). |
| `pio_rx_fifo_flush()` | Flush the RX FIFO so no stale bytes remain after the handover. |
| `uart_console_baud_preserve()` | Preserve the existing UART baud‑rate settings (the FPGA does not handle USB/console). |
| `main()` minimal init | Initialise only the GPIOs needed for the FPGA‑CS mux; skip the full PIO arbiter initialisation. |

### Minimal RP2040 C Snippet

```c
// RP2040 main.c — after FPGA arbiter hand‑over
int main() {
    // 1. Initialise standard board (clocks, UART, etc.)
    stdio_init_all();

    // 2. Disable the PIO SPI arbiter (release GPIOs)
    pio_arbiter_disable(PIO0, 0); // SM0, SM1

    // 3. Re‑map SPI CS GPIO to input (floating, driven by FPGA)
    gpio_set_function(SPI_CS_GPIO, GPIO_FUNC_SPI); // or GPIO_FUNC_IN

    // 4. Optional: enable external interrupt on FPGA‑irq line
    //    gpio_set_irq_enabled_with_filcher(FPGA_IRQ_GPIO, GPIO_IRQ_EDGE_FALL, true);

    // 5. Enter main loop — now the host (vbfc-host) talks to the FPGA
    //    over the Wishbone/AHB‑Lite bridge, not the RP2040 PIO.
    while (1) {
        // host command processing, etc.
        tight_loop_contents();
    }
}
```

## 4.4 Host‑Side (`vbfc-host`) Changes

The host‑side CLI (`vbfc-host`) currently sends PIO‑encoded SPI commands.
After the FPGA arbiter is in place, the CLI must be updated to use the
Wishbone/AHB‑Lite register interface instead of PIO FIFO pushes.

### New CLI Commands (proposed)

| Command | Syntax | Description |
|---------|--------|-------------|
| `vbfc fpga patch <addr> <val>` | `vbfc fpga patch 0x1000 0xAABBCCDD` | Write a 4‑byte patch to the FPGA’s patch‑table at `addr[11:2]`. |
| `vbfc fpga read <addr>` | `vbfc fpga read 0x2000` | Read a 64‑byte shadow‑map entry at the given offset. |
| `vbfc fpga qspi <lines>` | `vbfc fpga qspi 8` | Configure the external flash to 1‑line, 4‑line, or 8‑line SPI. |
| `vbfc fpga status` | `vbfc fpga status` | Print FPGA status: current region (ORIG/EXT/HYBRID), overflow flag, IRQ status. |

### Integration Flow (high‑level)

1. **Host boot**: `vbfc-host` enumerates the Wishbone bridge, identifies the
   FPGA arbiter’s base address (via PCIe, USB, or LPC depending on platform).
2. **Initialisation**: Send `vbfc fpga status` to read the current region
   and verify the FPGA is responding.
3. **Patch deployment**: Use `vbfc fpga patch` to load custom patch entries
   (e.g., bypass a locked firmware region, insert a debug trigger).
4. **Flash operation**: Use `vbfc fpga read` / standard `vbfc-read` to read
   from the flash; the FPGA’s arbiter automatically decodes the address
   region and either passes through original data, drives patched data,
   or selects the extension‑flash path.
5. **Shutdown / re‑handback**: If the user wishes to return to the RP2040
   PIO arbiter, send a “hand‑back” command that re‑enables the PIO state
   machines and disables the FPGA’s Wishbone interface.

## 4.5 FPGA Resource Estimate (ECP5 / Artix‑7)

| Resource         | Approx. Usage | Notes |
|------------------|---------------|-------|
| LUTs             | 1.2k – 2.5k   | FSM, counters, BRAM interface |
| Flip‑Flops       | 1.0k – 2.0k   | Pipeline registers, shift‑registers for data |
| BRAM (36‑kb)     | 1 – 2 blocks   | Patch‑table (32 × 32‑bit = 128 b, negligible; shadow‑map entry = 64 b) |
| DSP slices       | 0             | Not required for pure‑logic arbiter |
| IO pins          | 8 – 12        | SPI signals (CS, SCK, MOSI, MISO) + 2‑3 control (irq, config) |
| Max SPI frequency|  –            | 20 MHz (software‑defined); can be increased with a dedicated PLL |

## 4.5 Roadmap Item 5 (Formal Property Checking)

- Write SystemVerilog Assertions (SVA) as demonstrated in `scripts/property_checker.sv`.
- Run formal verification with JasperGold or Cadence Incisive on the FSM.
- Key properties:
  - **No two concurrent masters** drive MOSI without CS arbitration.
  - **Region decoding is mutually exclusive**: only one of ORIGINAL/EXTENSION/HYBRID is asserted per transaction.
  - **Address overflow never propagates** to the data path (if `addr_ovf = 1`, the FPGA drives `MISO` to `high‑Z` or a known safe value).
  - **patch_en pulse width** is exactly one SCK cycle in `DATA_PHASE`.

### Next Step (Step 5)
Proceed to **Formal property checking** — write the complete FSM model in
SystemVerilog, synthesize the SVA properties, and run a formal analysis
run (JasperGold/Mentor). This will validate the arbiter’s correctness before
any FPGA synthesis or FPGA‑in‑the‑loop testing.

---

*This completes **Step 4** of the 5‑step sequence for the `feature/vbfc-fpga-arbiter` case study.*