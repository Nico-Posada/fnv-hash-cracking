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
C_COVERAGE_SOURCES = $(filter-out src/main.c,$(SOURCES))

# Generate .o file names from .c files
OBJECTS = $(SOURCES:.c=.o)

# Default target (runs when you just type 'make')
.PHONY: all build clean clean-c-coverage build-pyext build-pyext-coverage coverage-c-probe coverage-c

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

clean-c-coverage:
	find . -name '*.gcda' -o -name '*.gcno' -o -name '*.gcov' | xargs -r rm -f

build-pyext:
	uv run --with setuptools --with wheel python setup.py build_ext --inplace --force

build-pyext-coverage:
	CC=$(CC) FNVCRACK_COVERAGE=1 uv run --with setuptools --with wheel python setup.py build_ext --inplace --force

coverage-c-probe:
	mkdir -p build
	$(CC) -O0 -g --coverage -Wall -Isrc tests/c_coverage_probe.c $(C_COVERAGE_SOURCES) -o build/c_coverage_probe $(LIBRARIES) -lmpfr
	./build/c_coverage_probe

coverage-c: clean-c-coverage build-pyext-coverage
	uv run pytest
	$(MAKE) coverage-c-probe
	uv run --with gcovr gcovr --root . --filter 'src/.*' --filter 'python/.*\.c' --exclude 'src/main.c' --txt
