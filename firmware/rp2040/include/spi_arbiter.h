#ifndef VBFC_SPI_ARBITER_H
#define VBFC_SPI_ARBITER_H

#include <stdint.h>
#include <stdbool.h>

void spi_arbiter_init(void);
void spi_arbiter_poll(void);

/* Force hardware pass-through (bypass jumper or fault). */
void spi_arbiter_enable_passthrough(void);
bool spi_arbiter_is_passthrough(void);

#endif /* VBFC_SPI_ARBITER_H */
