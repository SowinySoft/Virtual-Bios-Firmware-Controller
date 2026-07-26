# Virtual BIOS Firmware Controller (VBFC) — Master Plan

**Document Version:** 1.0  
**Date:** 2026-07-20  
**Status:** Awaiting Confirmation  
**Classification:** Hardware/Firmware Co-Design Specification

---

## 1. Executive Summary

The Virtual BIOS Firmware Controller (VBFC) is a programmable interposer device that sits between a motherboard's BIOS SPI flash socket and the physical BIOS IC. It transparently passes through SPI signals under normal operation while providing the ability to intercept, redirect, modify, or extend BIOS firmware operations in real time.

The device serves as both a development tool (for BIOS modders, security researchers, and firmware engineers) and a functional extender (for machines with locked or limited BIOS chips).

**Key Capabilities:**
- Zero-latency transparent pass-through (default mode)
- Real-time SPI bus sniffing and logging
- Firmware image redirection and overlay
- Full flash emulation from external storage
- Hot-patching of specific memory regions
- Safe backup and restore of original firmware

---

## 2. System Architecture

### 2.1 High-Level Block Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                         MOTHERBOARD                                  │
│    ┌──────────────────────────────────────────────────────┐         │
│    │          CHIPSET SPI CONTROLLER                      │         │
│    └──────────────┬───────────────────────────────────────┘         │
│                   │ SPI Bus (CS/SCK/SI/SO)                         │
│    ┌──────────────┴───────────────────────────────────────┐         │
│    │          BIOS SPI FLASH SOCKET (SOIC-8)              │         │
│    └──────────────────────┬───────────────────────────────┘         │
└───────────────────────────┼─────────────────────────────────────────┘
                            │
              ┌─────────────┴──────────────┐
              │    VBFC DEVICE (Interposer) │
              │  ┌────────────────────────┐ │
              │  │   FPGA (iCE40-HX8K)     │ │
              │  │  ┌──────────────────┐   │ │
              │  │  │ SPI Switch Matrix │   │ │
              │  │  │ ┌───┐    ┌────┐  │   │ │
              │  │  │ │MB │◄──►│ORIG│  │   │ │
              │  │  │ └───┘    └────┘  │   │ │
              │  │  │      ▲            │   │ │
              │  │  │  ┌───┴───┐        │   │ │
              │  │  │  │ Arbiter│        │   │ │
              │  │  │  └───┬───┘        │   │ │
              │  │  │      │            │   │ │
              │  │  │  ┌───┴──────┐     │   │ │
              │  │  │  │ EXT FLASH│     │   │ │
              │  │  │  │W25Q128JV │     │   │ │
              │  │  │  └──────────┘     │   │ │
              │  │  └──────────────────┘   │ │
              │  │           │              │ │
              │  │  ┌────────┴───────┐      │ │
              │  │  │ CONFIG REGISTER │     │ │
              │  │  │   FILE (CRAM)   │     │ │
              │  │  └─────────────────┘     │ │
              │  └──────────────────────────┘ │
              │              │                │
              │  ┌───────────┴────────────┐   │
              │  │   RP2040 (System MCU)   │   │
              │  │  - USB-C Interface      │   │
              │  │  - FPGA Configuration   │   │
              │  │  - Flash File System    │   │
              │  │  - Protocol Handler     │   │
              │  └─────────────────────────┘   │
              └────────────────────────────────┘
```

### 2.2 Component Selection

| Component | Part Number | Role | Justification |
|-----------|-------------|------|---------------|
| FPGA | Lattice iCE40-HX8K-CB132 | Real-time SPI routing | Low cost, instant-on, sufficient LUTs, open-source toolchain (Yosys/NextPNR) |
| MCU | Raspberry Pi RP2040 | System controller | Dual-core M0+, native USB, PIO for custom protocols, abundant community support |
| External Flash | Winbond W25Q128JVSQ | Firmware storage | 128Mbit (16MB), 104MHz SPI, compatible voltage, low cost |
| Voltage Regulator | AMS1117-3.3 | Power management | Simple LDO, 3.3V output, sufficient for all components |
| USB Connector | TYPE-C-31-M-12 | Host interface | USB-C, through-hole for mechanical stability |
| Oscillator | SG-310SCF 12MHz | System clock | Stable reference for FPGA PLL |
| Configuration Flash | W25Q16JV | FPGA bitstream storage | 16Mbit, holds compressed iCE40 bitstream |

### 2.3 Power Architecture

```
Motherboard VCC (3.3V) ──┬──► VBFC VCC Rail
                          │     ├──► FPGA VCCIO (Bank 0,1,2)
                          │     ├──► RP2040 VREG_IN
                          │     ├──► External Flash VCC
                          │     └──► LDO Input (if level shifting needed)
                          │
                          └──► Original BIOS Chip VCC (switched via FPGA)

