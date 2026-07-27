# VBFC — Virtual BIOS Firmware Controller

Programmable SPI interposer that extends an existing motherboard BIOS chip
with a 64 KB shadow region, while appearing as the original flash device.

## Project Layout

```
vbfc-controller/
├── docs/                  Architecture, shadow map spec, install guide
├── hardware/
│   ├── breadboard/        Breadboard validation rig notes
│   └── interposer-soic8/  SOIC-8 interposer PCB design + BOM
├── firmware/
│   ├── rp2040/            RP2040 MVP firmware (Pico SDK)
│   └── test_master/       Pico-based breadboard test master
├── host/
│   └── vbfc-cli/          Python host CLI (USB CDC)
└── tests/
    └── spi-capture/       Logic analyzer test vectors
```

## Quick Start

### 1. Build firmware

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware/rp2040
mkdir build && cd build
cmake ..
make -j4
# Flash vbfc_controller.uf2 via BOOTSEL
```

### 2. Build the breadboard test master (optional)

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware/test_master
mkdir build && cd build
cmake -G Ninja ..
ninja
```

### 3. Install host CLI

```bash
cd host/vbfc-cli
pip install -e .
vbfc-cli scan
```

### 4. Install hardware

See [docs/install-guide.md](docs/install-guide.md).

## Defaults (MVP)

| Setting | Value |
|---------|-------|
| MCU | RP2040 |
| Mode | Pass-through + 64 KB shadow at `0xFF0000` |
| Form factor | SOIC-8 interposer |
| Host interface | USB CDC |
| Recovery | Bypass jumper + factory reset |

## Status

| Phase | Status |
|-------|--------|
| P0 Spec | Done |
| P1 Hardware design | Done (schematic reference + BOM) |
| P2 MVP firmware | Implemented, with hardware timing validation still pending |
| P3 Host tools | Done (upload / dump / patch / sniff workflow) |
| P4 Validation | Breadboard scaffold added; real hardware validation pending |
| P5 FPGA tier | Planned |

## Documentation

- [Architecture](docs/architecture.md)
- [Shadow Map Spec](docs/shadow-map-spec.md)
- [Install Guide](docs/install-guide.md)

## Safety

Always keep the original BIOS chip installed. Use the bypass jumper for
recovery. Shadowing boot-critical regions may break Secure Boot.

## License

MIT
