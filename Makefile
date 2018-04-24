ifndef PREFIX	
	PREFIX = /usr/local
endif

CC = gcc # C compiler
CFLAGS = -fPIC -Wall -Wextra -O2 -g # C flags
LDFLAGS = -shared  # linking flags
LIBS = -lvkdrv
RM = rm -f  # rm command

TARGET_LIB = libvkil.so # target lib
HDRS = vkil_api.h
SRCS = vkil_api.c vkil_session.c# source files
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install uninstall

all: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(SRCS:.c=.d):%.d:%.c
	$(CC) $(CFLAGS) -MM $< >$@

-include $(SRCS:.c=.d)

clean:
	-$(RM) $(TARGET_LIB) $(OBJS) $(SRCS:.c=.d)

# may want to run with sudo
install:
	install -d $(PREFIX)/lib/
	install -d $(PREFIX)/include/
	install -m 0644 $(TARGET_LIB) $(PREFIX)/lib/
	install -m 0644 $(HDRS) $(PREFIX)/include/
	ldconfig

uninstall:
	$(RM) $(PREFIX)/lib/$(TARGET_LIB)
	$(RM) $(PREFIX)/include/$(HDRS)