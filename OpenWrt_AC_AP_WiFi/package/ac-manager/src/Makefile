CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?= -lsqlite3 -lssl -lcrypto -lwebsockets -lhiredis -lcjson -luuid -lpthread

INCLUDES = -I./ -I./ac-manager/include -I./common/include

SRCS = \
	./ac-manager/src/main.c \
	./ac-manager/src/auth.c \
	./ac-manager/src/config_manager.c \
	./ac-manager/src/db_interface.c \
	./ac-manager/src/device_manager.c \
	./ac-manager/src/msg_bus.c \
	./ac-manager/src/redis_interface.c \
	./ac-manager/src/ws_server.c \
	./common/src/common.c \
	./common/src/config.c \
	./common/src/log.c

OBJS = $(SRCS:.c=.o)

TARGET = ac-manager.bin

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)/usr/sbin
	install -m 755 $(TARGET) $(DESTDIR)/usr/sbin/ac-manager

.PHONY: all clean install