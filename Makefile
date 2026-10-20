CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
LDFLAGS = -lm

SRCS = main.c utils.c ocr.c pdf.c pandoc.c xlsx.c table.c log.c
OBJS = $(SRCS:.c=.o)
HDRS = utils.h

TARGET = file-to-file

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

check: $(TARGET)
	cd Sample && ./run_tests.sh

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all check clean install uninstall