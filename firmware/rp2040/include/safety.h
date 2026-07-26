#ifndef VBFC_SAFETY_H
#define VBFC_SAFETY_H

#include <stdbool.h>

void safety_init(void);
void safety_feed(void);
void safety_fault(const char *reason);

bool safety_bypass_active(void);

#endif /* VBFC_SAFETY_H */
