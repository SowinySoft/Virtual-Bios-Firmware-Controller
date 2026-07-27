# Real Hardware Validation Checklist for VBFC

Use this checklist on the old G41 PC with a USB flash drive and a BIOS recovery path.

## 0. Preparation

- [ ] Backup the original BIOS image to the USB flash drive.
- [ ] Store a copy of the backup as `bios_backup.bin`.
- [ ] Prepare a small test payload, such as `test_payload.bin`.
- [ ] Make sure you have a recovery path:
  - [ ] BIOS Flashback / CrashFree BIOS / dual-bios, or
  - [ ] a CH341A-style SPI programmer.
- [ ] Keep the bypass jumper open for the first tests.
- [ ] Power off the PC and disconnect the PSU.

## 1. Build and flash controller firmware

- [ ] Build the RP2040 firmware using the repo instructions.
- [ ] Flash the controller firmware to the RP2040 board.
- [ ] Connect the controller over USB.
- [ ] Confirm the controller enumerates and responds.

## 2. Verify pass-through boot

- [ ] Install the controller on the target board.
- [ ] Power on the PC with bypass open.
- [ ] Observe whether the system boots normally.
- [ ] If boot fails, immediately close bypass and reboot.
- [ ] Record result: boot success / boot failure.

## 3. Validate basic host communication

- [ ] Run:
  ```bash
  vbfc-cli scan
  ```
- [ ] Confirm the device is detected.
- [ ] Record the reported mode and firmware version.

## 4. Upload a test payload

- [ ] Upload the test payload to the controller:
  ```bash
  vbfc-cli upload --file test_payload.bin --offset 0
  ```
- [ ] Confirm the upload succeeds.
- [ ] Record the result.

## 5. Validate shadow-map configuration

- [ ] Configure a small shadow-map entry:
  ```bash
  vbfc-cli map add --start 0xFF0000 --size 64K --source ext --ext-offset 0
  ```
- [ ] Switch to shadow mode:
  ```bash
  vbfc-cli mode shadow
  ```
- [ ] Reboot the PC.
- [ ] Record whether the machine still boots.

## 6. Validate recovery path

- [ ] Close the bypass jumper.
- [ ] Reboot the PC.
- [ ] Confirm the machine returns to normal operation.
- [ ] Record result: recovered / not recovered.

## 7. Evidence to save

- [ ] BIOS backup file from the USB flash drive.
- [ ] Test payload file.
- [ ] Output of `vbfc-cli scan`.
- [ ] Upload and map command logs.
- [ ] Boot success/failure notes.
- [ ] Recovery result.

## 8. Success criteria

You can move forward with confidence only if all of the following are true:

- [ ] The original BIOS was successfully backed up.
- [ ] The controller enumerates over USB.
- [ ] Pass-through boots the PC.
- [ ] Shadow mode can be enabled without permanent failure.
- [ ] Recovery via bypass returns the system to normal operation.