RP2040 USB VBUS (5V) ──────► RP2040 VREG_IN (internal LDO to 3.3V)
                              └──► Used when configuring device standalone
```

**Power Budget:**
| Component | Typical | Max |
|-----------|---------|-----|
| iCE40-HX8K | 5 mA | 50 mA |
| RP2040 | 20 mA | 90 mA |
| W25Q128JV | 5 mA | 25 mA |
| W25Q16JV | 2 mA | 10 mA |
| LEDs/Other | 5 mA | 15 mA |
| **Total** | **~37 mA** | **~190 mA** |

The motherboard 3.3V rail (typically rated 100-500mA) can easily supply this load.

---

## 3. Hardware Design

### 3.1 FPGA Pin Assignment (iCE40-HX8K, CB132 Package)

**SPI Interface to Motherboard (Bank 0 — 3.3V LVCMOS):**

| Signal | Direction | FPGA Pin | Description |
|--------|-----------|----------|-------------|
| `MB_CS_N` | Input | 37 | Chip Select from motherboard chipset |
| `MB_SCK` | Input | 38 | Serial Clock from motherboard |
| `MB_SI` | Input | 39 | Serial Input (data from MB to flash) |
| `MB_SO` | Output | 40 | Serial Output (data from flash to MB) |
| `MB_WP_N` | Input | 41 | Write Protect (optional, dual/quad IO) |
| `MB_HOLD_N` | Input | 42 | Hold (optional) |

**SPI Interface to Original BIOS Chip (Bank 0):**

| Signal | Direction | FPGA Pin | Description |
|--------|-----------|----------|-------------|
| `ORIG_CS_N` | Output | 45 | Chip Select to original BIOS |
| `ORIG_SCK` | Output | 46 | Serial Clock to original BIOS |
| `ORIG_SI` | Output | 47 | Serial Input to original BIOS |
| `ORIG_SO` | Input | 48 | Serial Output from original BIOS |
| `ORIG_WP_N` | Output | 49 | Write Protect to original |
| `ORIG_HOLD_N` | Output | 50 | Hold to original |

**SPI Interface to External Flash (Bank 1):**

| Signal | Direction | FPGA Pin | Description |
|--------|-----------|----------|-------------|
| `EXT_CS_N` | Output | 63 | Chip Select to external flash |
| `EXT_SCK` | Output | 64 | Serial Clock to external flash |
| `EXT_SI` | Output | 65 | Serial Input to external flash |
| `EXT_SO` | Input | 66 | Serial Output from external flash |
| `EXT_WP_N` | Output | 67 | Write Protect |
| `EXT_HOLD_N` | Output | 68 | Hold |

**RP2040 Interface (Bank 2 — 8-bit Config Bus):**

| Signal | Direction | FPGA Pin | Description |
|--------|-----------|----------|-------------|
| `CFG_D0` | Bidir | 78 | Config data bit 0 |
| `CFG_D1` | Bidir | 79 | Config data bit 1 |
| `CFG_D2` | Bidir | 80 | Config data bit 2 |
| `CFG_D3` | Bidir | 81 | Config data bit 3 |
| `CFG_D4` | Bidir | 82 | Config data bit 4 |
| `CFG_D5` | Bidir | 83 | Config data bit 5 |
| `CFG_D6` | Bidir | 84 | Config data bit 6 |
| `CFG_D7` | Bidir | 85 | Config data bit 7 |
| `CFG_RD_N` | Input | 86 | Config read strobe (active low) |
| `CFG_WR_N` | Input | 87 | Config write strobe (active low) |
| `CFG_CS_N` | Input | 88 | Config chip select |
| `CFG_IRQ` | Output | 89 | Interrupt to RP2040 |
| `CFG_CLK` | Input | 90 | Config bus clock (from RP2040) |

**Clock and Reset:**

| Signal | FPGA Pin | Description |
|--------|----------|-------------|
| `CLK_12M` | 21 | 12MHz oscillator input |
| `RESET_N` | 20 | Global reset (pulled up, RP2040 can assert) |

**JTAG (for debugging/programming):**

| Signal | FPGA Pin | Description |
|--------|----------|-------------|
| `TCK` | 94 | Test Clock |
| `TMS` | 95 | Test Mode Select |
| `TDI` | 96 | Test Data In |
| `TDO` | 97 | Test Data Out |

### 3.2 RP2040 Pin Assignment

| GPIO | Function | Description |
|------|----------|-------------|
| GP0 | UART0_TX | Debug UART (optional) |
| GP1 | UART0_RX | Debug UART (optional) |
| GP2-GP9 | CFG_D0-D7 | FPGA config bus data |
| GP10 | CFG_RD_N | FPGA config read strobe |
| GP11 | CFG_WR_N | FPGA config write strobe |
| GP12 | CFG_CS_N | FPGA config chip select |
| GP13 | CFG_IRQ | FPGA interrupt input |
| GP14 | CFG_CLK | FPGA config bus clock |
| GP15 | RESET_N_OUT | FPGA reset control |
| GP16-GP19 | SPI0 | FPGA bitstream loading (SCK, MOSI, MISO, CS) |
| GP20 | FPGA_CDONE | Configuration done input |
| GP21 | FPGA_CRESET | Configuration reset |
| GP22 | STATUS_LED | Main status LED |
| GP23 | ACTIVITY_LED | SPI activity LED |
| GP24 | USB_VBUS_DET | USB VBUS detect |
| GP25 | (Internal) | On-board LED (if available) |
| GP26-GP29 | ADC | Not used (reserved) |

**USB:**
- DM: GPIO 18 (or dedicated USB pins)
- DP: GPIO 19
- VBUS: 5V from USB-C

### 3.3 PCB Form Factor

**Dimensions:** 35mm x 45mm x 8mm (L x W x H)

**Layer Stackup (4-layer):**
```
Layer 1 (Top):    Signal routing, component pads
Layer 2 (Inner 1): Ground plane
Layer 3 (Inner 2): 3.3V power plane
Layer 4 (Bottom):  Signal routing, test points
```

**Physical Connectors:**
1. **Bottom Side — SOIC-8 Pads:** Direct solder or spring-clip interface to motherboard BIOS socket
2. **Top Side — SOIC-8 Socket:** Standard socket for original BIOS chip (piggyback)
3. **Edge — USB-C:** Configuration and data interface to host PC
4. **Edge — 4-pin UART Header:** Optional debug serial (1.27mm pitch)

**Signal Integrity Considerations:**
- All SPI traces matched to ~50Ω impedance
- Trace length matching for SCK/SI/SO to within 2mm
- Series termination resistors (22Ω) on all SPI lines
- Decoupling capacitors (100nF + 10µF) near each IC power pin
- FPGA placed centrally to minimize trace lengths

---

## 4. FPGA Logic Design

### 4.1 Module Hierarchy

```
top.v
├── clock_gen.v          (PLL: 12MHz → 96MHz system clock)
├── reset_sync.v         (Synchronized reset distribution)
├── spi_phy.v            (SPI physical layer — 3 instances)
│   ├── spi_phy_mb.v     (Motherboard side)
│   ├── spi_phy_orig.v   (Original chip side)
│   └── spi_phy_ext.v    (External flash side)
├── spi_decoder.v        (Command/address decoder)
├── address_map.v        (Address region mapping logic)
├── arbiter.v            (Access arbitration between sources)
├── overlay_engine.v     (Firmware overlay/patch engine)
├── passthrough.v        (Combinatorial pass-through mode)
├── config_reg.v         (Configuration register file)
│   └── cfg_bus.v        (RP2040 parallel interface)
├── fifo_async.v         (Async FIFO for sniffer data)
└── led_controller.v     (Status LED patterns)
```

### 4.2 Key Module Specifications

#### 4.2.1 spi_phy_mb.v — Motherboard SPI Physical Layer

**Function:** Captures SPI transactions from the motherboard chipset.

**Interface:**
```verilog
module spi_phy_mb (
    input  wire clk,           // System clock (96MHz)
    input  wire rst_n,         // Active low reset
    // Raw SPI pins
    input  wire spi_cs_n,      // Chip select from MB
    input  wire spi_sck,       // Serial clock from MB
    input  wire spi_si,        // Serial input from MB
    output wire spi_so,        // Serial output to MB
    // Decoded output
    output reg  [7:0] cmd_byte,// Decoded command
    output reg  [31:0] address,// Decoded address (for 24/32-bit)
    output reg  addr_valid,    // Address decoded
    output reg  [7:0] tx_data, // Data to send to MB
    input  wire [7:0] rx_data, // Data received from target
    input  wire data_valid,    // rx_data is valid
    output reg  transaction_active,
    // Sniffer tap
    output reg  [7:0] sniff_data,
    output reg  sniff_valid
);
```

**Implementation Notes:**
- Oversample SPI at 4x clock rate (96MHz / 4 = 24MHz max SPI) to handle up to 24MHz SPI bus
- Edge detection on SCK to sample data at correct phase
- Support SPI Mode 0 (CPOL=0, CPHA=0) as primary; Mode 3 as secondary
- First byte after CS assertion is always command
- Commands: 0x03 (READ), 0x02 (PP), 0x20 (SE), 0xD8 (BE), 0xC7/0x60 (CE), 0x05 (RDSR), 0x01 (WRSR), etc.

#### 4.2.2 arbiter.v — SPI Bus Arbiter

**Function:** Routes SPI transactions between motherboard, original chip, and external flash based on mode and address mapping.

**Operating Modes (from config register):**

| Mode | Value | Behavior |
|------|-------|----------|
| `MODE_PASSTHROUGH` | 0x00 | FPGA is transparent wire; MB ↔ ORIG directly |
| `MODE_SNIFF` | 0x01 | Passthrough + copy all traffic to RP2040 |
| `MODE_REDIRECT` | 0x02 | Address-mapped reads from EXT; writes to ORIG |
| `MODE_EMULATE` | 0x03 | All operations served from EXT; ORIG disabled |
| `MODE_HOTPATCH` | 0x04 | Specific addresses modified on-the-fly |

**Address Map Structure (32 regions):**
```verilog
reg [31:0] region_start [0:31];
reg [31:0] region_end   [0:31];
reg [1:0]  region_target[0:31];  // 00=ORIG, 01=EXT, 10=PATCH, 11=Reserved
reg        region_valid [0:31];
```

#### 4.2.3 passthrough.v — Zero-Latency Mode

**Function:** Combinatorial routing when in transparent mode.

**Critical Path Requirement:**
```verilog
// Pure combinational — no clocked elements
assign ORIG_CS_N = MB_CS_N;
assign ORIG_SCK  = MB_SCK;
assign ORIG_SI   = MB_SI;
assign MB_SO     = ORIG_SO;
assign ORIG_WP_N = MB_WP_N;
assign ORIG_HOLD_N = MB_HOLD_N;
```

**Propagation Delay Target:** < 5ns (FPGA pin-to-pin)

#### 4.2.4 overlay_engine.v — Hot-Patch Engine

**Function:** Modifies data bytes in-flight based on patch table.

**Patch Entry Structure:**
```verilog
typedef struct packed {
    bit [31:0] address;     // Target address
    bit [7:0]  orig_byte;   // Expected original value (for safety)
    bit [7:0]  patch_byte;  // Replacement value
    bit        enabled;     // Entry active
} patch_entry_t;

