NAME = crack
FILENAME = test
OUTPUT_DIR = build
CXX = g++
CXXFLAGS = -x c++ -march=native -O3 -std=c++20 -Wall
LIBS = -lfplll -lgmp
INCLUDES = -I./src

.PHONY: build run test clean

test:
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		mkdir $(OUTPUT_DIR); \
	fi
	$(CXX) $(CXXFLAGS) -o $(OUTPUT_DIR)/$(NAME) tests/$(FILENAME).cpp $(LIBS) $(INCLUDES)

run: test
	./$(OUTPUT_DIR)/$(NAME)

clean:
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		echo "No files to clean"; \
	else \
		rm -vr $(OUTPUT_DIR)/; \
	fi