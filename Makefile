# Compiler
CC=gcc
OS=$(shell uname)
# Compiler flags
INCLUDES=
CFLAGS=-c -Wall -std=gnu99 $(INCLUDES) -D_REENTRANT -D_PROXY_DEBUG
LDFLAGS=-lm -lpthread
ifeq ($(OS),SunOS)
LDFLAGS+=-lsocket -lnsl
endif
# Sources
SOURCES=main.c\
				sockets-handler.c\
				pstring.c\
				cache.c\
				proxy-handler.c\
				proxy-client-handler.c\
				proxy-target-handler.c\
				http-parser.c\
				proxy-utils.c
HEADERS=sockets-handler.h\
				pstring.h\
				cache.h\
				proxy-handler.h\
				proxy-client-handler.h\
				proxy-target-handler.h\
				http-parser.h\
				proxy-utils.h

# Compiler output
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=yx-proxy

all: $(HEADERS) $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c Makefile
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(OBJECTS) $(EXECUTABLE)

clear: clean

rebuild: clean all

.PHONY: all clear rebuild $(SOURCES)

