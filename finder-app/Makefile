# Makefile for Assignment 2

CC = gcc
TARGET = writer

# check if CROSS_COMPILER is set or not
ifdef CROSS_COMPILE
    CC := $(CROSS_COMPILE)gcc
endif


# build application
all: $(TARGET)

$(TARGET): writer.o
	$(CC) writer.o -o $(TARGET)

writer.o: writer.c
	$(CC) -c writer.c

# removes the application and all .o files
clean:
	rm -f $(TARGET) *.o
