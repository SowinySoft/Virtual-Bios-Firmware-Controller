#ifndef VBFC_SNIFFER_H
#define VBFC_SNIFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * SPI transaction sniffer.
 *
 * The arbiter already decodes every motherboard SPI transaction. When sniffing
 * is enabled, each completed transaction is compressed into a compact event
 * record and pushed into a ring buffer in SRAM. The host drains it via
 * "DUMP SNIFF", which streams the buffer as base64 CHUNK lines.
 *
 * Event record (8 bytes), little-endian:
 *   u8   cmd           SPI command byte (0x03/0x0B/0x02/0x20/0x9F/...)
 *   u8   flags         bit0: was a read, bit1: was a write, bit2: addr valid
 *   u32  addr          24-bit address (0 if no address phase)
 *   u16  count         #data bytes in the transaction (capped)
 *
 * Total 8 bytes keeps the ring dense; a 4 KB buffer holds ~512 events.
 */

#define SNIFF_RING_BYTES 4096
#define SNIFF_EVENT_SIZE 8
#define SNIFF_MAX_EVENTS (SNIFF_RING_BYTES / SNIFF_EVENT_SIZE)

void sniffer_init(void);

/* Enable/disable capture. Pass-through still routes traffic safely; capture
 * only reads decoded fields the arbiter already has. */
void sniffer_start(void);
void sniffer_stop(void);
bool sniffer_is_active(void);

/* Ring metrics for STATUS replies. */
uint32_t sniffer_count(void);      /* events captured since last drain */
uint32_t sniffer_capacity(void);   /* SNIFF_MAX_EVENTS */

/* Record one decoded transaction. Called from the arbiter (cs_rising) when
 * sniff is active. `data_bytes` is the number of data-phase bytes the host
 * drove/expected (capped to u16). */
void sniffer_record(uint8_t cmd, uint8_t flags, uint32_t addr, uint16_t data_bytes);

/* Clear the ring. */
void sniffer_clear(void);

/* Stream the captured events to the host as DUMP CHUNK lines (base64),
 * then a DUMP DONE <crc32> line. Empties the ring. */
void sniffer_dump(void);

#endif /* VBFC_SNIFFER_H */
