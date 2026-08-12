# Makefile for ERL Spectra Custom EtherCAT Library & Test Applications

CC      ?= gcc
CFLAGS  = -Wall -Wextra -O2 -fPIC -I. -I/opt/etherlab/include
LDFLAGS = -L/opt/etherlab/lib -Wl,-rpath,/opt/etherlab/lib -lethercat

PREFIX  ?= /usr/local
LIB_NAME = libecat_api.so
HEADER   = ecat_api.h
LIB_SRC  = ecat_api.c

TARGETS = $(LIB_NAME) ecat_test

all: $(TARGETS)

# Compile Shared Dynamic Library (.so)
$(LIB_NAME): $(LIB_SRC)
	$(CC) $(CFLAGS) -shared $(LIB_SRC) $(LDFLAGS) -o $@
	@echo "[BUILD SUCCESS] Shared Library $@ compiled!"

ecat_test: main.c $(LIB_NAME)
	$(CC) $(CFLAGS) main.c -L. -lecat_api $(LDFLAGS) -o $@
	@echo "[BUILD SUCCESS] Compiled $@!"

# Install Header & Library System-Wide (like ecrt.h)
install: $(LIB_NAME) $(HEADER)
	install -d $(PREFIX)/include
	install -d $(PREFIX)/lib
	install -m 644 $(HEADER) $(PREFIX)/include/
	install -m 755 $(LIB_NAME) $(PREFIX)/lib/
	ldconfig
	@echo "[INSTALL SUCCESS] Installed $(HEADER) to $(PREFIX)/include and $(LIB_NAME) to $(PREFIX)/lib!"

clean:
	rm -f $(LIB_NAME) ecat_test *.o

.PHONY: all install clean
