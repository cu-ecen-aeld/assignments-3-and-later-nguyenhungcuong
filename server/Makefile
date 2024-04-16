# Makefile for Assignment 5 Part 1

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror 
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

# build application
all: $(TARGET)

$(TARGET): aesdsocket.o
	$(CC) $(LDFLAGS) aesdsocket.o -o $(TARGET)

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c

# removes the application and all .o files
clean:
	rm -f $(TARGET) *.o
