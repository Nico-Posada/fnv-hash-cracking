# Compiler and flags
CC = gcc
CFLAGS = -march=native -O3
# CFLAGS = -march=native -g
LIBRARIES = -lflint -lgmp

# Target executable name
TARGET = main

# Find all .c files in current directory
SOURCES = $(wildcard src/*.c)
HEADERS = $(wildcard src/*.h)

# Generate .o file names from .c files
OBJECTS = $(SOURCES:.c=.o)

# Default target (runs when you just type 'make')
.PHONY: all build clean

all: build

build: $(TARGET)

# Build the main executable from all object files
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBRARIES)

# Pattern rule to compile .c files to .o files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@ $(LIBRARIES)

# Clean up generated files
clean:
	rm -f $(OBJECTS) $(TARGET)