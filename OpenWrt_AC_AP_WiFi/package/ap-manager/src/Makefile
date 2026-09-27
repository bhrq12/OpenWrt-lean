CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?= -lssl -lcrypto -lwebsockets -lcjson -lubus -luci -lpthread

INCLUDES = -I./ -I./ap-manager/include -I./common/include

SRCS = \
	./ap-manager/src/main.c \
	./ap-manager/src/config_adapter.c \
	./ap-manager/src/sys_bus.c \
	./ap-manager/src/ws_client.c \
	./common/src/common.c \
	./common/src/config.c \
	./common/src/log.c \
	./common/src/cjson_schema.c

OBJS = $(SRCS:.c=.o)

TARGET = ap-manager.bin

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)/usr/sbin
	install -m 755 $(TARGET) $(DESTDIR)/usr/sbin/ap-manager

.PHONY: all clean install