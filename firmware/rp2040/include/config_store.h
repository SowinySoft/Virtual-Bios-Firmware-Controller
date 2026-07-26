#ifndef VBFC_CONFIG_STORE_H
#define VBFC_CONFIG_STORE_H

#include <stdbool.h>
#include "shadow_map.h"

void config_store_init(void);
bool config_store_read_map(vbfc_shadow_map_t *map);
bool config_store_write_map(const vbfc_shadow_map_t *map);

#endif /* VBFC_CONFIG_STORE_H */
