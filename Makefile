NAME = crack
FILENAME = test
OUTPUT_DIR = build
CXX = g++
CXXFLAGS = -x c++ -march=native -O3 -std=c++20 -Wall -Wextra -pedantic
LIBS = -lfplll -lgmp
INCLUDES = -I./src

.PHONY: build run-test test clean

test:
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		mkdir $(OUTPUT_DIR); \
	fi
	$(CXX) $(CXXFLAGS) -o $(OUTPUT_DIR)/$(NAME) tests/$(FILENAME).cpp $(LIBS) $(INCLUDES)

run-test: test
	./$(OUTPUT_DIR)/$(NAME)

clean:
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		echo "No files to clean"; \
	else \
		rm -vr $(OUTPUT_DIR)/; \
	fi