# SPI Capture Test Vectors

Place logic analyzer captures here for validation.

## Expected Test Cases

| ID | Description | Pass Criteria |
|----|-------------|---------------|
| T01 | Power-on pass-through read | MISO matches original chip |
| T02 | RDID command | Returns original JEDEC ID |
| T03 | Shadow region read | Data from extension flash |
| T04 | Non-shadow read | Pass-through to original |
| T05 | Bypass jumper closed | Controller fully bypassed |
| T06 | Factory reset | Mode = pass-through |

## Capture Format

- Saleae Logic 2 `.sal` sessions, or
- CSV export with columns: `time_ns, cs, clk, mosi, miso`

## Running

```bash
# After connecting logic analyzer to SPI bus:
# 1. Boot target with bypass OPEN
# 2. Capture 1 ms window around first CS# assertion
# 3. Compare MISO against golden reference
```
