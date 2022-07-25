# Makefile for libamy , including an example

TARGET = amy-example
LIBS = -lpthread -lsoundio -lm 
CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing 

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, src/amy-example.c src/algorithms.c src/amy.c src/envelope.c \
	src/filters.c src/oscillators.c src/pcm.c src/partials.c)
HEADERS = $(wildcard src/*.h)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LIBS += -L/opt/homebrew/lib -framework Cocoa -lstdc++ 
	CFLAGS += -I/opt/homebrew/include
endif

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.mm $(HEADERS)
	clang $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f amy/*.o
	-rm -f $(TARGET)