patch_entry_t patch_table [0:63];  // 64 patches max
```

**Operation:** When in HOTPATCH mode, every byte read from ORIG is checked against the patch table before being sent to MB.

### 4.3 Configuration Register Map (FPGA Internal)

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| 0x00 | `REG_MODE` | R/W | Operating mode (0-4) |
| 0x01 | `REG_STATUS` | R | Status flags (ready, error, activity) |
| 0x02-0x05 | `REG_SPI_CLK` | R/W | Max SPI clock divider |
| 0x10-0x11 | `REG_REGION_COUNT` | R/W | Number of active regions |
| 0x20-0x2F | `REG_REGION_N_START` | R/W | Region N start address (per region) |
| 0x40-0x4F | `REG_REGION_N_END` | R/W | Region N end address |
| 0x60-0x6F | `REG_REGION_N_CTRL` | R/W | Region N control (target, valid) |
| 0x80-0xBF | `REG_PATCH_TABLE` | R/W | Hot-patch entries (4 bytes each) |
| 0xF0 | `REG_FIFO_DATA` | R | Sniffer FIFO read data |
| 0xF1 | `REG_FIFO_STATUS` | R | FIFO flags (empty, full, count) |
| 0xFE | `REG_MAGIC` | R | Device ID ('V' 'B' = 0x5642) |
| 0xFF | `REG_VERSION` | R | Firmware version |

### 4.4 Timing Analysis

**FPGA Clock Domains:**
- `clk_sys`: 96MHz (from PLL) — main logic
- `clk_spi`: Derived from SCK (oversampled) — SPI capture

**Critical Timing Paths:**
1. MB_CS_N falling edge → ORIG_CS_N assertion: < 5ns (combinatorial)
2. ORIG_SO valid → MB_SO drive: < 5ns (combinatorial in pass-through)
3. SCK rising edge → SI sample: 10.4ns @ 96MHz (4x oversample)

---

## 5. RP2040 Firmware Design

### 5.1 Architecture

```
main.c
├── usb_stack/             (TinyUSB — CDC + MSC)
│   ├── cdc_interface.c    (Command line interface)
│   └── msc_interface.c    (Mass storage for firmware images)
├── fpga_config/           (FPGA bitstream management)
│   ├── ice40_prog.c       (iCE40 slave SPI configuration)
│   └── bitstream_store.c  (Compressed bitstream in flash)
├── spi_controller/        (FPGA register access)
│   ├── cfg_bus.c          (Parallel bus protocol)
│   └── reg_access.c       (High-level register API)
├── flash_manager/         (External flash management)
│   ├── flash_io.c         (W25Q128 driver)
│   ├── image_store.c      (Firmware image filesystem)
│   └── verification.c     (Checksum/hash verification)
├── command_parser/        (CLI implementation)
│   ├── lexer.c
│   ├── parser.c
│   └── commands.c         (Individual command handlers)
├── logger/                (SPI traffic logging)
│   ├── log_buffer.c       (Circular buffer)
│   └── log_format.c       (Binary/text format output)
├── file_system/           (LittleFS on RP2040 flash)
│   ├── lfs_config.c
│   └── file_ops.c
└── system/                (Core system)
    ├── init.c             (Power-on initialization)
    ├── gpio.c             (Pin configuration)
    ├── dma.c              (DMA channel management)
    └── watchdog.c         (System watchdog)
