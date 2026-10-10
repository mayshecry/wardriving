CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
LDLIBS += -pthread -lm
SOURCES = src/main.c src/ap.c src/scan.c src/bluetooth.c src/gps.c src/web.c src/util.c
OBJECTS = $(SOURCES:.c=.o)

all: wardriver

wardriver: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -c $< -o $@

tests/iw: tests/fake_iw.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

tests/scan_test: tests/scan_test.c src/scan.c src/gps.c src/util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $^ -o $@ $(LDLIBS)

tests/bluetoothctl: tests/fake_bluetoothctl.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

tests/bluetooth_test: tests/bluetooth_test.c src/bluetooth.c src/util.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $^ -o $@ $(LDLIBS)

web_test: tests/web_test.c src/web.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $^ -o $@ $(LDLIBS)

test: tests/iw tests/scan_test tests/bluetoothctl tests/bluetooth_test web_test
	PATH="$(CURDIR)/tests:$$PATH" ./tests/scan_test
	PATH="$(CURDIR)/tests:$$PATH" ./tests/bluetooth_test
	./web_test

clean:
	rm -f $(OBJECTS) wardriver web_test tests/iw tests/scan_test tests/bluetoothctl tests/bluetooth_test

.PHONY: all test clean
