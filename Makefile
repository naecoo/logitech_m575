CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -framework CoreGraphics -framework CoreFoundation -lpthread

TARGET = m575-scroll
SRCS = main.c scroll_interceptor.c hid_config.c daemon.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	chmod +x /usr/local/bin/$(TARGET)
	echo "✅ 安装完成：/usr/local/bin/$(TARGET)"

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	echo "✅ 卸载完成"

test: $(TARGET)
	./$(TARGET) status