```

### 5.2 Boot Sequence

```
Power-On Reset
    │
    ▼
┌─────────────────┐
│ RP2040 Bootrom  │
│ (USB/Flash boot)│
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Load Firmware   │
│ from RP2040 QSPI│
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Initialize GPIO │
│ Configure clocks│
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Assert FPGA     │
│ RESET_N         │
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Load FPGA       │
│ Bitstream via   │
│ Slave SPI       │
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Wait for FPGA   │
│ CDONE           │
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Release MB      │
│ (enable pass-   │
│ through)        │
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Initialize USB  │
│ stack (CDC+MSC) │
└────────┬────────┘
    │
    ▼
┌─────────────────┐
│ Main Event Loop │
│ (USB, CLI,      │
│ FPGA comms)     │
└─────────────────┘
```

**Boot Time Target:** < 500ms from power-on to FPGA ready

### 5.3 Command-Line Interface (USB-CDC)

**Connection:** 115200 baud, 8N1 (USB-CDC, baud rate emulated)

**Command Reference:**

```
help                              Show this help text
status                            Display device status and mode
mode <mode>                       Set operating mode
  modes: transparent, sniff, redirect, emulate, hotpatch

map add <start> <end> <target> [name]
                                  Add address mapping region
  target: original, external, patch
map list                          List all active regions
map remove <index>                Remove mapping region
map clear                         Remove all regions

