# Compiler
CC=cc

# Compiler flags
INCLUDES=
CFLAGS=-c -Wall -std=gnu99 $(INCLUDES) -D_REENTRANT
LDFLAGS=-lm -lpthread

# Sources
SOURCES=main.c\
				cache.c
HEADERS=cache.h

# Compiler output
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=yx-proxy

all: $(HEADERS) $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c Makefile
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(OBJECTS) $(EXECUTABLE) $(BISON_PRE_BUILD) $(BISON_PRE_BUILD:.c=.h)

clear: clean

rebuild: clean all

