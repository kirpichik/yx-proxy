# Compiler
CC=gcc
OS=$(shell uname)
# Compiler flags
INCLUDES=
CFLAGS=-c -Wall -std=gnu99 $(INCLUDES) -D_REENTRANT
LDFLAGS=-lm -lpthread
ifeq ($(OS),SunOS)
LDFLAGS+=-lsocket
endif
# Sources
SOURCES=main.c\
				sockets-handler.c\
				proxy-handler.c\
				http-parser.c
HEADERS=sockets-handler.h\
				proxy-handler.h\
				http-parser.h

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

