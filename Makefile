NAME = test
OUTPUT_DIR = build
CXX = g++
CXXFLAGS = -x c++ -march=native -O3 -std=c++17
LIBS = -lfplll -lgmp

.PHONY: build run clean

build:
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		mkdir $(OUTPUT_DIR);         \
	fi
	$(CXX) $(CXXFLAGS) -o $(OUTPUT_DIR)/$(NAME) src/$(NAME).cpp $(LIBS)

run: build
	./$(OUTPUT_DIR)/$(NAME)

clean:
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		echo "No files to clean";    \
	else                             \
		rm -vr $(OUTPUT_DIR)/;       \
	fi