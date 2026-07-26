# vbfc-cli

Host-side command-line tool for the Virtual BIOS Firmware Controller.

## Install

```bash
pip install -e .
```

## Usage

```bash
# Auto-detect USB serial port
vbfc-cli scan

# Specify port
vbfc-cli --port COM5 scan

# Upload payload to extension flash
vbfc-cli upload --file payload.bin --offset 0

# Configure shadow map
vbfc-cli map add --start 0xFF0000 --size 64K --source ext --ext-offset 0

# Switch mode
vbfc-cli mode shadow
vbfc-cli mode pass-through

# Factory reset
vbfc-cli factory-reset
```

## Protocol

Text line protocol over USB CDC serial. See `firmware/rp2040/README.md`.
