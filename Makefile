.PHONY: all install

all: read_sudo_timestamp

CFLAGS += -W -Wall -Werror -ansi -pedantic -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS += -lrt

read_sudo_timestamp: read_sudo_timestamp.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o read_sudo_timestamp read_sudo_timestamp.c

install: read_sudo_timestamp
	install -o root -g root -m u+rwxs,g+rx,o+rx read_sudo_timestamp /usr/local/bin/read_sudo_timestamp
