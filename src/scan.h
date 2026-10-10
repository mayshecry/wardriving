#ifndef SCAN_H
#define SCAN_H

#include "app.h"

void scan_once(Scanner *scanner);
void *scan_loop(void *argument);

#endif