image list                        List stored firmware images
image load <filename>             Load image into external flash
image delete <filename>           Delete image
image verify <filename>           Verify image checksum

flash read <addr> <len> [file]    Read from original/external flash
flash write <addr> <file>         Write to flash (careful!)
flash erase <addr> <len>          Erase flash sectors
flash backup [filename]           Backup original BIOS to file
flash restore <filename>          Restore original BIOS from file

sniff start [file]                Start SPI sniffing
sniff stop                        Stop SPI sniffing
sniff status                      Show sniff buffer status

patch add <addr> <orig> <new>     Add hot-patch entry
patch list                        List patches
patch remove <index>              Remove patch
patch clear                       Clear all patches

config save [profile]             Save configuration to profile
config load <profile>             Load configuration profile
config default                    Reset to defaults

fpga reload                       Reload FPGA bitstream
fpga status                       Show FPGA status

diagnostics                       Run self-test
calibrate                         Calibrate timing (advanced)
version                           Show firmware versions
reboot                            Restart device
```

### 5.4 USB Mass Storage Interface

**Function:** Expose external flash firmware images as files on a virtual USB drive.

**Layout:**
```
/VBFC/                    (Root)
  /IMAGES/                (Firmware images)
    bios_stock.bin
    bios_modded.bin
    backup_20260720.bin
  /LOGS/                  (Sniffer logs)
    sniff_session_001.bin
  /CONFIG/                (Configuration profiles)
    default.cfg
    modding.cfg
  README.TXT              (Auto-generated device info)
