#ifndef VBFC_PROTO_BINARY_H
#define VBFC_PROTO_BINARY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Chunked binary framing over the ASCII CDC stream. Two roles:
 *
 *  UPLOAD (host -> device, into ext flash):
 *    "ULOAD START <offset> <total_len>"   device erases the covering range,
 *                                         acks; offset must be >= EXT_OFF_IMAGE_STORE
 *    "ULOAD CHUNK <base64>"               device decodes, writes, acks "OK <total_rcvd>"
 *    "ULOAD DONE <crc32>"                 device verifies total + crc, acks OK/ERR
 *
 *  DUMP (device -> host):
 *    "DUMP ORIG <offset> <len>" | "DUMP EXT <offset> <len>" | "DUMP SNIFF"
 *    device emits one or more "DUMP CHUNK <base64>" lines + "DUMP DONE <crc32>"
 *
 * A line-oriented token walker. `binary_feed()` consumes one complete input
 * line (already trimmed) and may emit reply lines via the printf-backed sink.
 */
bool binary_feed(const char *line);

void binary_init(void);

/* Reset any in-flight upload/dump state (called on factory reset or fault). */
void binary_reset(void);

/* Internal: stream a source range to the host as DUMP CHUNK lines. */
void binary_dump_ext(uint32_t offset, uint32_t len);
void binary_dump_orig(uint32_t offset, uint32_t len);

#endif /* VBFC_PROTO_BINARY_H */
