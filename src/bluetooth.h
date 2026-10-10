#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "app.h"

void bluetooth_scan_once(BluetoothScanner *scanner);
void *bluetooth_scan_loop(void *argument);

#endif