```

**Behavior:**
- Images can be dragged/dropped from host PC
- Files auto-detected and indexed on unmount
- Read-only when device is in active mode

---

## 6. Software/Hardware Interface Protocol

### 6.1 FPGA ↔ RP2040 Parallel Bus Protocol

**Physical:** 8-bit data, RD_N/WR_N strobes, CS_N chip select, IRQ interrupt

**Read Cycle:**
```
        ┌──────────────────────────────────────┐
CFG_CS_N│                                      │
        └──┐                                ┌──┘
           │←── t_setup ──→│←── t_access ──→│
        ───┘                ┌────────────────┘
CFG_RD_N                    │
        ────────────────────┘
                            │
        ────────────────────┬────────────────
CFG_D[7:0]                  │ VALID DATA
        ────────────────────┴────────────────
                            │
                            ▼
                         RP2040 latches data
```

**Timing Parameters:**
- `t_setup`: 10ns (CS_N stable before RD_N)
- `t_access`: 30ns (RD_N low to valid data)
- `t_hold`: 10ns (data stable after RD_N high)
- Bus frequency: Up to 10MHz

### 6.2 FPGA Configuration (Slave SPI Mode)

**iCE40 Slave SPI Programming:**
```
1. Assert CRESET_N (low for 200ns minimum)
2. Wait for CDONE low
3. Release CRESET_N
4. Send 0x7E sync byte (at < 2.5MHz)
5. Send bitstream bytes (MSB first)
6. Send 49+ dummy clock cycles
7. CDONE goes high → success
```

**Bitstream Storage:**
- Compressed bitstream stored in RP2040 QSPI flash
- Decompression via fast-copy to FPGA
- Fallback bitstream for recovery

---

## 7. Development Phases

### Phase 1: Hardware Design (Weeks 1-3)

**Deliverables:**
- [ ] KiCad schematic (complete with ERC pass)
- [ ] KiCad PCB layout (DRC pass, Gerbers generated)
- [ ] BOM with supplier part numbers
- [ ] Preliminary signal integrity simulation

**Milestones:**
- Schematic review complete
- PCB ordered from JLCPCB (5 prototypes)
- Components ordered

### Phase 2: FPGA Core Development (Weeks 2-4)

**Deliverables:**
- [ ] Individual Verilog modules (simulated)
- [ ] Integrated top-level design
- [ ] Testbenches for all modules (Icarus Verilog / Verilator)
- [ ] Timing constraints file (.pcf)
- [ ] Bitstream generation

**Milestones:**
- Pass-through mode verified in simulation
- SPI decoder correctly identifies all standard commands
- Address mapping logic tested

### Phase 3: Firmware Development (Weeks 3-5)

**Deliverables:**
- [ ] RP2040 bring-up code (blink, UART)
- [ ] FPGA configuration routine
- [ ] Config bus driver
- [ ] External flash driver
- [ ] USB stack integration
- [ ] Command parser
- [ ] File system integration

**Milestones:**
- FPGA successfully configured from RP2040
- Config register read/write working
- USB enumeration successful
- CLI responsive

### Phase 4: Integration & Bench Testing (Weeks 5-6)

**Deliverables:**
- [ ] Hardware bring-up checklist
- [ ] Signal capture (logic analyzer)
- [ ] Timing verification
- [ ] Power measurement
- [ ] Protocol compliance test

**Milestones:**
- Device powers on without magic smoke
- Pass-through mode allows motherboard boot
- Sniffer captures readable SPI traffic
- External flash read/write verified

### Phase 5: Real System Validation (Weeks 6-8)

**Deliverables:**
- [ ] Test report on 3+ motherboard models
- [ ] Boot success/failure matrix
- [ ] Performance benchmarks
- [ ] Stress test results

**Milestones:**
- Successful boot on Intel platform (LGA1700)
- Successful boot on AMD platform (AM5)
- Sniffer logs decoded correctly
- Redirect mode loads alternative firmware

### Phase 6: Documentation & Release (Weeks 8-9)

**Deliverables:**
- [ ] Hardware assembly guide
- [ ] User manual
- [ ] API documentation
- [ ] Open-source release package
- [ ] Video demonstration

---

## 8. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| **Timing violation prevents boot** | Medium | Critical | Combinatorial pass-through default; scope verification; adjustable delay lines |
| **FPGA config failure bricks boot** | Low | High | RP2040 watchdog; external config flash; recovery mode |
| **Signal integrity causes corruption** | Medium | High | 4-layer PCB; matched impedance; series termination; shortest traces |
| **Motherboard damage** | Low | Critical | Series resistors; proper ESD; 3.3V-only design; no 5V exposure |
| **Incompatible with some boards** | Medium | Medium | Extensive testing; 1.8V support in v2; Mode 3 SPI support |
| **External flash too slow** | Low | Medium | Prefetch buffer in FPGA; cache recent reads; use 104MHz-rated flash |
| **USB firmware bugs** | Medium | Low | TinyUSB proven stack; fallback to UART; field updatable |
| **Power draw too high** | Low | Medium | Verified <200mA budget; low-power modes; LED dimming |

---

## 9. Bill of Materials (BOM)

| Ref | Qty | Value | Footprint | Manufacturer | Part Number | Est. Price |
|-----|-----|-------|-----------|--------------|-------------|------------|
| U1 | 1 | FPGA | BGA-121/CB132 | Lattice | iCE40-HX8K-CB132 | $6.00 |
| U2 | 1 | MCU | QFN-56 | Raspberry Pi | RP2040 | $1.00 |
| U3 | 1 | Ext Flash | SOIC-8 | Winbond | W25Q128JVSQ | $0.80 |
| U4 | 1 | Config Flash | SOIC-8 | Winbond | W25Q16JVSSIQ | $0.30 |
| U5 | 1 | LDO | SOT-223 | AMS | AMS1117-3.3 | $0.10 |
| Y1 | 1 | 12MHz Crystal | 3.2x2.5mm | Epson | SG-310SCF 12MHz | $0.50 |
| J1 | 1 | USB-C | Through-hole | Korean Hroparts | TYPE-C-31-M-12 | $0.30 |
| J2 | 1 | SOIC-8 Socket | 5.3mm pitch | 3M/Enplas | 208-7391 | $0.50 |
| R1-R12 | 12 | 22Ω | 0402 | Various | RC0402JR-0722RL | $0.05 |
| R13-R16 | 4 | 10kΩ | 0402 | Various | RC0402JR-0710KL | $0.05 |
| R17-R20 | 4 | 1kΩ | 0402 | Various | RC0402JR-071KL | $0.05 |
| C1-C8 | 8 | 100nF | 0402 | Murata | GRM155R71C104KA88D | $0.10 |
| C9-C10 | 2 | 10µF | 0603 | Murata | GRM188R61E106KA73D | $0.20 |
| C11-C12 | 2 | 22pF | 0402 | Murata | GRM1555C1H220JA01D | $0.05 |
| LED1 | 1 | Green | 0603 | Wurth | 150060VS75000 | $0.10 |
| LED2 | 1 | Yellow | 0603 | Wurth | 150060YS75000 | $0.10 |
| PCB | 1 | 4-layer | 35x45mm | JLCPCB | Custom | $2.00 |
| **Total** | | | | | | **~$12.50/unit @ qty 10** |

---

## 10. Test Plan

### 10.1 Hardware Tests

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Power-up | Measure 3.3V rail | 3.25V ≤ VCC ≤ 3.35V |
| FPGA config | Scope CDONE pin | CDONE high within 100ms |
| USB enumeration | `lsusb` on Linux | Device appears as CDC+MSC |
| Clock stability | Scope oscillator | 12MHz ± 50ppm |
| Current draw | Multimeter in series | < 200mA @ 3.3V |

### 10.2 Functional Tests

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Pass-through | Boot motherboard | Successful POST and OS boot |
| Sniffer mode | Capture boot SPI | Recognizable READ commands visible |
| Redirect mode | Map region to EXT | MB reads modified data from EXT |
| Emulate mode | Full EXT boot | Successful boot from external flash image |
| Hot-patch | Patch single byte | Byte at address shows patched value |
| Backup/Restore | Read original to file | File matches known BIOS dump |

### 10.3 Compatibility Matrix (Target)

| Platform | Chipset | BIOS Type | SPI Freq | Test Status |
|----------|---------|-----------|----------|-------------|
| Intel LGA1700 | Z790 | UEFI | 33MHz | Planned |
| Intel LGA1200 | Z590 | UEFI | 33MHz | Planned |
| AMD AM5 | X670E | UEFI | 33MHz | Planned |
| AMD AM4 | B550 | UEFI/Legacy | 20MHz | Planned |
| Legacy System | H81 | Legacy | 20MHz | Planned |

---

## 11. File Structure (Repository)

```
vbfc-controller/
├── hardware/
│   ├── kicad/
│   │   ├── vbfc-controller.kicad_pro
│   │   ├── vbfc-controller.kicad_sch
│   │   ├── vbfc-controller.kicad_pcb
│   │   ├── fpga-lib.kicad_sym
│   │   ├── rp2040-lib.kicad_sym
│   │   └── production/
│   │       ├── gerbers/
│   │       ├── drill/
│   │       └── bom/
│   └── docs/
│       ├── schematic-notes.md
│       └── pcb-layout-notes.md
├── fpga/
│   ├── rtl/
│   │   ├── top.v
│   │   ├── clock_gen.v
│   │   ├── reset_sync.v
│   │   ├── spi_phy_mb.v
│   │   ├── spi_phy_orig.v
│   │   ├── spi_phy_ext.v
│   │   ├── spi_decoder.v
│   │   ├── address_map.v
│   │   ├── arbiter.v
│   │   ├── overlay_engine.v
│   │   ├── passthrough.v
│   │   ├── config_reg.v
│   │   ├── cfg_bus.v
│   │   ├── fifo_async.v
│   │   └── led_controller.v
│   ├── constraints/
│   │   └── vbfc-controller.pcf
│   ├── sim/
│   │   ├── tb_top.v
│   │   ├── tb_spi_decoder.v
│   │   ├── tb_arbiter.v
│   │   └── Makefile
│   └── build/
│       ├── Makefile
│       └── yosys-script.ys
├── firmware/
│   ├── src/
│   │   ├── main.c
│   │   ├── usb_stack/
│   │   ├── fpga_config/
│   │   ├── spi_controller/
│   │   ├── flash_manager/
│   │   ├── command_parser/
│   │   ├── logger/
│   │   ├── file_system/
│   │   └── system/
│   ├── include/
│   │   ├── vbfc.h
│   │   ├── config.h
│   │   └── commands.h
│   ├── pico-sdk/
│   │   └── (submodule)
│   ├── CMakeLists.txt
│   └── build/
├── software/
│   ├── cli-client/
│   │   ├── vbfc-cli.py
│   │   └── requirements.txt
│   └── gui-client/
│       ├── vbfc-gui.py
│       └── ui/
├── docs/
│   ├── vbfc-controller-plan.md (this file)
│   ├── hardware-guide.md
│   ├── user-manual.md
│   ├── api-reference.md
│   └── protocol-spec.md
├── tests/
│   ├── hardware-tests/
│   ├── fpga-tests/
│   └── firmware-tests/
├── tools/
│   ├── bitstream-compress.py
│   └── image-convert.py
├── LICENSE
└── README.md
```

---

## 12. Open Questions & Decisions Required

1. **FPGA Package:** CB132 (easier soldering) vs CM81 (BGA, smaller)?
2. **1.8V Support:** Add level shifter for modern 1.8V SPI buses in v1 or v2?
3. **Wireless:** Add ESP32 or nRF52 for wireless firmware updates?
4. **Display:** Small OLED for status without USB connection?
5. **Case:** 3D-printed enclosure design?
6. **Open Source License:** CERN-OHL (hardware) + MIT (firmware)?

---

## 13. Approval Checklist

- [ ] Architecture approved
- [ ] Component selection approved
- [ ] Pin assignments approved
- [ ] Budget approved (~$12.50/unit + PCB)
- [ ] Development timeline approved
- [ ] Risk assessment reviewed
- [ ] Ready to proceed to Phase 1

---

**Next Steps Upon Approval:**
1. Create KiCad project and begin schematic capture
2. Set up FPGA simulation environment (Yosys/NextPNR/Verilator)
3. Set up RP2040 firmware project (Pico SDK)
4. Order prototype components

**End of Plan Document**
