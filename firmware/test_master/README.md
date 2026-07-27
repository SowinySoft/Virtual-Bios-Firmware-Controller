# VBFC Test Master

Minimal Pico firmware for the breadboard validation rig described in [../../hardware/breadboard/README.md](../../hardware/breadboard/README.md).

This target acts as the "master A" side of the test rig. It drives the shared SPI bus and prints the JEDEC ID from the attached flash device so the VBFC controller can be exercised in pass-through and shadow modes.

## Build

From a machine with the Raspberry Pi Pico SDK installed:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware/test_master
mkdir build && cd build
cmake -G Ninja ..
ninja
```

The resulting UF2 can be flashed to a Pico that will act as the test master.
